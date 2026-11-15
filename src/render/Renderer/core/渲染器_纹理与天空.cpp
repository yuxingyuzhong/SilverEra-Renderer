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

namespace engine
{
	//从文件加载纹理（解码由纹理模块的 stb 路径承担）
	bool Render_Resource::texture_load_file(uint64_t& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 从文件加载纹理失败：尚未初始化");
			return false;
		}
		//转交纹理模块解码并创建资源
		return state().texture.texture_create_from_file(handle_out, path);
	}

	//纹理加载
	bool Render_Resource::texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块创建
		return state().texture.texture_create_from_memory(handle_out, pixels, width, height);
	}

	//程序生成棋盘格纹理
	bool Render_Resource::texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 棋盘格纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块生成
		return state().texture.texture_create_checkerboard(handle_out, width, height, cell_size);
	}

	//程序生成恒等 3D LUT（第一百四十四轮；2D 条带布局）
	bool Render_Resource::texture_load_lut_identity(uint64_t& handle_out, uint32_t edge)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 恒等查找表加载失败：尚未初始化");
			return false;
		}
		//每轴纹素数须在 [2, 64]
		if (edge < 2 || edge > 64)
		{
			logger.warn("渲染器:: 恒等查找表边长非法（{}），忽略", edge);
			return false;
		}
		//条带尺寸（宽 = 边长²、高 = 边长）
		const uint32_t strip_width = edge * edge;
		const uint32_t strip_height = edge;
		std::vector<uint8_t> pixels(static_cast<std::size_t>(strip_width) * strip_height * 4);
		//每级线性值（0..1）与对应的 sRGB 存储值（纹理格式为 SRGB，采样时自动解码回线性）
		std::vector<uint8_t> 编码表(edge);
		for (uint32_t 级 = 0; 级 < edge; ++级)
		{
			//线性值（恒等映射的自变量）
			float 线性 = static_cast<float>(级) * (edge > 1 ? 1.0f / static_cast<float>(edge - 1) : 0.0f);
			//线性 → sRGB 编码（与交换链 / 纹理的 SRGB 格式配套）
			float 编码 = 线性 <= 0.0031308f ? 线性 * 12.92f
				: 1.055f * std::pow(线性, 1.0f / 2.4f) - 0.055f;
			编码表[级] = static_cast<uint8_t>(编码 * 255.0f + 0.5f);
		}
		for (uint32_t 层 = 0; 层 < edge; ++层)
		{
			for (uint32_t 绿 = 0; 绿 < edge; ++绿)
			{
				for (uint32_t 红 = 0; 红 < edge; ++红)
				{
					//条带坐标（X = 层 × 边长 ＋ 红、Y = 绿）
					std::size_t 偏移 = (static_cast<std::size_t>(绿) * strip_width + 层 * edge + 红) * 4;
					pixels[偏移] = 编码表[红];
					pixels[偏移 + 1] = 编码表[绿];
					pixels[偏移 + 2] = 编码表[层];
					pixels[偏移 + 3] = 255;
				}
			}
		}
		//转交纹理模块上传
		return state().texture.texture_create_from_memory(handle_out, pixels.data(), strip_width, strip_height);
	}

	//从内存像素创建立方体贴图（第一百二十五轮；供天空盒与简易 IBL 使用）
	bool Render_Resource::texture_cube_load(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 立方体贴图加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块创建
		return state().texture.texture_create_cube(handle_out, six_faces, width, height);
	}

	//天空盒 / 简易 IBL 开关（第一百二十五轮）
	void Render_Resource::sky_enable_set(bool enabled)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().sky_enabled = enabled;
	}

	//当前是否启用天空盒
	bool Render_Resource::sky_enabled(void) const
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		return state().sky_enabled;
	}

	//设置天空盒强度（负值按 0 处理）
	void Render_Resource::sky_params_set(float ambient, float specular)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().sky_ambient = ambient > 0.0f ? ambient : 0.0f;
		state().sky_specular = specular > 0.0f ? specular : 0.0f;
	}

	//读取天空盒强度（出参可为空）
	void Render_Resource::sky_params_get(float* ambient_out, float* specular_out) const
	{
		//未初始化时返回默认值（均为 0）
		float ambient = owner->impl != nullptr && owner->impl->initialized ? state().sky_ambient : 0.0f;
		float specular = owner->impl != nullptr && owner->impl->initialized ? state().sky_specular : 0.0f;
		if (ambient_out != nullptr)
			*ambient_out = ambient;
		if (specular_out != nullptr)
			*specular_out = specular;
	}

	//绑定天空盒立方体贴图（0 = 内置纯白立方体）
	void Render_Resource::sky_texture_set(uint64_t texture_handle)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		//目标立方体（0 → 内置纯白立方体）
		uint64_t target = texture_handle != 0 ? texture_handle : state().default_sky_texture;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!state().texture.texture_bind_get(target, view, sampler))
		{
			logger.error("渲染器:: 天空盒贴图绑定失败：立方体贴图句柄 {} 无效", texture_handle);
			return;
		}
		//先等尽在途帧，避免重写仍被在途命令缓冲引用的描述符集
		if (!owner->impl->frame.frames_wait())
			return;
		for (uint32_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
			//把立方体贴图写入该帧槽场景采样集的 binding 5
			if (!owner->impl->pipeline.sky_bind(slot, sampler, view))
			{
				logger.error("渲染器:: 天空盒贴图绑定失败（帧槽 {}）", slot);
				return;
			}
		state().sky_texture = texture_handle;
	}

	//当前天空盒立方体贴图句柄（0 = 内置纯白立方体）
	uint64_t Render_Resource::sky_texture(void) const
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().sky_texture;
	}

	//卸载纹理
	void Render_Resource::texture_unload(uint64_t handle)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		//摘出纹理图像与其视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Texture_Record record{};
		if (!state().texture.texture_detach(handle, record))
			return;
		//若卸载的是当前绑定视图，则回落内置默认白纹理（避免采样器状态切换时写入已失效视图）
		if (record.view == state().bound_view)
			texture_bind_set(0);
		//若卸载的是当前天空盒贴图，则回落内置纯白立方体（避免 binding 5 指向已失效视图）
		if (handle != 0 && handle == state().sky_texture)
			sky_texture_set(0);
		//组装延迟条目（图像与视图经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.image = record.image;
		resource.image_allocation = record.allocation;
		resource.view = record.view;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		owner->impl->frame.deferred_push(std::move(entry), owner->impl->frame.in_flight_count_get() + 1);
	}

	//读回纹理第 0 级像素
	bool Render_Resource::texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽，避免与该纹理的采样冲突
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（内存模块同步读回）
		return state().texture.texture_readback(handle, pixels, capacity, width_out, height_out);
	}

	//读回纹理第 0 级的指定子区域
	bool Render_Resource::texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（子区域同步读回）
		return state().texture.texture_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//更新纹理第 0 级的子区域（第一百五十一轮）
	bool Render_Resource::texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍在采样中的图像）
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（子区域同步上传）
		return state().texture.texture_update_region(handle, x, y, width, height, pixels);
	}

	//更新纹理指定级的子区域（第一百五十八轮）
	bool Render_Resource::texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍在采样中的图像）
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（分级子区域同步上传）
		return state().texture.texture_update_region_level(handle, level, x, y, width, height, pixels);
	}

	//读回纹理指定级的子区域（第一百六十六轮）
	bool Render_Resource::texture_readback_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（该级子区域同步读回）
		return state().texture.texture_readback_region_level(handle, level, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//读回纹理指定级整幅像素（第一百五十八轮）
	bool Render_Resource::texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零（未初始化 / 等帧失败时也须满足文档口径）
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//先把在途帧等尽
		if (!owner->impl->frame.frames_wait())
			return false;
		//转交纹理模块（该级整幅同步读回）
		return state().texture.texture_readback_level(handle, level, pixels, capacity, width_out, height_out);
	}

	//查询纹理 mip 级数（第一百六十二轮）
	bool Render_Resource::texture_mip_levels(uint64_t handle, uint32_t* mip_levels_out) const
	{
		//出参先归零
		if (mip_levels_out != nullptr)
			*mip_levels_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//转交纹理模块
		uint32_t levels = 0;
		if (!state().texture.texture_mip_levels_get(handle, levels))
			return false;
		if (mip_levels_out != nullptr)
			*mip_levels_out = levels;
		return true;
	}

	//查询纹理指定级尺寸（第一百六十一轮）
	bool Render_Resource::texture_size_level(uint64_t handle, uint32_t level, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//转交纹理模块（按级换算尺寸）
		return state().texture.texture_size_level(handle, level, width_out, height_out);
	}

	//查询纹理尺寸
	bool Render_Resource::texture_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//转交纹理模块取尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		if (!state().texture.texture_size_get(handle, width, height))
			return false;
		if (width_out != nullptr)
			*width_out = width;
		if (height_out != nullptr)
			*height_out = height;
		return true;
	}

	//设置当前绘制所用纹理
	void Render_Resource::texture_bind_set(uint64_t texture_handle)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		//句柄为零时回落内置默认白纹理
		uint64_t target = texture_handle == 0 ? state().default_texture : texture_handle;
		//取目标纹理的视图与采样器
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!state().texture.texture_bind_get(target, view, sampler))
		{
			//目标无效则回落默认白纹理
			target = state().default_texture;
			if (!state().texture.texture_bind_get(target, view, sampler))
			{
				logger.error("渲染器:: 纹理绑定失败：默认白纹理不可用");
				return;
			}
		}
		//记录当前绑定视图与采样器（逐绘制项采样集按此快照写入）
		state().bound_view = view;
		state().bound_sampler = sampler;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		owner->impl->frame.frames_wait();
		//把目标纹理写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
			owner->impl->pipeline.texture_bind(slot, sampler, view);
		/*
		材质贴图回落（第一百二十四轮；第一百七十轮合并为一次批量写）
		单独切换反照率时把 set1 binding 3 / 4 / 6 / 7 / 8 复位为内置默认白纹理，
		避免上一材质的各类贴图残留；材质路径随后会再写自己的贴图。
		*/
		{
			VkImageView white_view = VK_NULL_HANDLE;
			VkSampler white_sampler = VK_NULL_HANDLE;
			if (state().texture.texture_bind_get(state().default_texture, white_view, white_sampler))
			{
				for (uint32_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
					owner->impl->pipeline.material_textures_bind(slot, white_sampler,
						white_view, white_view, white_view, white_view, white_view);
				//材质贴图状态同步回落白纹理（逐绘制项采样集按该快照写入）
				for (VkImageView& material_view : state().material_views)
					material_view = white_view;
			}
		}
	}

	//设置纹理采样器状态（切换后重新绑定当前纹理，使新采样器真正生效）
	void Render_Resource::texture_sampler_state_set(const Texture_Sampler_State& sampler_state)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return;
		/*
		按设备能力归一各向异性
		设备创建时未启用 samplerAnisotropy 特性时，采样器请求各向异性会创建失败，
		故此处先归一：不支持则置假并告警，倍数钳到 [1, 设备上限]。
		*/
		Texture_Sampler_State effective = sampler_state;
		//请求各向异性时按设备能力调整
		if (effective.anisotropy)
		{
			//若设备不支持该特性
			if (!owner->impl->device.anisotropy_enabled_get())
			{
				logger.warn("渲染器:: 设备不支持采样器各向异性，本次请求已忽略");
				effective.anisotropy = false;
				effective.max_anisotropy = 1.0f;
			}
			else
			{
				//倍数钳到 [1, 设备上限]
				float limit = owner->impl->device.max_anisotropy_get();
				if (effective.max_anisotropy > limit)
					effective.max_anisotropy = limit;
				if (effective.max_anisotropy < 1.0f)
					effective.max_anisotropy = 1.0f;
			}
		}
		/*
		LOD 偏置归一（第七十七轮；第九十七轮改按设备上限钳制）
		非数归 0；再钳到设备极限 `maxSamplerLodBias`（绝对值）以内，避免创建采样器时校验报错。
		*/
		if (!(effective.lod_bias == effective.lod_bias))
			effective.lod_bias = 0.0f;
		float lod_bias_limit = owner->impl->device.max_sampler_lod_bias_get();
		if (effective.lod_bias < -lod_bias_limit)
			effective.lod_bias = -lod_bias_limit;
		if (effective.lod_bias > lod_bias_limit)
			effective.lod_bias = lod_bias_limit;
		/*
		切换共享采样器状态并以新采样器重写各帧槽 set1
		采样器被烘焙进各帧槽 set1，故切换后须重写 set1，否则采样仍按旧参数进行；
		切换会销毁并重建共享采样器，故先等全部在途帧完成，避免销毁仍被在途描述符集引用的旧采样器。
		重写以「当前绑定视图」为采样源（纹理与渲染目标共用该语义），未绑过任何源时回落内置默认白纹理。
		*/
		owner->impl->frame.frames_wait();
		state().texture.sampler_state_set(effective);
		//取重建后的共享采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!state().texture.texture_bind_get(state().default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 采样器状态切换失败：共享采样器不可用");
			return;
		}
		//当前绑定视图为空则回落内置默认白纹理视图
		VkImageView view = state().bound_view != VK_NULL_HANDLE ? state().bound_view : default_view;
		//记录当前绑定视图与重建后的共享采样器（逐绘制项集按此快照写入）
		state().bound_view = view;
		state().bound_sampler = sampler;
		//以新采样器重写全部帧槽 set1（binding 0）
		for (uint32_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
			owner->impl->pipeline.texture_bind(slot, sampler, view);
		/*
		同步重写阴影采样器（set1 binding 1；第九十七轮）
		旧共享采样器已销毁，若 binding 1 仍指向旧句柄，阴影绘制会触发「采样器已销毁」校验错误；
		故在切换后把阴影图以新采样器重新写入各帧槽。
		*/
		if (owner->impl->shadow_target != 0)
		{
			VkImage shadow_image = VK_NULL_HANDLE;
			VkImageView shadow_view = VK_NULL_HANDLE;
			VkImageView shadow_depth_view = VK_NULL_HANDLE;
			VkExtent2D shadow_extent{};
			if (owner->impl->resource_domain->state().render_target.target_get(owner->impl->shadow_target, shadow_image, shadow_view, shadow_depth_view, shadow_extent))
				for (uint32_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
					owner->impl->pipeline.shadow_bind(slot, sampler, shadow_view);
		}
	}

	//读取当前采样器状态（只读诊断；未初始化返回默认状态）
	Texture_Sampler_State Render_Resource::texture_sampler_state_get(void) const
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
			return Texture_Sampler_State{};
		return state().texture.sampler_state_get();
	}

}
