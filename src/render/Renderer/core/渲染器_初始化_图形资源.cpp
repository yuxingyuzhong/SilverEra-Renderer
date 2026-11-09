#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "渲染器内部工具.h"

namespace engine
{

	//初始化阶段：管线 / 描述符 / 逐项采样 / 粒子 / 文字管线 / 阴影目标 / 后处理 / 天空 / 阴影资源
	//（从 init 抽出；失败时由调用方统一 shutdown）
	bool renderer_init_graphics(Renderer* owner, Renderer_Impl_Holder::Impl* impl, const Renderer_Config& effective)
	{
		//⑫ 管线：图形管线（初始档位 = 不透明）
		if (!impl->pipeline.pipeline_build(impl->vert_module, impl->frag_module, Blend_Mode::Opaque,
			impl->scene_input_locations))
		{
			logger.error("渲染器:: 图形管线创建失败");
			owner->shutdown();
			return false;
		}
		/*
		⑫.1 反向外扩描边管线（第二百二十八轮）
		剔除正面的场景管线（沿法线外扩后只剩背面），与默认场景管线共享布局与描述符集布局；
		着色器缺失（未随构建拷贝）时不阻断初始化，仅告警并保持描边关闭。
		*/
		{
			uint64_t outline_handle = 0;
			if (impl->pipeline_load_ex(outline_handle, "反向外扩.vert.spv", "反向外扩.frag.spv",
				Blend_Mode::Opaque, false, false, true, true))
				impl->outline_pipeline = outline_handle;
			else
				logger.warn("渲染器:: 外扩描边管线登记失败（描边将保持关闭）");
		}
		/*
		⑫.2 计算管线（第二百四十一轮）
		计算与图形共用同一队列族；队列族不含 COMPUTE 位时 supported() 为假，初始化不失败（GPU 路径整体关闭）。
		健康检查着色器缺失时不阻断初始化（仅告警，自检返回假）。
		*/
		{
			/*
			GPU 能力与降级矩阵探测（第二百四十六轮）
			与 compute_enable 无关：即使强制关闭计算路径，也如实记录设备能力，便于上层决定是否开启。
			*/
			Compute_Capabilities& capabilities = impl->compute_capabilities;
			capabilities = Compute_Capabilities{};
			VkPhysicalDevice physical = impl->device.physical_device_get();
			uint32_t probe_family = impl->device.graphics_family_get();
			//队列族属性：COMPUTE 位与时间戳有效位
			uint32_t family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
			if (family_count > probe_family)
			{
				std::vector<VkQueueFamilyProperties> families(family_count);
				vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
				capabilities.compute = (families[probe_family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
				capabilities.timestamp = families[probe_family].timestampValidBits > 0;
			}
			//特性查询：多批次间接绘制（核心特性）与 Vulkan 1.2 两项
			VkPhysicalDeviceVulkan12Features features12{};
			features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			VkPhysicalDeviceFeatures2 features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features.pNext = &features12;
			vkGetPhysicalDeviceFeatures2(physical, &features);
			capabilities.multi_draw_indirect = features.features.multiDrawIndirect == VK_TRUE;
			capabilities.draw_indirect_count = features12.drawIndirectCount == VK_TRUE;
			capabilities.host_query_reset = features12.hostQueryReset == VK_TRUE;
			//间接命令以 firstInstance 承担实例偏移，故需该核心特性（第二百五十二轮）
			impl->indirect_first_instance_supported = features.features.drawIndirectFirstInstance == VK_TRUE;
			/*
			Hi-Z 能力探测（第二百五十三轮）
			要求计算路径可用且 R32 浮点格式支持存储图像（金字塔逐级写入）与采样（遮挡查询读取）；
			不满足时 hiz_supported 为假，开启请求被拒绝且不创建任何资源。
			*/
			VkFormatProperties hiz_format_properties{};
			vkGetPhysicalDeviceFormatProperties(physical, HIZ_PYRAMID_FORMAT, &hiz_format_properties);
			const VkFormatFeatureFlags hiz_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
			impl->hiz_capable = capabilities.compute
				&& (hiz_format_properties.optimalTilingFeatures & hiz_features) == hiz_features;
			//强制关闭计算路径时跳过构建（全部 GPU 驱动路径保持关闭，默认帧路径不受影响）
			if (!effective.compute_enable)
			{
				impl->compute_health_handle = 0;
				impl->compute_health_module = VK_NULL_HANDLE;
			}
			else
			{
				Compute_Config compute_config;
				compute_config.frame_overlap = effective.frame_overlap;
				//构建计算上下文（布局 / 池 / 描述符集 / 一次性提交资源）
				if (!impl->compute.build(impl->device.physical_device_get(), impl->device.device_get(),
					impl->device.graphics_family_get(), impl->device.graphics_queue_get(), compute_config))
				{
					logger.error("渲染器:: 计算上下文构建失败");
					owner->shutdown();
					return false;
				}
				//设备支持计算路径时才加载自检着色器并登记管线
				if (impl->compute.supported())
				{
					VkShaderModule compute_module = VK_NULL_HANDLE;
					if (impl->shader.module_load(compute_module, "计算写入.comp.spv"))
					{
						uint64_t compute_handle = 0;
						//登记健康检查管线
						if (impl->compute.pipeline_register(compute_handle, compute_module))
						{
							impl->compute_health_module = compute_module;
							impl->compute_health_handle = compute_handle;
						}
						else
						{
							logger.warn("渲染器:: 计算健康检查管线登记失败");
							impl->shader.module_destroy(compute_module);
						}
					}
					else
						logger.warn("渲染器:: 计算写入着色器加载失败（计算路径保持未启用）");
				}
			}
			//时间戳能力与实际计时链路同口径（队列族有效位 ＋ 设备周期，二者与关闭开关无关）
			VkPhysicalDeviceProperties device_properties{};
			vkGetPhysicalDeviceProperties(physical, &device_properties);
			capabilities.timestamp = capabilities.timestamp && device_properties.limits.timestampPeriod > 0.0f;
		}
		/*
		⑬ 场景 UBO：每帧槽一个主机可见缓冲（64 字节 mat4），
		写入相机视图 × 投影并把该帧槽 set0 绑定到对应 UBO。
		*/
		impl->scene_ubos.resize(effective.frame_overlap);
		impl->scene_ubo_allocs.resize(effective.frame_overlap);
		//实例缓冲按帧槽各一（主机可见，逐帧写入本帧全部实例模型矩阵）
		impl->instance_buffers.resize(effective.frame_overlap);
		impl->instance_allocations.resize(effective.frame_overlap);
		//间接命令缓冲按帧槽各一（第二百五十二轮；主机可见，仅在开关开启时创建）
		impl->indirect_buffers.resize(effective.frame_overlap);
		impl->indirect_allocations.resize(effective.frame_overlap);
		/*
		簇数据占位存储缓冲（第二百八十八轮）
		set0 binding 1..4 在 Cluster 关闭时无真实数据，仍须有完整描述符；64 字节占位缓冲统一占位。
		*/
		if (!impl->cluster_placeholder_ensure())
		{
			logger.error("渲染器:: 簇数据占位缓冲创建失败");
			owner->shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途；第一百二十五轮由 1072 扩为 1088 字节：追加天空盒参数；第一百四十六轮扩为 1168；第一百七十九轮扩为 1184：追加卡通高光参数）
			if (!impl->memory.buffer_create(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				static_cast<VkDeviceSize>(Renderer_Impl_Holder::Impl::SCENE_BLOCK_BYTES),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 场景 UBO 创建失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始视图 × 投影矩阵（列主序 16 浮点）
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], view_proj, 64, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始光照块（offset 64、64 字节：公共参数 ＋ 方向光 ＋ 颜色 ＋ 相机位置）
			float light_block[16] = {
				impl->light_ambient, impl->specular_intensity, impl->specular_shininess, static_cast<float>(impl->point_light_count),
				impl->light_direction[0], impl->light_direction[1], impl->light_direction[2], impl->light_intensity,
				impl->light_color[0], impl->light_color[1], impl->light_color[2], static_cast<float>(impl->attenuation_mode),
				0.0f, 0.0f, 0.0f, 0.0f,
			};
			//相机世界位置（镜面高光的视线方向来源）；位置在首帧前为默认机位，w = 方向光逐光高光缩放（第五十轮）
			float camera_position[3] = {};
			impl->camera.position_get(camera_position);
			light_block[12] = camera_position[0];
			light_block[13] = camera_position[1];
			light_block[14] = camera_position[2];
			light_block[15] = impl->directional_specular;
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], light_block, 64, 64))
			{
				logger.error("渲染器:: 场景光照写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始点光源数组（offset 128、256 字节；旧段 8 个槽位，未用槽位清零；第六十二轮扩为 8 盏）
			float point_block[64] = {};
			for (uint32_t light = 0; light < Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS; ++light)
			{
				//先排全部「位置 ＋ 半径」，再排全部「颜色 ＋ 强度」（与着色器两个数组的 std140 布局一致）
				const std::size_t base = static_cast<std::size_t>(light) * 4;
				point_block[base + 0] = impl->point_lights[light].position[0];
				point_block[base + 1] = impl->point_lights[light].position[1];
				point_block[base + 2] = impl->point_lights[light].position[2];
				point_block[base + 3] = impl->point_lights[light].radius;
				//颜色段起点 = 8 盏位置（32 浮点）之后
				point_block[32 + base + 0] = impl->point_lights[light].color[0];
				point_block[32 + base + 1] = impl->point_lights[light].color[1];
				point_block[32 + base + 2] = impl->point_lights[light].color[2];
				point_block[32 + base + 3] = impl->point_lights[light].intensity;
			}
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], point_block, 256, 128))
			{
				logger.error("渲染器:: 场景点光源写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			写入初始聚光参数（offset 384、256 字节；第三十四轮；第六十二轮扩为 8 盏）
			前 128 字节为 8 个「方向 ＋ 有效标志」，后 128 字节为 8 组「内锥余弦 ＋ 外锥余弦」；
			两个 std140 数组各自连续，故分两段排布。
			*/
			float spot_block[64] = {};
			for (uint32_t light = 0; light < Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS; ++light)
			{
				//槽内基址（每个槽 4 个浮点）
				const std::size_t base = static_cast<std::size_t>(light) * 4;
				spot_block[base + 0] = impl->point_lights[light].spot_direction[0];
				spot_block[base + 1] = impl->point_lights[light].spot_direction[1];
				spot_block[base + 2] = impl->point_lights[light].spot_direction[2];
				spot_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
				//锥参数段起点 = 8 盏方向（32 浮点）之后
				spot_block[32 + base + 0] = impl->point_lights[light].spot_inner_cos;
				spot_block[32 + base + 1] = impl->point_lights[light].spot_outer_cos;
				//z = 逐光源衰减模式（0 = 跟随全局、1 = 线性、2 = 平方反比；第四十六轮）
				spot_block[32 + base + 2] = static_cast<float>(impl->point_lights[light].attenuation);
			}
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], spot_block, 256, 384))
			{
				logger.error("渲染器:: 场景聚光参数写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			写入初始雾参数（offset 640、32 字节；第三十八轮）
			前 16 字节 = 雾色 rgb ＋ 密度 w；后 16 字节 = 起始距离 x ＋ 结束距离 y ＋ 模式 z（第一百四十三轮）。
			*/
			float fog_block[8] = {
				impl->fog_color[0], impl->fog_color[1], impl->fog_color[2], impl->fog_density,
				impl->fog_start, impl->fog_end, static_cast<float>(impl->fog_mode), impl->fog_height_falloff,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], fog_block, 32, 640))
			{
				logger.error("渲染器:: 场景雾参数写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始环境光颜色（offset 672、16 字节；第四十轮）
			float ambient_block[4] = {
				impl->ambient_color[0], impl->ambient_color[1], impl->ambient_color[2], 0.0f,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], ambient_block, 16, 672))
			{
				logger.error("渲染器:: 场景环境光颜色写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始半球环境光（offset 688、16 字节；第四十七轮）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				impl->ambient_ground, 16, 688))
			{
				logger.error("渲染器:: 场景半球环境光写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始补光方向与强度（offset 704、16 字节；第四十七轮）
			float fill_dir_block[4] = {
				impl->fill_direction[0], impl->fill_direction[1], impl->fill_direction[2], impl->fill_intensity,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				fill_dir_block, 16, 704))
			{
				logger.error("渲染器:: 场景补光方向写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始补光颜色（offset 720、16 字节；第四十七轮；w = 逐光高光缩放，第五十轮）
			float fill_color_block[4] = {
				impl->fill_color[0], impl->fill_color[1], impl->fill_color[2], impl->fill_specular,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				fill_color_block, 16, 720))
			{
				logger.error("渲染器:: 场景补光颜色写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始逐盏光泽度（offset 736、128 字节；第五十一轮；第六十二轮扩为 8 盏，std140 每盏占 16 字节）
			float shininess_block[32] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shininess_block, 128, 736))
			{
				logger.error("渲染器:: 场景逐盏光泽度写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			写入高 24 槽（offset 1840、1920 字节；第六百九十九轮 8 槽、第七百零一轮 24 槽）
			前向 32 槽中的 8..31 槽追加在场景 UBO 块尾（三组 × 160 浮点），旧 8 槽偏移保持不变；
			每帧由 scene_apply 覆盖，此处先清零保证首帧前不读取脏数据。
			*/
			float high_slot_block[480] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				high_slot_block, 1920, static_cast<VkDeviceSize>(Renderer_Impl_Holder::Impl::SCENE_HIGH_SLOTS_BASE) * sizeof(float)))
			{
				logger.error("渲染器:: 场景高 8 槽写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始阴影块（offset 864、96 字节；第五十二轮：光源空间矩阵 64 ＋ 阴影参数 16；第五十三轮：阴影强度 4 ＋ 尾部圆整 12）
			float shadow_init_block[24] = {};
			//阴影强度默认 1（块内第 20 浮点 = offset 944）
			shadow_init_block[20] = 1.0f;
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shadow_init_block, 96, 864))
			{
				logger.error("渲染器:: 场景阴影参数写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//写入初始 PBR-lite 参数（offset 960、16 字节；第六十四轮：默认关闭、粗糙度缩放 1）
			float pbr_init_block[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				pbr_init_block, 16, 960))
			{
				logger.error("渲染器:: 场景 PBR 参数写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//把该帧槽 set0 绑定到对应 UBO
			if (!impl->pipeline.scene_bind(slot, impl->scene_ubos[slot]))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			该帧槽 set0 的簇数据绑定（第二百八十八轮）
			初始化阶段统一指向占位缓冲；Cluster 开启后由 cluster_graphics_ensure 改写为真实缓冲。
			*/
			if (!impl->pipeline.scene_cluster_bind(slot, impl->cluster_placeholder_buffer,
				impl->cluster_placeholder_buffer, impl->cluster_placeholder_buffer, impl->cluster_placeholder_buffer))
			{
				logger.error("渲染器:: 簇数据占位绑定写入失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			//创建该帧槽的实例缓冲（主机可见、顶点缓冲用途；容量 = instance_capacity 个实例，一个实例 80 字节）
			if (!impl->memory.buffer_create(impl->instance_buffers[slot], impl->instance_allocations[slot],
				static_cast<VkDeviceSize>(impl->instance_capacity) * INSTANCE_STRIDE_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 实例缓冲创建失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			创建该帧槽的间接命令缓冲（第二百五十二轮）
			仅在开关开启且设备支持 firstInstance 偏移时建立；否则该槽保持空缓冲、录制走逐项绘制。
			主机一致内存使命令写入后无需显式冲刷；创建或映射失败仅告警并回落逐项，不阻断初始化。
			*/
			if (effective.gpu_indirect_enable && impl->indirect_first_instance_supported)
			{
				if (!impl->memory.buffer_create(impl->indirect_buffers[slot], impl->indirect_allocations[slot],
					static_cast<VkDeviceSize>(Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_CAPACITY) * Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_BYTES,
					VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true, false, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
					logger.warn("渲染器:: 间接命令缓冲创建失败（帧槽 {}，将回落逐项绘制）", slot);
				else
				{
					//取持久映射指针（命令由主机侧录制时写入）
					void* indirect_mapped = nullptr;
					if (!impl->memory.buffer_map(impl->indirect_buffers[slot], impl->indirect_allocations[slot], indirect_mapped))
						logger.warn("渲染器:: 间接命令缓冲映射失败（帧槽 {}，将回落逐项绘制）", slot);
					else if (!impl->frame.slot_indirect_buffer_set(slot, impl->indirect_buffers[slot], indirect_mapped,
						Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_CAPACITY))
						logger.warn("渲染器:: 间接命令缓冲登记失败（帧槽 {}，将回落逐项绘制）", slot);
				}
			}
		}
		/*
		⑭ 纹理：记录内存与设备，生成内置默认白纹理（2×2 全白）并绑定到所有帧槽 set1，
		保证 descriptor_set_count = 2 时不存在未写入的描述符集。
		*/
		if (!impl->texture.build(impl->memory, impl->device.device_get()))
		{
			logger.error("渲染器:: 纹理模块初始化失败");
			owner->shutdown();
			return false;
		}
		//2×2 全白像素（RGBA8 紧排）
		const uint8_t white_pixels[16] = {
			255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!impl->texture.texture_create_from_memory(impl->default_texture, white_pixels, 2, 2))
		{
			logger.error("渲染器:: 默认白纹理创建失败");
			owner->shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			owner->shutdown();
			return false;
		}
		//初始采样状态（逐绘制项采样集按此快照写入；第一百七十四轮）
		impl->bound_view = default_view;
		impl->bound_sampler = default_sampler;
		for (VkImageView& material_view : impl->material_views)
			material_view = default_view;
		//把默认纹理写入所有帧槽 set1（长命资源，一次写满）
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			if (!impl->pipeline.texture_bind(slot, default_sampler, default_view))
			{
				logger.error("渲染器:: 默认纹理绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			材质贴图槽同步写入默认白纹理（第一百二十四轮；第一百七十轮合并为一次批量写）
			场景片元静态声明 binding 3 / 4 / 6 / 7 / 8，即使未使用材质也必须有有效绑定；
			白纹理使法线 / 粗糙度 / 自发光 / 遮蔽 / 金属度乘子恒为 1，故不影响默认画面。
			*/
			if (!impl->pipeline.material_textures_bind(slot, default_sampler, default_view, default_view,
				default_view, default_view, default_view))
			{
				logger.error("渲染器:: 默认材质贴图绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
		}
		/*
		内置纯白立方体（第一百二十五轮）
		场景片元静态声明 binding 5（samplerCube），故即使未启用天空盒也必须有有效绑定；
		1×1×6 面全白，且默认天空盒强度为 0，故不改变既有画面。
		*/
		const uint8_t white_cube_faces[6 * 4] = {
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!impl->texture.texture_create_cube(impl->default_sky_texture, white_cube_faces, 1, 1))
		{
			logger.error("渲染器:: 默认天空盒立方体创建失败");
			owner->shutdown();
			return false;
		}
		//取默认立方体的视图与采样器（与默认白纹理共用同一共享采样器）
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_sky_texture, sky_view, sky_sampler))
		{
			logger.error("渲染器:: 默认天空盒立方体视图获取失败");
			owner->shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
			if (!impl->pipeline.sky_bind(slot, sky_sampler, sky_view))
			{
				logger.error("渲染器:: 默认天空盒绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
		return true;
	}
}
