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
	namespace
	{
		//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；第一百零六轮）
		std::filesystem::path path_from_utf8(const std::string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}
	}

	//取门面自身

	//初始化（任一环节失败即逆序回退）
	bool Renderer_Core::init(const Renderer_Config& config)
	{
		//若实现体未分配成功
		if (impl == nullptr)
		{
			logger.error("渲染器:: 实现体分配失败");
			return false;
		}
		//若已初始化
		if (impl->initialized)
		{
			logger.warn("渲染器:: 已初始化，忽略重复 init");
			return true;
		}

		//参数校验：宽高非零
		if (config.width == 0 || config.height == 0)
		{
			logger.error("渲染器:: 窗口尺寸不得为零（{}×{}）", config.width, config.height);
			return false;
		}
		//参数校验：在途帧数量在 [1,3]
		if (config.frame_overlap < 1 || config.frame_overlap > 3)
		{
			logger.error("渲染器:: 在途帧数量越界（{}，应在 [1,3]）", config.frame_overlap);
			return false;
		}

		//生效配置（Release 下强制关闭校验层；K21）
		Renderer_Config effective = config;
#ifdef NDEBUG
		effective.validation = false;
#endif

		//① 平台：视频子系统与窗口
		if (!impl->platform.build(effective.width, effective.height, effective.title, effective.hidden))
		{
			logger.error("渲染器:: 平台初始化失败");
			self().shutdown();
			return false;
		}
		//② 平台：实例扩展清单
		vector<const char*> instance_extensions;
		//输入聚合器绑定平台窗口（文本输入启停需要）
		impl->input.window_set(impl->platform.window_get());
		if (!impl->platform.instance_extensions_get(instance_extensions))
		{
			logger.error("渲染器:: 实例扩展清单获取失败");
			self().shutdown();
			return false;
		}
		//③ 设备：实例与调试信使
		if (!impl->device.build_instance(instance_extensions, effective.validation))
		{
			logger.error("渲染器:: 实例创建失败");
			self().shutdown();
			return false;
		}
		//④ 平台：表面（必须先于设备创建，供呈现队列族查询）
		if (!impl->platform.surface_build(impl->device.instance_get()))
		{
			logger.error("渲染器:: 表面创建失败");
			self().shutdown();
			return false;
		}
		//⑤ 设备：物理设备与逻辑设备
		if (!impl->device.build_device(impl->platform.surface_get()))
		{
			logger.error("渲染器:: 逻辑设备创建失败");
			self().shutdown();
			return false;
		}
		//⑥ 内存：VMA 分配器
		if (!impl->memory.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get()))
		{
			logger.error("渲染器:: 内存初始化失败");
			self().shutdown();
			return false;
		}

		//取窗口客户区实际尺寸（HiDPI 下可能与配置不同）
		uint32_t swapchain_width = 0;
		uint32_t swapchain_height = 0;
		impl->platform.window_size_get(swapchain_width, swapchain_height);
		//若窗口尺寸不可用则回退配置值
		if (swapchain_width == 0 || swapchain_height == 0)
		{
			swapchain_width = effective.width;
			swapchain_height = effective.height;
		}
		//⑦ 交换链（深度图像依赖内存模块，故传入 memory；首次建链固定单采样，待格式确定后再择定 MSAA）
		impl->swapchain.sample_count_set(1);
		if (!impl->swapchain.build(impl->device.physical_device_get(), impl->memory, impl->device.device_get(),
			impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
			swapchain_width, swapchain_height, effective.vsync))
		{
			logger.error("渲染器:: 交换链创建失败");
			self().shutdown();
			return false;
		}
		/*
		MSAA 采样数择定（第九十六轮）
		交换链格式已确定，按设备对「颜色 ＋ 深度」两种格式的共同支持上限钳制请求值；
		请求 > 1 时重建一次交换链以建立多重采样附件（初始化期一次性开销）。
		*/
		{
			//请求值归一（仅接受 2 / 4 / 8，其余按 1）
			uint32_t requested_samples = effective.msaa_samples;
			if (requested_samples != 2 && requested_samples != 4 && requested_samples != 8)
				requested_samples = 1;
			//按设备支持钳制
			uint32_t chosen_samples = msaa_supported_max(impl->device.physical_device_get(),
				impl->swapchain.format_get(), impl->swapchain.depth_format_get(), requested_samples);
			//请求未被完全满足时告警
			if (requested_samples > 1 && chosen_samples < requested_samples)
				logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", requested_samples, chosen_samples);
			//若启用 MSAA：重建交换链以建立多重采样附件
			if (chosen_samples > 1)
			{
				impl->swapchain.sample_count_set(chosen_samples);
				impl->swapchain.destroy();
				if (!impl->swapchain.build(impl->device.physical_device_get(), impl->memory, impl->device.device_get(),
					impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
					swapchain_width, swapchain_height, effective.vsync))
				{
					logger.error("渲染器:: 交换链 MSAA 附件创建失败");
					self().shutdown();
					return false;
				}
			}
			impl->msaa_samples = impl->swapchain.sample_count_get();
			logger.info("渲染器:: MSAA 采样数 {}", impl->msaa_samples);
		}
		/*
		钳制在途帧数
		图像可用信号量属「每交换链图像」（数量 = 交换链图像数），帧模块按该池轮转取用；
		仅当在途帧数不超过信号量数时，「先等帧栅栏」才能保证被取用的信号量已在前一次被消费（K13）。
		*/
		uint32_t image_available_count = impl->swapchain.image_available_count_get();
		//若在途帧数超过图像可用信号量数则下调并告警
		if (image_available_count > 0 && effective.frame_overlap > image_available_count)
		{
			logger.warn("渲染器:: 在途帧数 {} 超过图像可用信号量数 {}，已下调至前者", effective.frame_overlap, image_available_count);
			effective.frame_overlap = image_available_count;
		}
		//⑧ 帧（像素回读缓冲依赖内存模块，故传入 memory）
		/*
		实例存储容量与渲染工作线程（第一百七十七轮）
		容量钳到 [16, 1048576]，随后按该容量分配逐帧实例缓冲（默认 256，与引入该配置前一致）；
		线程数大于 0 时启动工作线程，启动失败回落为单线程（仅影响耗时，不影响结果）。
		*/
		{
			//容量归一
			uint32_t instance_capacity = effective.instance_capacity;
			if (instance_capacity < 16)
				instance_capacity = 16;
			if (instance_capacity > 1048576)
				instance_capacity = 1048576;
			impl->instance_capacity = instance_capacity;
			/*
			实例登记缓冲预保留（第一百九十四轮，优化）
			pending_items 已在下方按实例容量预留（第一百三十六轮）；此处补 pending_instances 的同类预留，
			避免帧循环内首次扩容（行为不变）。
			*/
			impl->pending_instances.reserve(impl->instance_capacity);
			//掩码缓冲按容量预留（避免逐次调用分配）
			impl->instanced_cull_mask.reserve(instance_capacity);
			impl->instance_changed_mask.reserve(instance_capacity);
			//线程数归一
			uint32_t render_threads = effective.render_threads;
			if (render_threads > 64)
				render_threads = 64;
			//启动工作线程（0 = 单线程）
			if (render_threads > 0 && !impl->thread_pool.threads_start(render_threads))
			{
				logger.warn("渲染器:: 渲染工作线程启动失败，回落为单线程");
				render_threads = 0;
			}
			impl->render_threads = render_threads;
			/*
			实例剔除并行阈值（第二百二十二轮；钳 1..65536）
			默认 256 与引入该配置前一致；调低可让小批量实例也走并行，调高则更保守。
			*/
			uint32_t cull_parallel_min = effective.cull_parallel_min_instances;
			if (cull_parallel_min < 1)
				cull_parallel_min = 1;
			if (cull_parallel_min > 65536)
				cull_parallel_min = 65536;
			impl->cull_parallel_min_instances = cull_parallel_min;
			/*
			差异扫描 / 缓存回写并行阈值（第二百二十六轮；钳 1..65536）
			默认 512 与引入该配置前一致。
			*/
			uint32_t scan_parallel_min = effective.instance_scan_parallel_min;
			if (scan_parallel_min < 1)
				scan_parallel_min = 1;
			if (scan_parallel_min > 65536)
				scan_parallel_min = 65536;
			impl->instance_scan_parallel_min = scan_parallel_min;
			/*
			并行录制最小项数（第二百零二轮；钳到 2..4096）
			线程数为 0 时该值无作用（始终单线程）。
			*/
			uint32_t parallel_min_items = effective.parallel_min_items;
			if (parallel_min_items < 2)
				parallel_min_items = 2;
			if (parallel_min_items > 4096)
				parallel_min_items = 4096;
			impl->parallel_min_items = parallel_min_items;
			//把阈值同步给帧模块（第二百零二轮）
			impl->frame.parallel_min_items_set(parallel_min_items);
			/*
			并行段数上限（第二百零三轮；钳到 2..32）
			段数 = min(线程数, 上限, 项数 / 16)。
			*/
			uint32_t parallel_max_segments = effective.parallel_max_segments;
			if (parallel_max_segments < 2)
				parallel_max_segments = 2;
			if (parallel_max_segments > 32)
				parallel_max_segments = 32;
			impl->parallel_max_segments = parallel_max_segments;
			impl->frame.parallel_max_segments_set(parallel_max_segments);
			//把线程池交给帧模块（第一百九十七轮：场景 pass 次级缓冲并行录制；空 / 0 线程时退回单线程）
			impl->frame.thread_pool_set(&impl->thread_pool);
			logger.info("渲染器:: 渲染工作线程 {}（每帧实例容量 {}）", render_threads, instance_capacity);
		}
		/*
		预留帧内待绘制项与实例存储容量（第一百三十六轮）
		容量取自实例上限，避免逐帧扩容；仅在初始化时执行一次。
		*/
		impl->pending_items.reserve(impl->instance_capacity);
		impl->pending_instances.reserve(impl->instance_capacity);
		//逐绘制项采样集输入缓冲按预留上限预分配（第二百二十一轮，避免逐帧扩容）
		impl->draw_set_inputs.reserve(Pipeline_Context::DRAW_SET_PER_SLOT);
		//实例级剔除的筛选缓冲（第一百五十三轮；按实例上限预留，避免逐帧扩容）
		impl->instanced_cull_scratch.reserve(static_cast<std::size_t>(impl->instance_capacity) * 16);
		impl->batch_scratch.reserve(impl->instance_capacity);
		//阴影列表与后处理绘制项的复用缓冲（第一百八十二轮；避免逐帧堆分配）
		impl->shadow_items_scratch.reserve(64);
		impl->post_items_scratch.reserve(POST_CHAIN_MAX_STAGES + 2);
		//后处理级管线序列复用缓冲（第一百八十七轮）
		impl->post_stage_pipelines_scratch.reserve(POST_CHAIN_MAX_STAGES + 1);

		if (!impl->frame.build(impl->device.device_get(), impl->device.physical_device_get(),
			impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->device.present_family_get(), impl->device.present_queue_get(),
			impl->swapchain, impl->memory, effective.frame_overlap))
		{
			logger.error("渲染器:: 帧基础设施创建失败");
			self().shutdown();
			return false;
		}
		//⑨ 着色器：记录逻辑设备与着色器目录
		if (!impl->shader.build(impl->device.device_get()))
		{
			logger.error("渲染器:: 着色器模块初始化失败");
			self().shutdown();
			return false;
		}
		impl->shader.directory_set(effective.shader_dir);
		//⑩ 加载顶点 / 片元着色器模块
		//加载场景顶点着色器（顺带解析其声明的顶点输入位置，供管线裁剪属性）
		if (!impl->shader.module_load(impl->vert_module, "场景.vert.spv", &impl->scene_input_locations))
		{
			logger.error("渲染器:: 顶点着色器加载失败");
			self().shutdown();
			return false;
		}
		if (!impl->shader.module_load(impl->frag_module, "场景.frag.spv"))
		{
			logger.error("渲染器:: 片元着色器加载失败");
			self().shutdown();
			return false;
		}
		//⑪ 管线：描述符布局 / 管线布局 / 描述符池与每帧槽描述符集
		{
			Pipeline_Config pipeline_config{};
			pipeline_config.color_format = impl->swapchain.format_get();
			pipeline_config.depth_format = impl->swapchain.depth_format_get();
			pipeline_config.frame_overlap = effective.frame_overlap;
			pipeline_config.sample_count = impl->msaa_samples;
			/*
			推送常量块上限探测（第四百零一轮）
			Vulkan 保底 128 字节；桌面常见 256。本层把后处理第四组（offset 128..143）做成「有则用、无则降级」，
			故先取物理设备上限：≥ 144 用 144 并开放 post_stage_extra_set，否则维持 128 并拒绝该接口。
			*/
			VkPhysicalDeviceProperties device_props{};
			vkGetPhysicalDeviceProperties(impl->device.physical_device_get(), &device_props);
			impl->post_extra_supported = device_props.limits.maxPushConstantsSize >= 144;
			pipeline_config.max_push_constant_size = impl->post_extra_supported ? 144 : 128;
			impl->frame.post_extra_enabled = impl->post_extra_supported;
			if (!impl->post_extra_supported)
				logger.warn("渲染器:: 设备推送常量上限 {} 字节不足 144，后处理逐级 extra 参数已禁用",
					device_props.limits.maxPushConstantsSize);
			if (!impl->pipeline.build(impl->device.device_get(), pipeline_config))
			{
				logger.error("渲染器:: 管线基础设施创建失败");
				self().shutdown();
				return false;
			}
		}
		/*
		载入管线缓存（第一百零六轮；可选）
		须在创建任何图形管线之前注入；文件缺失或读取失败仅告警，不阻断初始化。
		*/
		if (!effective.pipeline_cache_path.empty())
		{
			std::ifstream cache_stream(path_from_utf8(effective.pipeline_cache_path), std::ios::binary | std::ios::ate);
			//文件存在则读出全部字节
			if (cache_stream.is_open())
			{
				std::streamsize cache_size = cache_stream.tellg();
				if (cache_size > 0)
				{
					std::vector<uint8_t> cache_bytes(static_cast<std::size_t>(cache_size));
					cache_stream.seekg(0);
					cache_stream.read(reinterpret_cast<char*>(cache_bytes.data()), cache_size);
					if (!cache_bytes.empty())
						impl->pipeline.cache_set(cache_bytes.data(), cache_bytes.size());
				}
				cache_stream.close();
			}
		}
		//⑫ 管线：图形管线（初始档位 = 不透明）
		if (!impl->pipeline.pipeline_build(impl->vert_module, impl->frag_module, Blend_Mode::Opaque,
			impl->scene_input_locations))
		{
			logger.error("渲染器:: 图形管线创建失败");
			self().shutdown();
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
					self().shutdown();
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
			self().shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途；第一百二十五轮由 1072 扩为 1088 字节：追加天空盒参数；第一百四十六轮扩为 1168；第一百七十九轮扩为 1184：追加卡通高光参数）
			if (!impl->memory.buffer_create(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				static_cast<VkDeviceSize>(Impl::SCENE_BLOCK_BYTES),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 场景 UBO 创建失败（帧槽 {}）", slot);
				self().shutdown();
				return false;
			}
			//写入初始视图 × 投影矩阵（列主序 16 浮点）
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], view_proj, 64, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				self().shutdown();
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
				self().shutdown();
				return false;
			}
			//写入初始点光源数组（offset 128、256 字节；旧段 8 个槽位，未用槽位清零；第六十二轮扩为 8 盏）
			float point_block[64] = {};
			for (uint32_t light = 0; light < Impl::UBO_LEGACY_SLOTS; ++light)
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
				self().shutdown();
				return false;
			}
			/*
			写入初始聚光参数（offset 384、256 字节；第三十四轮；第六十二轮扩为 8 盏）
			前 128 字节为 8 个「方向 ＋ 有效标志」，后 128 字节为 8 组「内锥余弦 ＋ 外锥余弦」；
			两个 std140 数组各自连续，故分两段排布。
			*/
			float spot_block[64] = {};
			for (uint32_t light = 0; light < Impl::UBO_LEGACY_SLOTS; ++light)
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
				self().shutdown();
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
				self().shutdown();
				return false;
			}
			//写入初始环境光颜色（offset 672、16 字节；第四十轮）
			float ambient_block[4] = {
				impl->ambient_color[0], impl->ambient_color[1], impl->ambient_color[2], 0.0f,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], ambient_block, 16, 672))
			{
				logger.error("渲染器:: 场景环境光颜色写入失败（帧槽 {}）", slot);
				self().shutdown();
				return false;
			}
			//写入初始半球环境光（offset 688、16 字节；第四十七轮）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				impl->ambient_ground, 16, 688))
			{
				logger.error("渲染器:: 场景半球环境光写入失败（帧槽 {}）", slot);
				self().shutdown();
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
				self().shutdown();
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
				self().shutdown();
				return false;
			}
			//写入初始逐盏光泽度（offset 736、128 字节；第五十一轮；第六十二轮扩为 8 盏，std140 每盏占 16 字节）
			float shininess_block[32] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shininess_block, 128, 736))
			{
				logger.error("渲染器:: 场景逐盏光泽度写入失败（帧槽 {}）", slot);
				self().shutdown();
				return false;
			}
			/*
			写入高 24 槽（offset 1840、1920 字节；第六百九十九轮 8 槽、第七百零一轮 24 槽）
			前向 32 槽中的 8..31 槽追加在场景 UBO 块尾（三组 × 160 浮点），旧 8 槽偏移保持不变；
			每帧由 scene_apply 覆盖，此处先清零保证首帧前不读取脏数据。
			*/
			float high_slot_block[480] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				high_slot_block, 1920, static_cast<VkDeviceSize>(Impl::SCENE_HIGH_SLOTS_BASE) * sizeof(float)))
			{
				logger.error("渲染器:: 场景高 8 槽写入失败（帧槽 {}）", slot);
				self().shutdown();
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
				self().shutdown();
				return false;
			}
			//写入初始 PBR-lite 参数（offset 960、16 字节；第六十四轮：默认关闭、粗糙度缩放 1）
			float pbr_init_block[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				pbr_init_block, 16, 960))
			{
				logger.error("渲染器:: 场景 PBR 参数写入失败（帧槽 {}）", slot);
				self().shutdown();
				return false;
			}
			//把该帧槽 set0 绑定到对应 UBO
			if (!impl->pipeline.scene_bind(slot, impl->scene_ubos[slot]))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
				self().shutdown();
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
				self().shutdown();
				return false;
			}
			//创建该帧槽的实例缓冲（主机可见、顶点缓冲用途；容量 = instance_capacity 个实例，一个实例 80 字节）
			if (!impl->memory.buffer_create(impl->instance_buffers[slot], impl->instance_allocations[slot],
				static_cast<VkDeviceSize>(impl->instance_capacity) * INSTANCE_STRIDE_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 实例缓冲创建失败（帧槽 {}）", slot);
				self().shutdown();
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
					static_cast<VkDeviceSize>(Impl::INDIRECT_COMMAND_CAPACITY) * Impl::INDIRECT_COMMAND_BYTES,
					VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true, false, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
					logger.warn("渲染器:: 间接命令缓冲创建失败（帧槽 {}，将回落逐项绘制）", slot);
				else
				{
					//取持久映射指针（命令由主机侧录制时写入）
					void* indirect_mapped = nullptr;
					if (!impl->memory.buffer_map(impl->indirect_buffers[slot], impl->indirect_allocations[slot], indirect_mapped))
						logger.warn("渲染器:: 间接命令缓冲映射失败（帧槽 {}，将回落逐项绘制）", slot);
					else if (!impl->frame.slot_indirect_buffer_set(slot, impl->indirect_buffers[slot], indirect_mapped,
						Impl::INDIRECT_COMMAND_CAPACITY))
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
			self().shutdown();
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
			self().shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			self().shutdown();
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
				self().shutdown();
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
				self().shutdown();
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
			self().shutdown();
			return false;
		}
		//取默认立方体的视图与采样器（与默认白纹理共用同一共享采样器）
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_sky_texture, sky_view, sky_sampler))
		{
			logger.error("渲染器:: 默认天空盒立方体视图获取失败");
			self().shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
			if (!impl->pipeline.sky_bind(slot, sky_sampler, sky_view))
			{
				logger.error("渲染器:: 默认天空盒绑定失败（帧槽 {}）", slot);
				self().shutdown();
				return false;
			}
		//⑮ 网格：记录内存模块
		if (!impl->mesh.build(impl->memory))
		{
			logger.error("渲染器:: 网格模块初始化失败");
			self().shutdown();
			return false;
		}
		//⑯ 渲染目标：记录设备 / 图形队列与内存模块（离屏目标按需创建）
		if (!impl->render_target.build(impl->device.device_get(), impl->device.graphics_family_get(),
			impl->device.graphics_queue_get(), impl->memory))
		{
			logger.error("渲染器:: 渲染目标模块初始化失败");
			self().shutdown();
			return false;
		}
		//渲染目标默认采样数与交换链一致（离屏 / 后处理目标随 MSAA 建立多重采样附件）
		impl->render_target.sample_count_set(impl->msaa_samples);
		//⑰ 字体：记录纹理模块（字形图集经纹理模块上传与销毁）
		if (!impl->font.build(impl->texture))
		{
			logger.error("渲染器:: 字体模块初始化失败");
			self().shutdown();
			return false;
		}
		/*
		文字绘制宿主绑定（第四十七轮下沉）
		文字绘制子系统只持网格缓存与文字颜色；字形补烘之外的 Vulkan 侧动作
		（管线登记、管线 / 纹理绑定、网格创建与归还、绘制项登记、目标尺寸查询）经本组回调转交本门面。
		回调均为无捕获 lambda（转函数指针），以 this 作上下文；生命周期不超过 Renderer。
		*/
		{
			Text_Host text_host{};
			text_host.user = this;
			//确保文字管线就绪（懒登记：Alpha 混合档，与场景管线共享同一管线布局）
			text_host.pipeline_ensure = [](void* user) -> uint64_t
			{
				Renderer* self = static_cast<Renderer*>(user);
				//尚未初始化则无管线可登记
				if (self->impl == nullptr || !self->impl->initialized)
					return self->impl == nullptr ? 0 : self->impl->text.pipeline_get();
				//已登记则直接返回
				if (self->impl->text.pipeline_get() != 0)
					return self->impl->text.pipeline_get();
				uint64_t handle = 0;
				//登记失败保持 0（文字覆盖率写入按配置决定；仅 MSAA > 1× 时生效）
				if (!self->pipeline_load(handle, "文字.vert.spv", "文字.frag.spv", Blend_Mode::Alpha,
					self->impl->config.text_alpha_to_coverage))
					return 0;
				self->impl->text.pipeline_set(handle);
				return handle;
			};
			//绑定文字管线
			text_host.pipeline_bind = [](void* user, uint64_t pipeline)
			{
				static_cast<Renderer*>(user)->pipeline_bind_set(pipeline);
			};
			//绑定字形图集纹理
			text_host.texture_bind = [](void* user, uint64_t texture)
			{
				static_cast<Renderer*>(user)->texture_bind_set(texture);
			};
			//创建文字网格
			text_host.mesh_create = [](void* user, Mesh_Handle& mesh, const Mesh_Data& data) -> bool
			{
				return static_cast<Renderer*>(user)->impl->mesh.mesh_create(mesh, data);
			};
			//归还文字网格（网格模块未就绪时只丢弃句柄）
			text_host.mesh_destroy = [](void* user, Mesh_Handle mesh)
			{
				Renderer* self = static_cast<Renderer*>(user);
				//若网格模块仍有效
				if (self->impl != nullptr && self->impl->mesh.valid())
					self->impl->mesh.mesh_destroy(mesh);
			};
			//登记文字绘制项
			text_host.mesh_draw = [](void* user, Mesh_Handle mesh, const float* transform, const float* color) -> bool
			{
				return static_cast<Renderer*>(user)->mesh_draw(mesh, transform, color);
			};
			//取当前绘制目标尺寸（像素）
			text_host.extent_get = [](void* user, float& width, float& height)
			{
				Renderer* self = static_cast<Renderer*>(user);
				width = static_cast<float>(self->extent_width());
				height = static_cast<float>(self->extent_height());
			};
			//绑定字体模块与宿主回调
			impl->text.init(impl->font, text_host);
		}
		//⑱ 相机：设置宽高比（取当前交换链尺寸）
		{
			VkExtent2D extent = impl->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				impl->camera.aspect_set(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		/*
		⑲ UI 通道
		向上层提供 ImGui Vulkan 后端所需的实例 / 设备 / 队列 / 窗口 / 图像信息与动态渲染口径；
		绘制回调由上层经 self().ui_channel() 另行设置（渲染层不代管 UI）。
		*/
		impl->ui_channel.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->platform.window_get(), effective.frame_overlap, impl->swapchain.image_count_get(),
		impl->swapchain.format_get(), impl->swapchain.depth_format_get());
		/*
		把事件泵的 SDL 事件桥接到 UI 通道
		上层经 self().ui_channel()->event_callback_set 注册回调后，事件泵每帧逐个事件转交，
		供上层平台后端（imgui_impl_sdl3）处理键鼠与文本输入；未注册时该桥接为空操作。
		桥接不持有资源；解除见 shutdown。
		*/
		impl->platform.event_forward_set(
			[](const SDL_Event* event, void* user_data) {
				//本实现体（先喂输入聚合器，再转交外部 UI 平台后端）
				Renderer_Impl_Holder::Impl* self = static_cast<Renderer_Impl_Holder::Impl*>(user_data);
				self->input.event_feed(event);
				self->ui_channel.event_dispatch(event);
			},
			impl.get());

		/*
		原生 UI 装配（保留模式控件树 + UI 阶段绘制）
		管线惰性登记失败只告警（原生 UI 不绘制），不影响既有渲染路径；
		UI 上下文随渲染器生命周期存续，控件由上层经 self().ui_context() 创建。
		*/
		impl->ui.build();
		impl->ui_pipeline_ensure();
		if (!impl->ui_render.build(impl->device.device_get(), impl->memory, impl->pipeline,
			impl->texture, impl->font, impl->ui_solid_pipeline, impl->ui_text_pipeline,
			effective.frame_overlap))
			logger.warn("渲染器:: 原生 UI 渲染器装配失败（原生 UI 不绘制）");

		//记录配置并标记初始化完成
		impl->config = effective;
		//静态合批开关（第一百二十七轮；默认开启）
		impl->static_batching = effective.static_batching;
		//GPU 剔除开关（第二百四十九轮；默认关闭）
		impl->gpu_cull_enabled = effective.gpu_cull_enable;
		//间接绘制开关（第二百五十二轮；默认关闭）
		impl->gpu_indirect_enabled = effective.gpu_indirect_enable;
		/*
		Hi-Z 开关（第二百五十三 / 二百五十四轮）
		真正的启用（构建资源 ＋ 懒加载生成管线）放在 initialized 置真之后，以便复用门面启用路径。
		*/
		//SSAA 倍率生效（钳制 1..4；默认 1 保持既有画面逐字节不变）
		impl->ssaa_factor = effective.ssaa_factor < 1 ? 1 : (effective.ssaa_factor > 4 ? 4 : effective.ssaa_factor);
		impl->ssaa_extent_failed = false;
		//记录生效的 MSAA 采样数（已按设备支持钳制）
		impl->config.msaa_samples = impl->msaa_samples;
		//MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = effective.msaa_depth_resolve;
		/*
		Cluster 光照能力探测（第二百六十六轮）
		与 Hi-Z 同口径：计算路径可用即具备；每帧槽光源记录 SSBO 在启用时惰性构建。
		*/
		impl->cluster_capable = impl->compute.supported();
		//分簇网格按配置钳制（第二百六十七轮；默认 16 × 9 × 24）
		impl->cluster_grid = cluster_grid_clamp(Cluster_Grid{ effective.cluster_grid[0],
			effective.cluster_grid[1], effective.cluster_grid[2] });
		impl->initialized = true;
		/*
		Hi-Z 启用（第二百五十三 / 二百五十四轮）
		配置为真时构建资源并懒加载生成管线；失败仅告警、开关保持关闭，默认路径不受影响。
		*/
		if (effective.hiz_enable && !self().hiz_enable_set(true))
			logger.warn("渲染器:: Hi-Z 启用失败（开关保持关闭）");
		/*
		Cluster 光照启用（第二百六十六轮）
		配置为真时构建每帧槽光源记录 SSBO；失败仅告警、开关保持关闭。
		*/
		if (effective.cluster_enable && !self().cluster_enable_set(true))
			logger.warn("渲染器:: Cluster 光照启用失败（开关保持关闭）");
		/*
		⑳ 阴影资源（第五十二轮；第五十四轮边长可配）
		阴影目标（颜色存打包深度，边长取 impl->shadow_size）＋ 阴影管线（与场景管线共享管线布局，
		光源空间矩阵经推送常量下发）；并把阴影图写入各帧槽场景采样集的 binding 1。
		默认关闭，故不改变既有画面。
		*/
		{
			/*
			① 阴影目标（边长取配置值，颜色存打包深度）
			颜色格式固定 R8G8B8A8_UNORM（第五十四轮修正）：交换链优先 sRGB，若沿用其格式，
			打包深度的字节会被 sRGB 编码 / 解码破坏，导致片元读回深度失真并误判为全遮蔽（自阴影全黑）。
			*/
			//阴影目标固定单采样（第九十六轮；与阴影管线一致，不随 MSAA 变化）
			if (!impl->render_target.target_create(impl->shadow_target, impl->shadow_size, impl->shadow_size,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				logger.error("渲染器:: 阴影目标创建失败");
				self().shutdown();
				return false;
			}
			//② 把阴影图写入各帧槽场景采样集的 binding 1（管线在首次开启阴影时懒登记）
			VkImage shadow_image = VK_NULL_HANDLE;
			VkImageView shadow_view = VK_NULL_HANDLE;
			VkImageView shadow_depth_view = VK_NULL_HANDLE;
			VkExtent2D shadow_extent{};
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			if (!impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view,
				shadow_depth_view, shadow_extent)
				|| !impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
			{
				logger.error("渲染器:: 阴影图绑定失败：资源查询失败");
				self().shutdown();
				return false;
			}
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
				//把阴影图写入该帧槽场景采样集的 binding 1
				if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, shadow_view))
				{
					logger.error("渲染器:: 阴影图绑定失败（帧槽 {}）", slot);
					self().shutdown();
					return false;
				}
			/*
			③ 聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张，按需建立）
			瓦片 0 恒建（与旧单盏行为一致）；瓦片 1..3 仅在 spot_shadow_all 首次开启时按需创建，
			避免未使用多盏时的显存与初始化开销。
			*/
			for (std::size_t tile = 0; tile < 1; ++tile)
			{
				if (!impl->render_target.target_create(impl->spot_shadow_targets[tile], impl->shadow_size, impl->shadow_size,
					VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
				{
					logger.error("渲染器:: 聚光灯阴影目标创建失败（瓦片 {}）", tile);
					self().shutdown();
					return false;
				}
				VkImage spot_shadow_image = VK_NULL_HANDLE;
				VkImageView spot_shadow_view = VK_NULL_HANDLE;
				VkImageView spot_shadow_depth_view = VK_NULL_HANDLE;
				VkExtent2D spot_shadow_extent{};
				if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_shadow_image, spot_shadow_view,
					spot_shadow_depth_view, spot_shadow_extent))
				{
					logger.error("渲染器:: 聚光灯阴影目标查询失败（瓦片 {}）", tile);
					self().shutdown();
					return false;
				}
				for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					//把该瓦片阴影图写入帧槽场景采样集 binding 9 的对应数组元素
					if (!impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, spot_shadow_view,
						static_cast<uint32_t>(tile)))
					{
						logger.error("渲染器:: 聚光灯阴影图绑定失败（帧槽 {}，瓦片 {}）", slot, tile);
						self().shutdown();
						return false;
					}
			}
		}
		logger.info("渲染器:: 初始化完成（校验层 {}）", impl->device.validation_get() ? "开" : "关");
		return true;
	}
}
