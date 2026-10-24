#include "src/render/Renderer/局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::nothrow
#include <new>
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//游戏引擎命名空间
namespace engine
{
	namespace
	{
		//列主序 4×4 矩阵相乘（out = a × b；out 可与 a / b 不同）
		void matrix4_multiply(const float a[16], const float b[16], float out[16])
		{
			for (std::size_t column = 0; column < 4; ++column)
				for (std::size_t row = 0; row < 4; ++row)
				{
					//累加「a 的第 row 行」与「b 的第 column 列」的点积
					float sum = 0.0f;
					for (std::size_t k = 0; k < 4; ++k)
						sum += a[k * 4 + row] * b[column * 4 + k];
					out[column * 4 + row] = sum;
				}
		}

		/*
		阴影输入签名（第五十五轮）
		把光源空间矩阵、本帧实例矩阵（全部 float 位模式）、投影项数与阴影图边长混入 64 位 FNV-1a；
		用于判定阴影图能否跨帧复用（完全一致才跳过重绘）。
		*/
		uint64_t shadow_signature_build(const float light_view_proj[16], const float* instances,
			std::size_t instance_count, std::size_t shadow_item_count, uint32_t shadow_size)
		{
			//FNV-1a 偏移基
			uint64_t hash = 1469598103934665603ull;
			//逐 float 混入位模式
			auto mix_float = [&hash](float value)
			{
				uint32_t bits = 0;
				std::memcpy(&bits, &value, sizeof(bits));
				hash ^= static_cast<uint64_t>(bits);
				hash *= 1099511628211ull;
			};
			//光源空间矩阵
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj[index]);
			//实例矩阵（每项 16 个 float）
			for (std::size_t index = 0; index < instance_count; ++index)
				mix_float(instances[index]);
			//投影项数与阴影图边长
			hash ^= static_cast<uint64_t>(shadow_item_count);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(shadow_size);
			hash *= 1099511628211ull;
			return hash;
		}

		/*
		视锥剔除（保守口径；第二十二轮新增）
		把局部包围盒 8 个角经「model × view_proj」变换到裁剪空间，逐个测 6 个裁剪面；
		**仅当 8 个角同时落在同一个面外**时判为「完全不可见」（返回真）；
		任何相交 / 跨越近平面一律保留（不做背面与遮挡剔除）。
		*/
		bool frustum_cull_aabb(const float view_proj[16], const float model[16], const float bounds_min[3], const float bounds_max[3])
		{
			//组合矩阵（列主序）
			float mvp[16] = {};
			matrix4_multiply(view_proj, model, mvp);
			//六个裁剪面的「全部角点在外」标志（左 / 右 / 下 / 上 / 近 / 远）
			bool outside[6] = { true, true, true, true, true, true };
			for (std::size_t corner = 0; corner < 8; ++corner)
			{
				//角点局部坐标（按位取上下界）
				float x = (corner & 1) ? bounds_max[0] : bounds_min[0];
				float y = (corner & 2) ? bounds_max[1] : bounds_min[1];
				float z = (corner & 4) ? bounds_max[2] : bounds_min[2];
				//变换到裁剪空间
				float clip_x = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
				float clip_y = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
				float clip_z = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
				float clip_w = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
				//逐面判「该角是否在外」
				if (!(clip_x > clip_w))
					outside[0] = false;
				if (!(clip_x < -clip_w))
					outside[1] = false;
				if (!(clip_y > clip_w))
					outside[2] = false;
				if (!(clip_y < -clip_w))
					outside[3] = false;
				if (!(clip_z < 0.0f))
					outside[4] = false;
				if (!(clip_z > clip_w))
					outside[5] = false;
			}
			//任一面「全部角点在外」即视为完全不可见
			for (std::size_t plane = 0; plane < 6; ++plane)
				if (outside[plane])
					return true;
			return false;
		}

		//单位矩阵（列主序）
		const float IDENTITY_MATRIX4[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};

		//白色（RGBA；第六十九轮：未设置逐物体色调时的默认推送值，乘到反照率上画面不变）
		const float WHITE_COLOR_RGBA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	//渲染层版本查询
	string renderer_version(void)
	{
		//渲染层版本号
		return "0.1.0";
	}

	//重建交换链并把旧资源压入帧延迟销毁队列
	bool Renderer::Impl::swapchain_rebuild_deferred(uint32_t width, uint32_t height)
	{
		//旧资源（重建成功后移交延迟队列）
		Swapchain_Old_Resources old_resources{};
		//若重建失败则不移交任何资源（交换链已回滚为旧链，仍可用）
		if (!swapchain.rebuild(width, height, old_resources))
			return false;
		//组装延迟条目并压入帧模块（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放旧资源）
		Deferred_Entry entry{};
		entry.resources = std::move(old_resources);
		frame.deferred_push(std::move(entry), frame.in_flight_count_get() + 1);
		return true;
	}

	//确保后处理全屏四边形网格就绪（惰性创建；单级路径与链路径共用）
	bool Renderer::Impl::post_mesh_ensure(void)
	{
		//全屏四边形网格尚未创建
		if (fullscreen_mesh == 0)
		{
			//全屏四边形（NDC 范围 -1..1；绕序取 (0,2,1)/(0,3,2) 以匹配「不做 Y 翻转」的顶点输出）
			Mesh_Data data{};
			data.vertices = {
				{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
				{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
				{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
				{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
			};
			data.indices = { 0, 2, 1, 0, 3, 2 };
			//网格句柄（创建失败则整体失败）
			Mesh_Handle created = 0;
			if (!mesh.mesh_create(created, data))
			{
				logger.error("渲染器:: 后处理全屏网格创建失败");
				return false;
			}
			fullscreen_mesh = created;
		}
		return true;
	}

	//确保内置单级后处理具名管线就绪（惰性登记；仅既有单级默认路径使用）
	bool Renderer::Impl::post_pipeline_ensure(void)
	{
		//后处理具名管线尚未登记
		if (post_pipeline == 0)
		{
			//加载后处理顶点 / 片元着色器模块
			VkShaderModule vert_module = VK_NULL_HANDLE;
			VkShaderModule frag_module = VK_NULL_HANDLE;
			if (!shader.module_load(vert_module, "后处理.vert.spv"))
			{
				logger.error("渲染器:: 后处理顶点着色器加载失败");
				return false;
			}
			if (!shader.module_load(frag_module, "后处理.frag.spv"))
			{
				logger.error("渲染器:: 后处理片元着色器加载失败");
				shader.module_destroy(vert_module);
				return false;
			}
			//登记具名管线（不透明档；与默认管线共享同一布局与描述符集布局）
			uint64_t registered = 0;
			if (!pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque))
			{
				logger.error("渲染器:: 后处理管线登记失败");
				shader.module_destroy(vert_module);
				shader.module_destroy(frag_module);
				return false;
			}
			//记录句柄与其着色器模块（模块所有权在本实现体，随 extra_pipelines 一并销毁）
			post_pipeline = registered;
			extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
			logger.info("渲染器:: 后处理管线已登记（句柄 {}）", registered);
		}
		return true;
	}

	//确保既有单级默认后处理所需资源就绪（默认后处理管线 ＋ 全屏网格）
	bool Renderer::Impl::post_resources_ensure(void)
	{
		//先确保默认后处理管线，再确保全屏网格
		if (!post_pipeline_ensure())
			return false;
		return post_mesh_ensure();
	}

	//确保 SSAA 降采样具名管线就绪（惰性登记）
	bool Renderer::Impl::ssaa_pipeline_ensure(void)
	{
		//降采样具名管线尚未登记
		if (ssaa_pipeline == 0)
		{
			//加载后处理顶点 / 降采样片元着色器模块
			VkShaderModule vert_module = VK_NULL_HANDLE;
			VkShaderModule frag_module = VK_NULL_HANDLE;
			if (!shader.module_load(vert_module, "后处理.vert.spv"))
			{
				logger.error("渲染器:: SSAA 降采样顶点着色器加载失败");
				return false;
			}
			if (!shader.module_load(frag_module, "降采样.frag.spv"))
			{
				logger.error("渲染器:: SSAA 降采样片元着色器加载失败");
				shader.module_destroy(vert_module);
				return false;
			}
			//登记具名管线（不透明档；与默认管线共享同一布局与描述符集布局）
			uint64_t registered = 0;
			if (!pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque))
			{
				logger.error("渲染器:: SSAA 降采样管线登记失败");
				shader.module_destroy(vert_module);
				shader.module_destroy(frag_module);
				return false;
			}
			//记录句柄与其着色器模块（模块所有权在本实现体，随 extra_pipelines 一并销毁）
			ssaa_pipeline = registered;
			extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
			logger.info("渲染器:: SSAA 降采样管线已登记（句柄 {}）", registered);
		}
		return true;
	}

	//销毁 SSAA 超采样目标（关闭时调用）
	void Renderer::Impl::ssaa_targets_destroy(void)
	{
		//逐个销毁已建立的目标（句柄无效时内部忽略）
		if (ssaa_scene_target != 0)
			render_target.target_destroy(ssaa_scene_target);
		if (ssaa_resolve_target != 0)
			render_target.target_destroy(ssaa_resolve_target);
		//句柄与建立参数一并清零
		ssaa_scene_target = 0;
		ssaa_resolve_target = 0;
		ssaa_extent_width = 0;
		ssaa_extent_height = 0;
		ssaa_target_factor = 0;
	}

	//确保 SSAA 超采样目标就绪（场景目标 = 输出 × 倍率；解析目标 = 输出尺寸）
	bool Renderer::Impl::ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor)
	{
		//尺寸无效或该尺寸已失败过则直接返回（避免逐帧重复尝试）
		if (width == 0 || height == 0)
			return false;
		if (ssaa_extent_failed)
			return false;
		//尺寸与倍率均未变化且目标齐备则复用
		if (ssaa_scene_target != 0 && ssaa_resolve_target != 0
			&& ssaa_extent_width == width && ssaa_extent_height == height && ssaa_target_factor == factor)
			return true;
		//参数变化：等尽在途帧后销毁旧目标（避免释放仍在使用的附件）
		if (ssaa_scene_target != 0 || ssaa_resolve_target != 0)
		{
			frame.frames_wait();
			ssaa_targets_destroy();
		}
		//按交换链格式创建场景目标（N 倍）与解析目标（输出尺寸）
		uint64_t scene = 0;
		uint64_t resolve = 0;
		if (!render_target.target_create(scene, width * factor, height * factor,
			swapchain.format_get(), swapchain.depth_format_get()))
		{
			logger.warn("渲染器:: SSAA 场景目标创建失败（{}×{} ×{}），本帧回落", width, height, factor);
			ssaa_extent_failed = true;
			return false;
		}
		if (!render_target.target_create(resolve, width, height,
			swapchain.format_get(), swapchain.depth_format_get()))
		{
			logger.warn("渲染器:: SSAA 解析目标创建失败（{}×{}），本帧回落", width, height);
			render_target.target_destroy(scene);
			ssaa_extent_failed = true;
			return false;
		}
		//记录句柄与建立参数
		ssaa_scene_target = scene;
		ssaa_resolve_target = resolve;
		ssaa_extent_width = width;
		ssaa_extent_height = height;
		ssaa_target_factor = factor;
		logger.info("渲染器:: SSAA 目标已建立（{}×{} ×{}）", width, height, factor);
		return true;
	}

	//构造（不创建任何 GPU 资源）
	Renderer::Renderer(void)
		: impl(new(std::nothrow) Impl())
	{
	}

	//析构（内部调用 shutdown，保证资源不泄漏）
	Renderer::~Renderer(void)
	{
		shutdown();
	}

	//初始化
	bool Renderer::init(const Renderer_Config& config)
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
			shutdown();
			return false;
		}
		//② 平台：实例扩展清单
		vector<const char*> instance_extensions;
		if (!impl->platform.instance_extensions_get(instance_extensions))
		{
			logger.error("渲染器:: 实例扩展清单获取失败");
			shutdown();
			return false;
		}
		//③ 设备：实例与调试信使
		if (!impl->device.build_instance(instance_extensions, effective.validation))
		{
			logger.error("渲染器:: 实例创建失败");
			shutdown();
			return false;
		}
		//④ 平台：表面（必须先于设备创建，供呈现队列族查询）
		if (!impl->platform.surface_build(impl->device.instance_get()))
		{
			logger.error("渲染器:: 表面创建失败");
			shutdown();
			return false;
		}
		//⑤ 设备：物理设备与逻辑设备
		if (!impl->device.build_device(impl->platform.surface_get()))
		{
			logger.error("渲染器:: 逻辑设备创建失败");
			shutdown();
			return false;
		}
		//⑥ 内存：VMA 分配器
		if (!impl->memory.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get()))
		{
			logger.error("渲染器:: 内存初始化失败");
			shutdown();
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
		//⑦ 交换链（深度图像依赖内存模块，故传入 memory）
		if (!impl->swapchain.build(impl->device.physical_device_get(), impl->memory, impl->device.device_get(),
			impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
			swapchain_width, swapchain_height, effective.vsync))
		{
			logger.error("渲染器:: 交换链创建失败");
			shutdown();
			return false;
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
		if (!impl->frame.build(impl->device.device_get(), impl->device.physical_device_get(),
			impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->device.present_family_get(), impl->device.present_queue_get(),
			impl->swapchain, impl->memory, effective.frame_overlap))
		{
			logger.error("渲染器:: 帧基础设施创建失败");
			shutdown();
			return false;
		}
		//⑨ 着色器：记录逻辑设备与着色器目录
		if (!impl->shader.build(impl->device.device_get()))
		{
			logger.error("渲染器:: 着色器模块初始化失败");
			shutdown();
			return false;
		}
		impl->shader.directory_set(effective.shader_dir);
		//⑩ 加载顶点 / 片元着色器模块
		if (!impl->shader.module_load(impl->vert_module, "场景.vert.spv"))
		{
			logger.error("渲染器:: 顶点着色器加载失败");
			shutdown();
			return false;
		}
		if (!impl->shader.module_load(impl->frag_module, "场景.frag.spv"))
		{
			logger.error("渲染器:: 片元着色器加载失败");
			shutdown();
			return false;
		}
		//⑪ 管线：描述符布局 / 管线布局 / 描述符池与每帧槽描述符集
		{
			Pipeline_Config pipeline_config{};
			pipeline_config.color_format = impl->swapchain.format_get();
			pipeline_config.depth_format = impl->swapchain.depth_format_get();
			pipeline_config.frame_overlap = effective.frame_overlap;
			if (!impl->pipeline.build(impl->device.device_get(), pipeline_config))
			{
				logger.error("渲染器:: 管线基础设施创建失败");
				shutdown();
				return false;
			}
		}
		//⑫ 管线：图形管线（初始档位 = 不透明）
		if (!impl->pipeline.pipeline_build(impl->vert_module, impl->frag_module, Blend_Mode::Opaque))
		{
			logger.error("渲染器:: 图形管线创建失败");
			shutdown();
			return false;
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
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途；第六十四轮由 960 扩为 976 字节：追加 PBR-lite 参数 vec4）
			if (!impl->memory.buffer_create(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], 976,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 场景 UBO 创建失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始视图 × 投影矩阵（列主序 16 浮点）
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], view_proj, 64, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				shutdown();
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
				shutdown();
				return false;
			}
			//写入初始点光源数组（offset 128、256 字节；8 个槽位，未用槽位清零；第六十二轮扩为 8 盏）
			float point_block[64] = {};
			for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
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
				shutdown();
				return false;
			}
			/*
			写入初始聚光参数（offset 384、256 字节；第三十四轮；第六十二轮扩为 8 盏）
			前 128 字节为 8 个「方向 ＋ 有效标志」，后 128 字节为 8 组「内锥余弦 ＋ 外锥余弦」；
			两个 std140 数组各自连续，故分两段排布。
			*/
			float spot_block[64] = {};
			for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
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
				shutdown();
				return false;
			}
			/*
			写入初始雾参数（offset 640、32 字节；第三十八轮）
			前 16 字节 = 雾色 rgb ＋ 密度 w；后 16 字节 = 起始距离 x ＋ 结束距离 y。
			*/
			float fog_block[8] = {
				impl->fog_color[0], impl->fog_color[1], impl->fog_color[2], impl->fog_density,
				impl->fog_start, impl->fog_end, 0.0f, 0.0f,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], fog_block, 32, 640))
			{
				logger.error("渲染器:: 场景雾参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始环境光颜色（offset 672、16 字节；第四十轮）
			float ambient_block[4] = {
				impl->ambient_color[0], impl->ambient_color[1], impl->ambient_color[2], 0.0f,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], ambient_block, 16, 672))
			{
				logger.error("渲染器:: 场景环境光颜色写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始半球环境光（offset 688、16 字节；第四十七轮）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				impl->ambient_ground, 16, 688))
			{
				logger.error("渲染器:: 场景半球环境光写入失败（帧槽 {}）", slot);
				shutdown();
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
				shutdown();
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
				shutdown();
				return false;
			}
			//写入初始逐盏光泽度（offset 736、128 字节；第五十一轮；第六十二轮扩为 8 盏，std140 每盏占 16 字节）
			float shininess_block[32] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shininess_block, 128, 736))
			{
				logger.error("渲染器:: 场景逐盏光泽度写入失败（帧槽 {}）", slot);
				shutdown();
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
				shutdown();
				return false;
			}
			//写入初始 PBR-lite 参数（offset 960、16 字节；第六十四轮：默认关闭、粗糙度缩放 1）
			float pbr_init_block[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				pbr_init_block, 16, 960))
			{
				logger.error("渲染器:: 场景 PBR 参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//把该帧槽 set0 绑定到对应 UBO
			if (!impl->pipeline.scene_bind(slot, impl->scene_ubos[slot]))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//创建该帧槽的实例缓冲（主机可见、顶点缓冲用途；容量 = instance_capacity 个实例，一个实例 80 字节）
			if (!impl->memory.buffer_create(impl->instance_buffers[slot], impl->instance_allocations[slot],
				static_cast<VkDeviceSize>(impl->instance_capacity) * INSTANCE_STRIDE_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 实例缓冲创建失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
		}
		/*
		⑭ 纹理：记录内存与设备，生成内置默认白纹理（2×2 全白）并绑定到所有帧槽 set1，
		保证 descriptor_set_count = 2 时不存在未写入的描述符集。
		*/
		if (!impl->texture.build(impl->memory, impl->device.device_get()))
		{
			logger.error("渲染器:: 纹理模块初始化失败");
			shutdown();
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
			shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			shutdown();
			return false;
		}
		//把默认纹理写入所有帧槽 set1（长命资源，一次写满）
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			if (!impl->pipeline.texture_bind(slot, default_sampler, default_view))
			{
				logger.error("渲染器:: 默认纹理绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
		}
		//⑮ 网格：记录内存模块
		if (!impl->mesh.build(impl->memory))
		{
			logger.error("渲染器:: 网格模块初始化失败");
			shutdown();
			return false;
		}
		//⑯ 渲染目标：记录设备 / 图形队列与内存模块（离屏目标按需创建）
		if (!impl->render_target.build(impl->device.device_get(), impl->device.graphics_family_get(),
			impl->device.graphics_queue_get(), impl->memory))
		{
			logger.error("渲染器:: 渲染目标模块初始化失败");
			shutdown();
			return false;
		}
		//⑰ 字体：记录纹理模块（字形图集经纹理模块上传与销毁）
		if (!impl->font.build(impl->texture))
		{
			logger.error("渲染器:: 字体模块初始化失败");
			shutdown();
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
				//登记失败保持 0
				if (!self->pipeline_load(handle, "文字.vert.spv", "文字.frag.spv", Blend_Mode::Alpha))
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
		绘制回调由上层经 ui_channel() 另行设置（渲染层不代管 UI）。
		*/
		impl->ui_channel.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->platform.window_get(), effective.frame_overlap, impl->swapchain.image_count_get(),
		impl->swapchain.format_get(), impl->swapchain.depth_format_get());
		/*
		把事件泵的 SDL 事件桥接到 UI 通道
		上层经 ui_channel()->event_callback_set 注册回调后，事件泵每帧逐个事件转交，
		供上层平台后端（imgui_impl_sdl3）处理键鼠与文本输入；未注册时该桥接为空操作。
		桥接不持有资源；解除见 shutdown。
		*/
		impl->platform.event_forward_set(
			[](const SDL_Event* event, void* user_data) {
				static_cast<Ui_Channel_Context*>(user_data)->event_dispatch(event);
			},
			&impl->ui_channel);

		//记录配置并标记初始化完成
		impl->config = effective;
		//SSAA 倍率生效（钳制 1..4；默认 1 保持既有画面逐字节不变）
		impl->ssaa_factor = effective.ssaa_factor < 1 ? 1 : (effective.ssaa_factor > 4 ? 4 : effective.ssaa_factor);
		impl->ssaa_extent_failed = false;
		impl->initialized = true;
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
			if (!impl->render_target.target_create(impl->shadow_target, impl->shadow_size, impl->shadow_size,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get()))
			{
				logger.error("渲染器:: 阴影目标创建失败");
				shutdown();
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
				shutdown();
				return false;
			}
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
				//把阴影图写入该帧槽场景采样集的 binding 1
				if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, shadow_view))
				{
					logger.error("渲染器:: 阴影图绑定失败（帧槽 {}）", slot);
					shutdown();
					return false;
				}
		}
		logger.info("渲染器:: 初始化完成（校验层 {}）", impl->device.validation_get() ? "开" : "关");
		return true;
	}

	//关闭（幂等）
	void Renderer::shutdown(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return;
		//严格逆序销毁（各模块 destroy 幂等，可安全处理半初始化状态；§5.4 / §9）
		/*
		阴影资源（第五十二轮）
		须先于渲染目标与管线模块销毁：阴影管线为门面具名管线（着色器模块所有权在门面）。
		*/
		if (impl->initialized)
		{
			//归还阴影管线
			if (impl->shadow_pipeline != 0)
			{
				pipeline_unload(impl->shadow_pipeline);
				impl->shadow_pipeline = 0;
			}
			//归还阴影目标
			if (impl->shadow_target != 0)
			{
				render_target_destroy(impl->shadow_target);
				impl->shadow_target = 0;
			}
			//阴影图已失效（第五十五轮）
			impl->shadow_pass_ready = false;
			impl->shadow_signature = 0;
			impl->shadow_pass_count = 0;
		}
		impl->frame.destroy();
		//文本网格缓存依赖网格模块，须先归还网格（含管线句柄与文字颜色复位）
		impl->text.shutdown();
		//网格缓冲依赖内存模块，须先于 memory 销毁
		impl->mesh.destroy();
		//字体图集纹理依赖纹理模块，须先归还
		impl->font.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		impl->texture.destroy();
		//渲染目标依赖设备与内存，须先于两者销毁
		impl->render_target.destroy();
		impl->active_target = 0;
		//复位同帧后处理呈现路径与其资源句柄（全屏网格随网格模块销毁、后处理管线随管线模块销毁）
		impl->present_target = 0;
		impl->present_extra.clear();
		impl->post_chain.clear();
		impl->post_pipeline = 0;
		impl->fullscreen_mesh = 0;
		//复位 SSAA 状态（超采样目标已随渲染目标模块销毁，仅清句柄、倍率与失败标记）
		impl->ssaa_scene_target = 0;
		impl->ssaa_resolve_target = 0;
		impl->ssaa_extent_width = 0;
		impl->ssaa_extent_height = 0;
		impl->ssaa_target_factor = 0;
		impl->ssaa_pipeline = 0;
		impl->ssaa_factor = 1;
		impl->ssaa_extent_failed = false;
		//复位逐级泛光参数覆盖与剔除观测
		for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
			impl->post_stage_param_valid[stage] = false;
		impl->culled_items = 0;
		//剔除开关恢复默认（开启）
		impl->cull_enabled = true;
		//逐物体 PBR 材质恢复默认（不覆盖全局，第六十八轮）
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
		//逐物体色调恢复默认（白色，第六十九轮）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
		//光照恢复默认（满照：朝 -Z、白色、强度 1、环境光 0）
		impl->light_direction[0] = 0.0f;
		impl->light_direction[1] = 0.0f;
		impl->light_direction[2] = -1.0f;
		impl->light_color[0] = 1.0f;
		impl->light_color[1] = 1.0f;
		impl->light_color[2] = 1.0f;
		impl->light_intensity = 1.0f;
		impl->light_ambient = 0.0f;
		//点光源与高光恢复默认（无点光源、方向光强度 1、高光关闭、光泽度 16）
		impl->point_light_count = 0;
		impl->specular_intensity = 0.0f;
		impl->specular_shininess = 16.0f;
		//复位距离衰减模式（第三十六轮：默认线性）
		impl->attenuation_mode = 0;
		//复位距离雾（第三十八轮：密度 0 = 关闭）
		impl->fog_color[0] = 0.05f;
		impl->fog_color[1] = 0.05f;
		impl->fog_color[2] = 0.10f;
		impl->fog_density = 0.0f;
		impl->fog_start = 1.0f;
		impl->fog_end = 10.0f;
		//复位环境光颜色（第四十轮：默认白）
		impl->ambient_color[0] = 1.0f;
		impl->ambient_color[1] = 1.0f;
		impl->ambient_color[2] = 1.0f;
		//半球环境光恢复默认（地面白、强度 0 = 关闭）
		impl->ambient_ground[0] = 1.0f;
		impl->ambient_ground[1] = 1.0f;
		impl->ambient_ground[2] = 1.0f;
		impl->ambient_ground[3] = 0.0f;
		//补光恢复默认（朝 -Z、白、强度 0 = 关闭）
		impl->fill_direction[0] = 0.0f;
		impl->fill_direction[1] = 0.0f;
		impl->fill_direction[2] = -1.0f;
		impl->fill_color[0] = 1.0f;
		impl->fill_color[1] = 1.0f;
		impl->fill_color[2] = 1.0f;
		impl->fill_intensity = 0.0f;
		//逐光高光缩放恢复默认（第五十轮：均为 1）
		impl->directional_specular = 1.0f;
		impl->fill_specular = 1.0f;
		impl->pending_push_constants.clear();
		//场景 UBO 依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->scene_ubos.size(); ++slot)
				//归还场景 UBO 与其内存
				impl->memory.buffer_destroy(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot]);
		impl->scene_ubos.clear();
		impl->scene_ubo_allocs.clear();
		//实例缓冲依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->instance_buffers.size(); ++slot)
				//归还实例缓冲与其内存
				impl->memory.buffer_destroy(impl->instance_buffers[slot], impl->instance_allocations[slot]);
		impl->instance_buffers.clear();
		impl->instance_allocations.clear();
		//默认纹理句柄复位
		impl->default_texture = 0;
		//管线依赖设备与内存，须先于两者销毁
		impl->pipeline.destroy();
		//着色器模块须先于着色器上下文复位
		if (impl->vert_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->vert_module);
			impl->vert_module = VK_NULL_HANDLE;
		}
		if (impl->frag_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->frag_module);
			impl->frag_module = VK_NULL_HANDLE;
		}
		//具名管线的着色器模块（须先于着色器上下文复位）
		for (auto& record : impl->extra_pipelines)
		{
			//回收该管线的顶点着色器模块
			if (record.vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.vert_module);
			//回收该管线的片元着色器模块
			if (record.frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.frag_module);
		}
		impl->extra_pipelines.clear();
		//材质记录与按档缓存的共享具名管线清空（具名管线已随 extra_pipelines 释放）
		impl->materials.clear();
		impl->blend_pipelines.clear();
		impl->bound_pipeline_request = 0;
		impl->shader.destroy();
		//交换链销毁连带销毁深度图像，须排在管线之后
		impl->swapchain.destroy();
		impl->memory.destroy();
		/*
		先平台后设备
		平台持有「借用」的实例句柄，销毁表面（vkDestroySurfaceKHR）要求实例仍在途；
		设备销毁会连带 vkDestroyInstance，故必须排在平台之后。
		*/
		//解除事件泵到 UI 通道的桥接（平台销毁后不应再有回调指向 UI 通道）
		impl->platform.event_forward_clear();
		impl->platform.destroy();
		impl->device.destroy();
		//清空待绘制项与其推送常量存储
		impl->pending_items.clear();
		impl->pending_instances.clear();
		//复位状态
		impl->ui_channel.destroy();
		impl->frame_count = 0;
		impl->initialized = false;
	}

	//是否已初始化
	bool Renderer::valid(void) const
	{
		return impl != nullptr && impl->initialized;
	}

	//每帧推进（可选像素回读）
	bool Renderer::frame_render(uint8_t* readback_pixels, uint32_t readback_capacity,
		uint32_t* readback_width, uint32_t* readback_height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//回读尺寸出参先复位（未兑现时保持零，便于调用方判据）
		if (readback_width != nullptr)
			*readback_width = 0;
		if (readback_height != nullptr)
			*readback_height = 0;

		//① 泵事件
		impl->platform.events_pump();
		/*
		② 若窗口请求退出则交调用方收敛
		③ 若窗口最小化则挂起本帧（避免 0×0 交换链；K7）
		两种挂起情形都不消费待绘制项，故一并清空，避免连续挂起期间无限累积。
		*/
		if (impl->platform.close_requested() || impl->platform.minimized())
		{
			impl->pending_items.clear();
			impl->pending_instances.clear();
			return true;
		}

		//取当前窗口客户区尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		impl->platform.window_size_get(width, height);

		//本帧是否已重建交换链（K8：每帧最多一次）
		bool rebuilt = false;
		//④ 若窗口尺寸已变化则重建交换链
		if (impl->platform.resized_consume())
		{
			//尺寸为零则不重建
			if (width != 0 && height != 0)
			{
				//重建交换链并把旧资源压入帧延迟队列；失败仅告警，本帧继续
				if (!impl->swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（本帧继续）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(), impl->swapchain.depth_format_get()))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
				rebuilt = true;
			}
		}

		//交换链尺寸可能已变化，刷新相机宽高比
		{
			VkExtent2D extent = impl->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				impl->camera.aspect_set(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		/*
		⑤a 光源空间矩阵（第五十二轮）
		无论阴影是否开启都先算好并写入 UBO，使开关切换可即时生效。
		以方向光传播方向构造正交视锥（覆盖以原点为中心、半径 12 的场景）；
		投影与相机同口径做 Y 翻转；方向与世界上方平行时改用 X 轴作上向量，避免 lookAt 退化。
		*/
		{
			glm::vec3 light_dir(impl->light_direction[0], impl->light_direction[1], impl->light_direction[2]);
			//方向退化时取默认传播方向
			if (glm::dot(light_dir, light_dir) < 1e-12f)
				light_dir = glm::vec3(0.0f, 0.0f, -1.0f);
			light_dir = glm::normalize(light_dir);
			//上向量与光向平行时改用 X 轴
			glm::vec3 up = glm::abs(light_dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			//光源机位位于场景中心的「逆光向」一侧
			glm::vec3 eye = -light_dir * 20.0f;
			glm::mat4 light_view = glm::lookAt(eye, glm::vec3(0.0f), up);
			glm::mat4 light_proj = glm::ortho(-12.0f, 12.0f, -12.0f, 12.0f, 1.0f, 60.0f);
			light_proj[1][1] *= -1.0f;
			glm::mat4 light_vp = light_proj * light_view;
			std::memcpy(impl->light_view_proj, glm::value_ptr(light_vp), sizeof(float) * 16);
		}
		/*
		⑤b 阴影 pass（第五十二轮；第五十五轮按需重绘）
		以光源视角把「参与投影的几何」渲染进阴影目标（RGBA8 打包深度）；
		该 pass 走独立的离屏提交（推进帧槽），故必须在取主 pass 帧槽之前完成；
		随后等尽在途帧，保证主 pass 采样时阴影图已写完。
		若本帧输入签名与上一帧完全一致则跳过整个 pass（阴影图原样复用），静态场景省一次离屏提交与等待。
		*/
		if (impl->shadow_enabled && impl->shadow_pipeline != 0 && impl->shadow_target != 0
			&& !impl->pending_items.empty())
		{
			//本帧参与投影的项数
			std::size_t shadow_item_count = 0;
			for (const auto& item : impl->pending_items)
			{
				//跳过文字等非投影项
				if (item.cast_shadow)
					++shadow_item_count;
			}
			//本帧阴影输入签名（实例矩阵按 float 个数计；实例存储为连续 20 浮点数组）
			const float* instance_floats = impl->pending_instances.empty()
				? nullptr : impl->pending_instances.front().data();
			uint64_t shadow_signature = shadow_signature_build(impl->light_view_proj,
				instance_floats, impl->pending_instances.size() * INSTANCE_FLOAT_COUNT,
				shadow_item_count, impl->shadow_size);
			//输入与上一帧完全一致且阴影图可用 → 跳过重绘
			if (shadow_item_count != 0 && impl->shadow_pass_ready && impl->shadow_signature == shadow_signature)
			{
				//阴影图复用，主 pass 直接采样
			}
			else if (shadow_item_count != 0)
			{
				//阴影 pass 使用的帧槽（render_to_target 内部同样推进帧计数）
				uint32_t shadow_slot = impl->frame.slot_index_get();
				//等待该槽上一轮提交完成，再写实例缓冲
				if (!impl->frame.slot_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待帧槽失败");
					return false;
				}
				//写入该槽实例缓冲（与主 pass 使用同一批模型矩阵）
				if (!impl->pending_instances.empty())
				{
					VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[shadow_slot],
						impl->instance_allocations[shadow_slot], impl->pending_instances.data(), instance_bytes, 0))
					{
						logger.error("渲染器:: 阴影 pass 实例缓冲写入失败");
						return false;
					}
					/*
					使该帧槽的实例缓存失效（第六十一轮）
					阴影 pass 直接整块写入该槽缓冲，绕过了按需写入的缓存；
					主 pass 之后若复用该槽，须以完整写入重建缓存，避免按旧缓存跳过必要更新。
					*/
					if (impl->instance_block_cached.size() > shadow_slot)
						impl->instance_block_cached[shadow_slot] = 0;
					//累计阴影 pass 的实例上传字节
					impl->instance_upload_bytes += static_cast<uint64_t>(instance_bytes);
				}
				//组装阴影绘制项（仅投影几何；改用阴影管线并以推送常量下发光源空间矩阵）
				std::vector<Frame_Render_Item> shadow_items;
				shadow_items.reserve(shadow_item_count);
				VkPipeline shadow_pipeline = impl->pipeline.pipeline_by_handle_get(impl->shadow_pipeline);
				VkPipelineLayout shadow_layout = impl->pipeline.layout_get();
				for (const auto& item : impl->pending_items)
				{
					//文字等非投影项跳过
					if (!item.cast_shadow)
						continue;
					Frame_Render_Item copy = item;
					copy.pipeline = shadow_pipeline;
					copy.layout = shadow_layout;
					/*
					实例缓冲指向阴影 pass 刚写入的槽
					pending_items 中的 instance_buffer 恒为空（主 pass 稍后才填装），
					若此处不指定，顶点属性 binding 1（实例模型矩阵）无缓冲，实例矩阵退化会让
					所有几何塌到原点、阴影图只剩一点，表现为整幅画面全遮蔽。
					*/
					copy.instance_buffer = impl->instance_buffers[shadow_slot];
					copy.scene_set = VK_NULL_HANDLE;
					copy.sampler_set = VK_NULL_HANDLE;
					copy.descriptor_set_count = 0;
					copy.push_constants = impl->light_view_proj;
					copy.push_constant_size = 64;
					copy.push_constant_offset = 0;
					shadow_items.push_back(copy);
				}
				//阴影目标附件
				Frame_Render_Target shadow_target{};
				if (!impl->render_target.target_get(impl->shadow_target, shadow_target.color_image,
					shadow_target.color_view, shadow_target.depth_view, shadow_target.extent))
				{
					logger.error("渲染器:: 阴影目标附件查询失败");
					return false;
				}
				//阴影图清空为全 0（打包深度 0 = 最远）
				const float shadow_clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				if (!impl->frame.render_to_target(shadow_target, shadow_items.data(),
					static_cast<uint32_t>(shadow_items.size()), shadow_clear))
				{
					logger.error("渲染器:: 阴影 pass 提交失败");
					return false;
				}
				//等尽在途帧：确保阴影图写入完成后再由主 pass 采样
				if (!impl->frame.frames_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待完成失败");
					return false;
				}
				//记录本帧签名并标记阴影图可复用
				impl->shadow_signature = shadow_signature;
				impl->shadow_pass_ready = true;
				++impl->shadow_pass_count;
			}
		}

		/*
		⑤ 取本帧帧槽序号（与帧模块内部一致），把相机视图 × 投影写入该槽 UBO 并绑定 set0；
		再为待绘制项填装 set0 / set1 描述符集（descriptor_set_count = 2）。
		推送常量恒为列主序 16 浮点模型矩阵（mesh_draw 已写入对应的 pending_transforms 存储）。
		*/
		uint32_t slot = impl->frame.slot_index_get();
		/*
		等待本帧槽前一次提交完成
		必须在写本帧槽 UBO 之前等待，否则可能覆盖 GPU 仍在读取的上一轮 UBO（真实竞态）；
		帧模块内部还会再等一次，届时栅栏已发出，vkWaitForFences 立即返回，无副作用。
		*/
		if (!impl->frame.slot_wait())
		{
			logger.error("渲染器:: 等待本帧帧槽失败");
			return false;
		}
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		//推进文字子系统帧时钟并归还已淘汰且无在途引用的网格（第四十八轮）
		impl->text.frame_begin(impl->config.frame_overlap);
		/*
		场景 UBO 整块组装（976 字节 = 244 浮点；第六十二轮 960、第六十四轮追加 PBR-lite 参数）
		各段浮点偏移与 std140 契约一致：0 视图投影、16 光照块、32 点光源位置、64 点光源颜色、
		96 聚光方向、128 聚光锥参数、160 雾、168 环境光颜色、172 半球环境光、176 补光方向与强度、
		180 补光颜色、184 逐盏光泽度（8 盏 × 4）、216 光源空间矩阵、232 阴影参数、236 阴影强度、
		240 PBR-lite 参数（x 启用 / y 粗糙度缩放）。
		*/
		float scene_block[244] = {};
		//视图 × 投影矩阵（列主序 16 浮点）
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[index] = view_proj[index];
		//光照公共块：环境光 / 高光强度 / 光泽度 / 点光源数量
		scene_block[16] = impl->light_ambient;
		scene_block[17] = impl->specular_intensity;
		scene_block[18] = impl->specular_shininess;
		scene_block[19] = static_cast<float>(impl->point_light_count);
		//方向光方向 / 强度 / 颜色 / 衰减模式
		scene_block[20] = impl->light_direction[0];
		scene_block[21] = impl->light_direction[1];
		scene_block[22] = impl->light_direction[2];
		scene_block[23] = impl->light_intensity;
		scene_block[24] = impl->light_color[0];
		scene_block[25] = impl->light_color[1];
		scene_block[26] = impl->light_color[2];
		scene_block[27] = static_cast<float>(impl->attenuation_mode);
		//相机世界位置（镜面高光的视线方向来源）；w = 方向光逐光高光缩放（第五十轮）
		float camera_position[3] = {};
		impl->camera.position_get(camera_position);
		scene_block[28] = camera_position[0];
		scene_block[29] = camera_position[1];
		scene_block[30] = camera_position[2];
		scene_block[31] = impl->directional_specular;
		//点光源：先排全部「位置 ＋ 半径」，再排全部「颜色 ＋ 强度」（与着色器两个 std140 数组一致）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
		{
			const std::size_t base = 32 + static_cast<std::size_t>(light) * 4;
			scene_block[base + 0] = impl->point_lights[light].position[0];
			scene_block[base + 1] = impl->point_lights[light].position[1];
			scene_block[base + 2] = impl->point_lights[light].position[2];
			scene_block[base + 3] = impl->point_lights[light].radius;
			//颜色段起点 = 8 盏位置（32 浮点）之后
			scene_block[base + 32 + 0] = impl->point_lights[light].color[0];
			scene_block[base + 32 + 1] = impl->point_lights[light].color[1];
			scene_block[base + 32 + 2] = impl->point_lights[light].color[2];
			scene_block[base + 32 + 3] = impl->point_lights[light].intensity;
		}
		//聚光参数：前 32 浮点为「方向 ＋ 有效标志」（8 盏），后 32 浮点为「内锥余弦 ＋ 外锥余弦 ＋ 逐光源衰减模式 ＋ 逐盏高光缩放」
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
		{
			const std::size_t base = 96 + static_cast<std::size_t>(light) * 4;
			scene_block[base + 0] = impl->point_lights[light].spot_direction[0];
			scene_block[base + 1] = impl->point_lights[light].spot_direction[1];
			scene_block[base + 2] = impl->point_lights[light].spot_direction[2];
			scene_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
			//锥参数段起点 = 8 盏方向（32 浮点）之后
			scene_block[base + 32 + 0] = impl->point_lights[light].spot_inner_cos;
			scene_block[base + 32 + 1] = impl->point_lights[light].spot_outer_cos;
			scene_block[base + 32 + 2] = static_cast<float>(impl->point_lights[light].attenuation);
			//w = 逐盏高光缩放（第四十九轮）
			scene_block[base + 32 + 3] = impl->point_lights[light].specular_scale;
		}
		//雾参数（雾色 ＋ 密度 ＋ 区间；第六十二轮起点后移 64 浮点）
		scene_block[160] = impl->fog_color[0];
		scene_block[161] = impl->fog_color[1];
		scene_block[162] = impl->fog_color[2];
		scene_block[163] = impl->fog_density;
		scene_block[164] = impl->fog_start;
		scene_block[165] = impl->fog_end;
		//环境光颜色（第四十轮）
		scene_block[168] = impl->ambient_color[0];
		scene_block[169] = impl->ambient_color[1];
		scene_block[170] = impl->ambient_color[2];
		//半球环境光（第四十七轮：rgb = 地面色、w = 混合强度）
		for (std::size_t index = 0; index < 4; ++index)
			scene_block[172 + index] = impl->ambient_ground[index];
		//补光方向与强度（第四十七轮）
		scene_block[176] = impl->fill_direction[0];
		scene_block[177] = impl->fill_direction[1];
		scene_block[178] = impl->fill_direction[2];
		scene_block[179] = impl->fill_intensity;
		//补光颜色（第四十七轮）＋ 逐光高光缩放（第五十轮；w）
		scene_block[180] = impl->fill_color[0];
		scene_block[181] = impl->fill_color[1];
		scene_block[182] = impl->fill_color[2];
		scene_block[183] = impl->fill_specular;
		//逐盏光泽度（第五十一轮；第六十二轮扩为 8 盏，std140 float[8] 每盏占 16 字节 → 偏移 184 + light × 4）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
			scene_block[184 + static_cast<std::size_t>(light) * 4] = impl->point_lights[light].shininess;
		/*
		光源空间矩阵沿用 ⑤a 结果（第五十二轮）
		写入 UBO 供场景片元解包比较；阴影关闭时字段保留但不参与计算。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[216 + index] = impl->light_view_proj[index];
		//阴影参数（第五十二轮：x = 启用、y = 偏置、z = 阴影图边长；第五十三轮 w = 软度）
		scene_block[232] = impl->shadow_enabled ? 1.0f : 0.0f;
		scene_block[233] = impl->shadow_bias;
		scene_block[234] = static_cast<float>(impl->shadow_size);
		scene_block[235] = impl->shadow_softness;
		//阴影强度（第五十三轮：0 = 阴影无效、1 = 全遮蔽）
		scene_block[236] = impl->shadow_strength;
		//PBR-lite 参数（第六十四轮：x = 启用、y = 粗糙度缩放；第六十五轮 z = 金属度）
		scene_block[240] = impl->pbr_enabled ? 1.0f : 0.0f;
		scene_block[241] = impl->pbr_roughness;
		scene_block[242] = impl->pbr_metallic;
		/*
		场景 UBO 按需写入（第六十轮）
		逐帧槽缓存上一次写入的 244 浮点，按 16 字节（4 浮点）单元比较；
		连续变化单元合并为一段，仅对变化区间调用 buffer_write；
		首次访问某帧槽（缓存未建立）仍整块写入 976 字节，保证内容完整。
		写入内容与整块写逐字节一致，故画面不变。
		*/
		const std::size_t scene_slot_count = impl->scene_ubos.size();
		//若缓存规模与帧槽数不一致则重建（帧槽数由在途帧数决定，运行期恒定）
		if (impl->scene_block_cache.size() != scene_slot_count * 244)
		{
			impl->scene_block_cache.assign(scene_slot_count * 244, 0.0f);
			impl->scene_block_cached.assign(scene_slot_count, 0);
		}
		//本帧槽缓存段（244 浮点）
		float* slot_cache = impl->scene_block_cache.data() + static_cast<std::size_t>(slot) * 244;
		//本帧实际上传字节数
		std::size_t uploaded_bytes = 0;
		//若该帧槽尚未建立缓存
		if (impl->scene_block_cached[slot] == 0)
		{
			//整块写入（与既有行为一致）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], scene_block, 976, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				return false;
			}
			uploaded_bytes = 976;
		}
		else
		{
			//比较单元：4 浮点 = 16 字节（std140 vec4 对齐）
			constexpr std::size_t UNIT_FLOATS = 4;
			constexpr std::size_t UNIT_BYTES = 16;
			constexpr std::size_t UNIT_COUNT = 244 / UNIT_FLOATS;
			//当前变化段的起始单元与是否处于段内
			std::size_t run_start = 0;
			bool in_run = false;
			//逐单元扫描并合并连续变化区间
			for (std::size_t unit = 0; unit < UNIT_COUNT; ++unit)
			{
				//该单元是否有任一浮点变化（非数视为变化，保证安全上传）
				bool changed = false;
				for (std::size_t offset = 0; offset < UNIT_FLOATS; ++offset)
					if (scene_block[unit * UNIT_FLOATS + offset] != slot_cache[unit * UNIT_FLOATS + offset])
						changed = true;
				//若变化段遇到未变化单元则收束并写入
				if (!changed && in_run)
				{
					const std::size_t segment_bytes = (unit - run_start) * UNIT_BYTES;
					if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
						scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
					{
						logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
					in_run = false;
				}
				//若未在段内且该单元变化则开启新段
				if (changed && !in_run)
				{
					run_start = unit;
					in_run = true;
				}
			}
			//收尾：最后一段延伸到末尾
			if (in_run)
			{
				const std::size_t segment_bytes = (UNIT_COUNT - run_start) * UNIT_BYTES;
				if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
					scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
				{
					logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes += segment_bytes;
			}
		}
		//更新该帧槽缓存（与已写入内容一致）
		for (std::size_t index = 0; index < 244; ++index)
			slot_cache[index] = scene_block[index];
		impl->scene_block_cached[slot] = 1;
		//累计上传字节（观测用）
		impl->scene_upload_bytes += uploaded_bytes;
		/*
		本帧槽 set0 无需重写
		各帧槽的场景 UBO 缓冲句柄自 init 起恒定不变，set0 已在 init 时一次性写入；
		每帧只更新 UBO 内容（上一段的 buffer_write），不重写描述符集。
		*/
		/*
		同帧多 pass 后处理准备
		若本帧设定「渲染到目标并同帧后处理呈现」，先确保后处理资源（全屏网格；单级路径另需默认后处理管线）就绪，
		并把「单位模型矩阵」追加到本帧实例存储末尾（后处理全屏四边形以此为实例缓冲来源）。
		追加须在下方「实例缓冲写入」之前完成，故此处先行。
		路径：post_chain 非空 → 链路径（须 post_mesh_ensure；链长 ≥ 2 还须第二目标）；
		      post_chain 为空 → 既有单级默认路径（post_resources_ensure）。
		*/
		bool post_frame = false;
		//本帧是否走后处理链路径（false 且 post_frame → 既有单级默认路径）
		bool post_chain_mode = false;
		//后处理绘制项在本帧实例缓冲内的字节偏移
		VkDeviceSize post_instance_offset = 0;
		//若已设定同帧后处理呈现目标
		if (impl->present_target != 0)
		{
			//本帧实例存储须仍有空位（后处理全屏四边形再占一个实例；满则回落原路径，避免实例缓冲越界写）
			if (impl->pending_instances.size() >= impl->instance_capacity)
				logger.warn("渲染器:: 本帧实例存储已满，同帧后处理呈现回落原路径");
			//链长 ≥ 2 但目标组不足 2 个则回落单级默认路径
			else if (!impl->post_chain.empty() && impl->post_chain.size() >= 2 && impl->present_extra.empty())
				logger.warn("渲染器:: 后处理链级数 ≥ 2 但中间目标不足 2 个，本帧回落单级默认后处理");
			else
			{
				//本帧是否走链路径
				bool chain_mode = !impl->post_chain.empty();
				//确保后处理资源就绪（链 → 全屏网格；单级 → 全屏网格 ＋ 默认后处理管线）
				bool resources_ready = chain_mode ? impl->post_mesh_ensure() : impl->post_resources_ensure();
				//若资源就绪
				if (resources_ready)
				{
					//追加单位矩阵（列主序；材质四浮点为零 = 不覆盖全局 PBR）并记录其字节偏移
					post_instance_offset = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					impl->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = impl->pending_instances.back();
					identity.fill(0.0f);
					identity[0] = 1.0f;
					identity[5] = 1.0f;
					identity[10] = 1.0f;
					identity[15] = 1.0f;
					post_frame = true;
					post_chain_mode = chain_mode;
				}
				//资源未就绪则本帧回落原路径
				else
					logger.warn("渲染器:: 后处理资源未就绪，本帧回落原路径");
			}
		}
		/*
		SSAA 超采样准备（倍率 > 1）
		与后处理 / 离屏路径互斥（优先级最低）：两者任一启用时本帧不走 SSAA。
		就绪后追加一个单位实例供降采样全屏四边形使用；两 pass 的实际录制在下方分派段完成。
		*/
		bool ssaa_frame = false;
		//降采样 pass 在本帧实例缓冲内的字节偏移
		VkDeviceSize ssaa_instance_offset = 0;
		//若启用 SSAA 且未走后处理 / 离屏路径
		if (impl->ssaa_factor > 1 && impl->present_target == 0 && impl->active_target == 0)
		{
			//确保降采样管线 / 全屏网格 / 默认后处理管线 / 超采样目标就绪
			if (impl->ssaa_pipeline_ensure() && impl->post_mesh_ensure() && impl->post_pipeline_ensure()
				&& impl->ssaa_targets_ensure(width, height, impl->ssaa_factor))
			{
				//本帧实例存储须仍有空位（降采样全屏四边形再占一个实例）
				if (impl->pending_instances.size() < impl->instance_capacity)
				{
					ssaa_instance_offset = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					impl->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = impl->pending_instances.back();
					identity.fill(0.0f);
					identity[0] = 1.0f;
					identity[5] = 1.0f;
					identity[10] = 1.0f;
					identity[15] = 1.0f;
					ssaa_frame = true;
				}
				//实例存储已满则回落
				else
					logger.warn("渲染器:: 本帧实例存储已满，SSAA 回落原路径");
			}
			//资源未就绪则回落原路径
			else
				logger.warn("渲染器:: SSAA 资源未就绪，本帧回落原路径");
		}
		/*
		实例模型矩阵写入本帧槽实例缓冲
		本帧登记的全部实例矩阵按登记顺序连续存放，故一次写入即可；
		随后每个渲染项只携带「帧槽缓冲句柄 ＋ 字节偏移 ＋ 实例数」。
		*/
		if (!impl->pending_instances.empty())
		{
			//本帧实例属性总字节数（一个实例 = 20 浮点 = 80 字节）
			VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
			/*
			实例缓冲按需写入（第六十一轮）
			逐帧槽缓存上次写入的实例属性，按「一个实例 = 80 字节」为比较单元；
			连续变化实例合并为一段逐段写入；首次访问该帧槽整块写入本帧全部实例。
			写入内容与整块写逐字节一致，故画面不变。
			*/
			const std::size_t cache_slot_count = impl->instance_buffers.size();
			const std::size_t cache_slot_floats = static_cast<std::size_t>(impl->instance_capacity) * INSTANCE_FLOAT_COUNT;
			//若缓存规模与帧槽 / 容量不一致则重建
			if (impl->instance_block_cache.size() != cache_slot_count * cache_slot_floats)
			{
				impl->instance_block_cache.assign(cache_slot_count * cache_slot_floats, 0.0f);
				impl->instance_block_count.assign(cache_slot_count, 0);
				impl->instance_block_cached.assign(cache_slot_count, 0);
			}
			//本帧槽缓存段
			float* instance_cache = impl->instance_block_cache.data() + static_cast<std::size_t>(slot) * cache_slot_floats;
			//上次已写入的实例数
			const uint32_t cached_count = impl->instance_block_count[slot];
			//本帧实例数
			const std::size_t instance_count = impl->pending_instances.size();
			//本帧实际上传字节数
			std::size_t uploaded_bytes = 0;
			//若该帧槽尚未建立缓存
			if (impl->instance_block_cached[slot] == 0)
			{
				//整块写入
				if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
					impl->pending_instances.data(), instance_bytes, 0))
				{
					logger.error("渲染器:: 实例缓冲写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes = static_cast<std::size_t>(instance_bytes);
			}
			else
			{
				//当前变化段的起始实例与是否处于段内
				std::size_t run_start = 0;
				bool in_run = false;
				//逐实例扫描并合并连续变化区间
				for (std::size_t index = 0; index < instance_count; ++index)
				{
					//超出上次缓存长度的实例一律视为变化
					bool changed = index >= cached_count;
					//未超出则逐浮点比较
					for (std::size_t offset = 0; !changed && offset < INSTANCE_FLOAT_COUNT; ++offset)
						if (impl->pending_instances[index][offset] != instance_cache[index * INSTANCE_FLOAT_COUNT + offset])
							changed = true;
					//若变化段遇到未变化实例则收束并写入
					if (!changed && in_run)
					{
						const std::size_t segment_bytes = (index - run_start) * INSTANCE_STRIDE_BYTES;
						if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
							impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
						{
							logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
							return false;
						}
						uploaded_bytes += segment_bytes;
						in_run = false;
					}
					//若未在段内且该实例变化则开启新段
					if (changed && !in_run)
					{
						run_start = index;
						in_run = true;
					}
				}
				//收尾：最后一段延伸到本帧实例末尾
				if (in_run)
				{
					const std::size_t segment_bytes = (instance_count - run_start) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
						impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
					{
						logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
				}
			}
			//更新该帧槽缓存（与已写入内容一致）
			for (std::size_t index = 0; index < instance_count * INSTANCE_FLOAT_COUNT; ++index)
				instance_cache[index] = impl->pending_instances[index / INSTANCE_FLOAT_COUNT][index % INSTANCE_FLOAT_COUNT];
			impl->instance_block_count[slot] = static_cast<uint32_t>(instance_count);
			impl->instance_block_cached[slot] = 1;
			//累计上传字节（观测用）
			impl->instance_upload_bytes += uploaded_bytes;
		}

		//为本帧待绘制项填装描述符集与实例缓冲句柄
		for (auto& item : impl->pending_items)
		{
			//本帧槽的实例缓冲（模型矩阵来源）
			item.instance_buffer = impl->instance_buffers[slot];
			//本帧槽的场景 UBO 描述符集（set0）
			item.scene_set = impl->pipeline.scene_set_get(slot);
			//本帧槽的采样器描述符集（set1）
			item.sampler_set = impl->pipeline.sampler_set_get(slot);
			item.descriptor_set_count = 2;
		}

		//交给帧模块走取图像 / 录制 / 提交 / 呈现（可选像素回读）
		const float* clear_color = impl->clear_color;
		bool rebuild_needed = false;
		//像素回读请求（仅当调用方给出目标缓冲时组装）
		Frame_Readback_Request readback_request{};
		Frame_Readback_Request* readback_ptr = nullptr;
		//若调用方请求回读
		if (readback_pixels != nullptr)
		{
			readback_request.pixels = readback_pixels;
			readback_request.capacity = readback_capacity;
			readback_ptr = &readback_request;
		}

		/*
		分派渲染路径
		① 同帧多 pass 后处理（present_target 有效且资源就绪）：渲染到目标并在同帧内后处理呈现到交换链
		   （post_chain_mode → N 级后处理链；否则 → 既有单级默认后处理）；
		② 离屏路径（active_target 有效）：把场景渲染进目标、不呈现到窗口；
		③ 交换链路径（默认）：取图像 → 渲染 → 呈现。
		三路共用同一批帧槽与帧计数轮转，且互斥（后处理优先于离屏，离屏优先于交换链）。
		*/
		bool offscreen = false;
		/*
		SSAA 路径：场景 → N 倍目标，降采样 → 解析目标，再默认后处理 → 交换链
		两级后处理共用同一全屏几何与场景 UBO；目标按 targets[级序 % 2] 轮转，
		故第 0 级取 N 倍场景目标、第 1 级取解析目标（与帧模块轮转规则一致）。
		*/
		if (ssaa_frame)
		{
			//两个目标：场景目标（N 倍）与解析目标（输出尺寸）
			std::vector<Frame_Render_Target> targets(2);
			//全屏四边形网格缓冲信息
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//取两个目标的附件与全屏网格
			bool ready = impl->render_target.target_get(impl->ssaa_scene_target, targets[0].color_image,
				targets[0].color_view, targets[0].depth_view, targets[0].extent)
				&& impl->render_target.target_get(impl->ssaa_resolve_target, targets[1].color_image,
					targets[1].color_view, targets[1].depth_view, targets[1].extent)
				&& impl->mesh.mesh_record_get(impl->fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//取共享采样器（用于写后处理采样集）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler shared_sampler = VK_NULL_HANDLE;
			if (ready && !impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
			{
				logger.error("渲染器:: SSAA 失败：共享采样器不可用");
				ready = false;
			}
			//若前置就绪
			if (ready)
			{
				//把两个目标写入各帧槽后处理采样集（下标 0 = 场景目标、1 = 解析目标）
				for (uint32_t each = 0; each < impl->config.frame_overlap; ++each)
				{
					impl->pipeline.post_texture_bind(each, shared_sampler, targets[0].color_view, 0);
					impl->pipeline.post_texture_bind(each, shared_sampler, targets[1].color_view, 1);
					impl->pipeline.post_scene_bind(each, shared_sampler, targets[0].color_view, 0);
					impl->pipeline.post_scene_bind(each, shared_sampler, targets[0].color_view, 1);
				}
				//两级管线：0 = 降采样（N 倍 → 输出），1 = 默认后处理（输出 → 交换链）
				uint64_t stage_pipelines[2] = { impl->ssaa_pipeline, impl->post_pipeline };
				std::vector<Frame_Render_Item> post_items;
				bool items_ready = true;
				for (uint32_t index = 0; index < 2; ++index)
				{
					//本级图形管线（失效即整帧回落）
					VkPipeline stage_pipeline = impl->pipeline.pipeline_by_handle_get(stage_pipelines[index]);
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项（全屏四边形 ＋ 本级管线 ＋ set0 场景 UBO / set1 对应源采样集）
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = impl->pipeline.layout_get();
					post_item.scene_set = impl->pipeline.scene_set_get(slot);
					post_item.sampler_set = impl->pipeline.post_set_get(slot, index);
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = impl->instance_buffers[slot];
					post_item.instance_offset = ssaa_instance_offset;
					post_item.instance_count = 1;
					post_items.push_back(post_item);
				}
				//若两级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ 降采样 ＋ 默认后处理 ＋ 呈现
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(), 2,
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr))
					{
						logger.error("渲染器:: SSAA 帧渲染失败");
						return false;
					}
				}
				//含失效管线则回落原路径
				else
					logger.warn("渲染器:: SSAA 管线失效，本帧回落原路径");
			}
			//前置不完整则回落原路径
			else
				logger.warn("渲染器:: SSAA 前置不完整，本帧回落原路径");
		}
		//若走同帧多 pass 后处理路径
		if (!offscreen && post_frame)
		{
			/*
			中间目标组
			首项为场景目标（present_target），其余为额外中间目标（present_extra）；
			链上第 i 级取 targets[i % 目标数]，故链长 ≥ 2 时目标数必须 ≥ 2（上方已保证）。
			*/
			std::vector<Frame_Render_Target> targets(1 + impl->present_extra.size());
			//全屏四边形网格的共用缓冲 / 索引区偏移 / 索引数
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//场景目标与全屏网格均就绪才可走该路径
			bool ready = impl->render_target.target_get(impl->present_target, targets[0].color_image,
				targets[0].color_view, targets[0].depth_view, targets[0].extent)
				&& impl->mesh.mesh_record_get(impl->fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//逐个取额外中间目标附件
			for (std::size_t each = 1; ready && each < targets.size(); ++each)
				ready = impl->render_target.target_get(impl->present_extra[each - 1], targets[each].color_image,
					targets[each].color_view, targets[each].depth_view, targets[each].extent);
			/*
			目标尺寸预检（第十七轮放宽）
			后处理片元按「源 / 宿尺寸」推送做像素映射，故目标**不再要求等于交换链范围**；
			仅要求每个目标尺寸非零（零尺寸无法建立视口）。
			*/
			if (ready)
			{
				//逐目标检查尺寸非零
				for (std::size_t each = 0; each < targets.size(); ++each)
				{
					//若该目标尺寸为零
					if (targets[each].extent.width == 0 || targets[each].extent.height == 0)
					{
						logger.warn("渲染器:: 第 {} 个后处理目标尺寸为零，本帧回落原路径", each);
						ready = false;
						break;
					}
				}
			}
			//取共享采样器（把「各级源」写入后处理采样集）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler shared_sampler = VK_NULL_HANDLE;
			if (ready && !impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
			{
				logger.error("渲染器:: 后处理失败：共享采样器不可用");
				ready = false;
			}
			//若前置就绪
			if (ready)
			{
				/*
				把各目标图像写入各帧槽后处理采样器集（下标 = 目标组内序号；长命资源，一次写满全部槽）
				binding 0 = 该级源；binding 1 恒写目标组首项（场景目标），供双源片元（泛光叠加）采样原画面。
				*/
				for (uint32_t each = 0; each < impl->config.frame_overlap; ++each)
					for (std::size_t index = 0; index < targets.size(); ++index)
					{
						impl->pipeline.post_texture_bind(each, shared_sampler, targets[index].color_view,
							static_cast<uint32_t>(index));
						impl->pipeline.post_scene_bind(each, shared_sampler, targets[0].color_view,
							static_cast<uint32_t>(index));
					}
				//各级后处理管线句柄序列（链路径取链；单级路径取默认后处理管线）
				std::vector<uint64_t> stage_pipelines;
				//链路径取注册链
				if (post_chain_mode)
					stage_pipelines = impl->post_chain;
				//单级路径取懒登记的默认后处理管线
				else
					stage_pipelines.push_back(impl->post_pipeline);
				//组装各级后处理绘制项（全屏四边形 ＋ 各级管线 ＋ set0 场景 UBO / set1 对应源采样集）
				std::vector<Frame_Render_Item> post_items;
				//任一级管线缺失即整帧回落
				bool items_ready = true;
				for (std::size_t index = 0; index < stage_pipelines.size(); ++index)
				{
					//本级图形管线（链设置后被卸载则为空）
					VkPipeline stage_pipeline = impl->pipeline.pipeline_by_handle_get(stage_pipelines[index]);
					//若本级管线已失效
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = impl->pipeline.layout_get();
					post_item.scene_set = impl->pipeline.scene_set_get(slot);
					//本级源采样集（下标 = 级序 % 目标数，与帧模块的目标轮转一致）
					post_item.sampler_set = impl->pipeline.post_set_get(slot,
						static_cast<uint32_t>(index % targets.size()));
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = impl->instance_buffers[slot];
					post_item.instance_offset = post_instance_offset;
					post_item.instance_count = 1;
					//泛光参数（帧模块随「源 / 宿尺寸」一并推送，offset 96）
					if (index < Impl::POST_CHAIN_MAX_STAGES && impl->post_stage_param_valid[index])
					{
						//该级有专设参数则覆盖全局值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = impl->post_stage_param[index][each];
					}
					else
					{
						//否则用全局泛光参数
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = impl->bloom_param[each];
					}
					post_items.push_back(post_item);
				}
				//若各级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ N 级后处理 pass（目标轮转）＋ 呈现（单级路径即本函数 N = 1 的特例）
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(),
						static_cast<uint32_t>(targets.size()),
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr))
					{
						logger.error("渲染器:: 同帧后处理链渲染失败");
						return false;
					}
				}
				//含失效管线则回落原路径
				else
					logger.warn("渲染器:: 后处理链含已失效管线，回落原路径");
			}
			//前置不完整则回落原路径
			else
				logger.warn("渲染器:: 同帧多 pass 后处理前置不完整（目标或全屏网格或采样器），回落原路径");
		}
		//若已设定本帧渲染目标（离屏路径）
		if (!offscreen && impl->active_target != 0)
		{
			//取目标的颜色图像 / 颜色视图 / 深度视图与范围
			Frame_Render_Target target{};
			if (impl->render_target.target_get(impl->active_target, target.color_image, target.color_view,
				target.depth_view, target.extent))
			{
				offscreen = true;
				//渲染到离屏目标（不取交换链图像、不呈现）
				if (!impl->frame.render_to_target(target, impl->pending_items.data(),
					static_cast<uint32_t>(impl->pending_items.size()), clear_color,
					impl->ui_channel.draw_callback_get(), readback_ptr))
				{
					logger.error("渲染器:: 离屏帧渲染失败");
					return false;
				}
			}
			//句柄无效则回落交换链路径
			else
				logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", impl->active_target);
		}
		//若走交换链路径
		if (!offscreen)
		{
			if (!impl->frame.render(impl->swapchain, impl->pending_items.data(),
				static_cast<uint32_t>(impl->pending_items.size()), rebuild_needed, clear_color,
				impl->ui_channel.draw_callback_get(), readback_ptr))
			{
				logger.error("渲染器:: 帧渲染失败");
				return false;
			}
		}
		//消费完毕，清空待绘制项与其推送常量存储（下一帧由 mesh_draw 重新登记）
		impl->pending_items.clear();
		impl->pending_instances.clear();
		impl->pending_push_constants.clear();
		/*
		回读结果交付
		若本帧请求回读但帧模块未产出像素（如窗口最小化 / 关闭请求而跳过渲染，
		或交换链过期提前返回），视为本次回读未兑现，报错返回假并保持尺寸出参为零。
		*/
		if (readback_ptr != nullptr)
		{
			//若帧模块未回读任何像素
			if (readback_request.bytes == 0)
			{
				logger.error("渲染器:: 本帧未产出像素，像素回读未兑现");
				return false;
			}
			//回填回读尺寸
			if (readback_width != nullptr)
				*readback_width = readback_request.width;
			if (readback_height != nullptr)
				*readback_height = readback_request.height;
		}
		//若帧链路要求重建且本帧尚未重建
		if (rebuild_needed && !rebuilt)
		{
			//尺寸有效时重建交换链
			if (width != 0 && height != 0)
			{
				//重建交换链并把旧资源压入帧延迟队列（失败仅告警，下一帧再试）
				if (!impl->swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（下一帧再试）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(), impl->swapchain.depth_format_get()))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
			}
		}

		//⑥ 帧计数自增
		++impl->frame_count;
		return true;
	}

	//窗口是否请求退出
	bool Renderer::close_requested(void) const
	{
		//若实现体未分配则视为未请求退出
		if (impl == nullptr)
			return false;
		return impl->platform.close_requested();
	}

	//已呈现帧数
	uint64_t Renderer::frame_count(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->frame_count;
	}

	//取帧统计快照
	Frame_Stats Renderer::frame_stats_get(void) const
	{
		//默认全零
		Frame_Stats stats{};
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return stats;
		//已推进帧数取自门面自身计数（与 frame_count() 同口径）
		stats.frames = impl->frame_count;
		//帧耗时与绘制 / 实例计数取自帧模块（主机侧录制统计）
		stats.frame_ms = impl->frame.last_frame_ms_get();
		stats.draw_items = impl->frame.total_draw_items_get();
		stats.instances = impl->frame.total_instances_get();
		//GPU 侧耗时取自帧模块时间戳查询池（设备不支持时恒 0）
		stats.gpu_frame_ms = impl->frame.last_gpu_frame_ms_get();
		//实际绘制量（不含被跳过的不完整项；与登记量 draw_items / instances 对照可见差异）
		stats.drawn_items = impl->frame.total_drawn_items_get();
		stats.drawn_instances = impl->frame.total_drawn_instances_get();
		//累计口径：主机侧耗时之和、GPU 侧成功读回之和与样本数
		stats.frame_total_ms = impl->frame.total_frame_ms_get();
		stats.gpu_total_ms = impl->frame.total_gpu_frame_ms_get();
		stats.gpu_samples = impl->frame.gpu_sample_count_get();
		//最近一帧实际执行的后处理级数（0 = 未走后处理链）
		stats.post_stages = impl->frame.last_post_stages_get();
		//累计因视锥剔除而未登记的项数
		stats.culled_items = impl->culled_items;
		return stats;
	}

	//是否支持 GPU 时间戳
	bool Renderer::gpu_timestamp_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转调帧模块能力标志
		return impl->frame.gpu_timestamp_supported_get();
	}

	//是否支持像素回读
	bool Renderer::readback_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//交换链图像具备传输源用法才可回读
		return impl->swapchain.color_transfer_src_get();
	}

	//设置超采样抗锯齿倍率（SSAA）
	void Renderer::ssaa_factor_set(uint32_t factor)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//钳制到 1..4（0 视为关闭）
		uint32_t clamped = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
		//同值重复设置为空操作
		if (clamped == impl->ssaa_factor)
			return;
		impl->ssaa_factor = clamped;
		//倍率变化：清失败标记，下一次 frame_render 按需重建目标
		impl->ssaa_extent_failed = false;
		logger.info("渲染器:: SSAA 倍率已设为 {}（下一帧生效）", clamped);
	}

	//当前超采样倍率
	uint32_t Renderer::ssaa_factor(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return impl->ssaa_factor;
	}

	//是否支持采样器各向异性过滤
	bool Renderer::sampler_anisotropy_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->device.anisotropy_enabled_get();
	}

	//采样器最大各向异性倍数（设备上限）
	float Renderer::sampler_max_anisotropy(void) const
	{
		//若尚未初始化则无能力可言
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->device.max_anisotropy_get();
	}

	//当前交换链宽度
	uint32_t Renderer::extent_width(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().width;
	}

	//当前交换链高度
	uint32_t Renderer::extent_height(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().height;
	}

	//请求改变窗口客户区尺寸
	bool Renderer::window_size_set(uint32_t width, uint32_t height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 窗口尺寸设置失败：尚未初始化");
			return false;
		}
		//转交平台层（成功即置「交换链待重建」标志，重建在下一次 frame_render 内完成）
		return impl->platform.window_size_set(width, height);
	}

	//按程序化球体加载网格（第六十六轮新增）
	bool Renderer::mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks)
	{
		//前置：已初始化、半径为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f)
			|| slices < 3 || slices > 128 || stacks < 2 || stacks > 64)
		{
			logger.warn("渲染器:: 球体参数非法（半径 {}、经度 {}、纬度 {}）", radius, slices, stacks);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::sphere_generate(radius, slices, stacks));
	}

	//按程序化圆柱加载网格（第六十六轮新增）
	bool Renderer::mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆柱参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cylinder_generate(radius, height, segments));
	}

	//按程序化胶囊加载网格（第六十九轮新增）
	bool Renderer::mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
		uint32_t segments, uint32_t rings)
	{
		//前置：已初始化、半径为正、中段高度非负（非数亦拒）、分段与纬圈在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height >= 0.0f)
			|| segments < 3 || segments > 128 || rings < 2 || rings > 64)
		{
			logger.warn("渲染器:: 胶囊参数非法（半径 {}、高度 {}、分段 {}、纬圈 {}）",
				radius, height, segments, rings);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::capsule_generate(radius, height, segments, rings));
	}

	//按程序化圆锥加载网格（第六十八轮新增）
	bool Renderer::mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆锥参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cone_generate(radius, height, segments));
	}

	//按程序化圆环加载网格（第六十七轮新增）
	bool Renderer::mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
		uint32_t major_segments, uint32_t minor_segments)
	{
		//前置：已初始化、双半径为正（非数亦拒）、双分段在区间内
		if (impl == nullptr || !impl->initialized || !(major_radius > 0.0f) || !(minor_radius > 0.0f)
			|| major_segments < 3 || major_segments > 128 || minor_segments < 3 || minor_segments > 128)
		{
			logger.warn("渲染器:: 圆环参数非法（环半径 {}、管半径 {}、环分段 {}、管分段 {}）",
				major_radius, minor_radius, major_segments, minor_segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::torus_generate(major_radius, minor_radius,
			major_segments, minor_segments));
	}

	bool Renderer::mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格加载失败：尚未初始化");
			return false;
		}
		//转交网格模块创建顶点 / 索引缓冲
		return impl->mesh.mesh_create(handle_out, data);
	}

	//卸载网格
	void Renderer::mesh_unload(Mesh_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//摘出网格缓冲（不立即销毁，避免销毁仍被在途命令缓冲引用的缓冲）
		Mesh_Record record{};
		if (!impl->mesh.mesh_detach(handle, record))
			return;
		//组装延迟条目（缓冲经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.buffer = record.buffer;
		resource.buffer_allocation = record.allocation;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//把一批模型矩阵追加入待绘制实例存储（列主序 16 浮点 / 个，材质 4 浮点 / 个），返回首个实例的字节偏移
	//（文件内辅助函数；容量是否足够由调用方先行校验，此处不重复检查）
	static VkDeviceSize instance_matrices_append(std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& store,
		const float* transforms, uint32_t count, bool material_active, float roughness, float metallic)
	{
		//首个实例的字节偏移（追加前的元素个数 × 80）
		VkDeviceSize offset = static_cast<VkDeviceSize>(store.size()) * INSTANCE_STRIDE_BYTES;
		//逐实例追加
		for (uint32_t instance = 0; instance < count; ++instance)
		{
			//新实例的矩阵存储
			store.emplace_back();
			std::array<float, INSTANCE_FLOAT_COUNT>& target = store.back();
			//transforms 为空则整批用单位矩阵
			if (transforms == nullptr)
			{
				//单位矩阵（列主序）
				const float identity[16] = {
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				};
				for (uint32_t element = 0; element < 16; ++element)
					target[element] = identity[element];
			}
			else
			{
				//拷贝第 instance 个外部模型矩阵
				for (uint32_t element = 0; element < 16; ++element)
					target[element] = transforms[static_cast<size_t>(instance) * 16 + element];
			}
			/*
			逐物体材质（第六十八轮）
			覆盖标志置于第 2 分量（z）：0 = 不覆盖，片元着色器回落场景 UBO 的全局 PBR 参数；
			故未调用 material_pbr_set 的绘制项与旧版逐字节一致。
			*/
			target[INSTANCE_MATERIAL_OFFSET + 0] = material_active ? roughness : 0.0f;
			target[INSTANCE_MATERIAL_OFFSET + 1] = material_active ? metallic : 0.0f;
			target[INSTANCE_MATERIAL_OFFSET + 2] = material_active ? 1.0f : 0.0f;
			target[INSTANCE_MATERIAL_OFFSET + 3] = 0.0f;
		}
		return offset;
	}

	//绘制网格
	bool Renderer::mesh_draw(Mesh_Handle handle, const float* transform, const float* color)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格绘制失败：尚未初始化");
			return false;
		}

		//取网格共用缓冲、索引区偏移与索引数
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!impl->mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 网格绘制失败：网格句柄 {} 无效", handle);
			return false;
		}

		/*
		实例级视锥剔除（第二十二轮新增）
		完全在视锥外的绘制项**不登记**（合法跳过，不是错误）；返回真表示已剔除。
		保守口径：包围盒与视锥相交一律保留，故画面与旧版一致、仅减少不可见项。
		*/
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		if (impl->cull_enabled && impl->mesh.mesh_bounds_get(handle, bounds_min, bounds_max))
		{
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			//模型矩阵（空指针视作单位矩阵）
			const float* model = (transform != nullptr) ? transform : IDENTITY_MATRIX4;
			//若包围盒完全落在视锥外则跳过登记
			if (frustum_cull_aabb(view_proj, model, bounds_min, bounds_max))
			{
				//计入剔除观测（累计量）
				++impl->culled_items;
				return true;
			}
		}

		/*
		模型矩阵改由「实例缓冲」承载（本层统一走实例路径）
		transform 为空则用单位矩阵；实例数为 1 即等价于逐次绘制（模型矩阵不再经推送常量传递）。
		*/
		if (impl->pending_instances.size() >= impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储已满（容量 {}），绘制被拒", impl->instance_capacity);
			return false;
		}
		//追加本项模型矩阵与材质并取其在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(impl->pending_instances, transform, 1,
			impl->material_pbr_active, impl->material_pbr_roughness, impl->material_pbr_metallic);

		/*
		组装待绘制项
		描述符集（set0 场景 UBO / set1 采样器）延迟到 frame_render 时按「本帧帧槽」填装，
		故此处先置空、数量为 0。
		*/
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_get();
		item.layout = impl->pipeline.layout_get();
		item.scene_set = VK_NULL_HANDLE;
		item.sampler_set = VK_NULL_HANDLE;
		item.descriptor_set_count = 0;
		item.vertex_buffer = mesh_buffer;
		item.index_buffer = mesh_buffer;
		item.index_offset = index_offset;
		item.index_count = index_count;
		item.instance_buffer = VK_NULL_HANDLE;
		item.instance_offset = instance_offset;
		item.instance_count = 1;
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !impl->in_text_draw;
		/*
		颜色 / 色调推送（文字着色 ＋ 第六十九轮逐物体色调）
		矩阵已由实例缓冲承载，推送块前 64 字节仅用于占位（单位矩阵），后 16 字节为颜色；
		存储于 Impl 的 deque（元素引用稳定），绘制项持其指针，直到 frame_render 消费。
		非文字路径**恒推送**：显式 color 优先，其次吸附式逐物体色调，最后回落白色——
		场景片元着色器把反照率 × 该颜色，白色时画面逐字节不变。
		*/
		const float* push_color = color;
		//非文字且未显式给出颜色时，改用吸附色调或白色
		if (push_color == nullptr && !impl->in_text_draw)
			push_color = impl->material_tint_active
				? impl->material_tint
				: WHITE_COLOR_RGBA;
		if (push_color != nullptr)
		{
			impl->pending_push_constants.emplace_back();
			std::array<float, 20>& push = impl->pending_push_constants.back();
			//前 64 字节 = 单位矩阵
			push.fill(0.0f);
			push[0] = 1.0f;
			push[5] = 1.0f;
			push[10] = 1.0f;
			push[15] = 1.0f;
			//后 16 字节 = 颜色
			for (std::size_t index = 0; index < 4; ++index)
				push[16 + index] = push_color[index];
			item.push_constants = push.data();
			item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		}

		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
	}

	//实例化绘制（同一网格 × count 个模型矩阵，一次绘制调用提交）
	bool Renderer::mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//实例数必须为正
		if (count == 0)
		{
			logger.error("渲染器:: 实例化绘制的实例数必须为正");
			return false;
		}
		//取网格记录（句柄无效则拒绝）
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!impl->mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 实例化绘制的网格句柄无效");
			return false;
		}
		/*
		整批视锥剔除（第二十二轮新增）
		逐实例判可见性，**全部实例都在视锥外**才整批跳过（部分可见则整批保留，含被遮挡实例）。
		*/
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		if (transforms != nullptr && impl->cull_enabled && impl->mesh.mesh_bounds_get(handle, bounds_min, bounds_max))
		{
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			//是否至少有一个实例可见
			bool any_visible = false;
			for (uint32_t instance = 0; instance < count && !any_visible; ++instance)
				if (!frustum_cull_aabb(view_proj, transforms + static_cast<std::size_t>(instance) * 16, bounds_min, bounds_max))
					any_visible = true;
			//若整批不可见则不登记
			if (!any_visible)
			{
				//整批按一个绘制项计（与登记量的口径一致）
				++impl->culled_items;
				return true;
			}
		}
		/*
		本帧实例矩阵存储容量校验
		不足则整体拒绝、不登记任何绘制项（避免留下「半个实例化绘制」）。
		*/
		if (impl->pending_instances.size() + count > impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储不足（需 {} 个，余 {} 个）", count,
				impl->instance_capacity - static_cast<uint32_t>(impl->pending_instances.size()));
			return false;
		}
		//追加本批模型矩阵与材质并取首个实例在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(impl->pending_instances, transforms, count,
			impl->material_pbr_active, impl->material_pbr_roughness, impl->material_pbr_metallic);

		/*
		组装待绘制项
		描述符集与实例缓冲句柄延迟到 frame_render 时按「本帧帧槽」填装，故此处先置空。
		*/
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_get();
		item.layout = impl->pipeline.layout_get();
		item.scene_set = VK_NULL_HANDLE;
		item.sampler_set = VK_NULL_HANDLE;
		item.descriptor_set_count = 0;
		item.vertex_buffer = mesh_buffer;
		item.index_buffer = mesh_buffer;
		item.index_offset = index_offset;
		item.index_count = index_count;
		item.instance_buffer = VK_NULL_HANDLE;
		item.instance_offset = instance_offset;
		item.instance_count = count;
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !impl->in_text_draw;
		/*
		色调推送（第六十九轮）
		场景片元着色器恒读推送常量 offset 64 处的 tint，故实例化绘制也必须推送该块：
		吸附色调或白色（未设置时），否则未设置的推送常量取值未定义。
		*/
		impl->pending_push_constants.emplace_back();
		std::array<float, 20>& push = impl->pending_push_constants.back();
		push.fill(0.0f);
		push[0] = 1.0f;
		push[5] = 1.0f;
		push[10] = 1.0f;
		push[15] = 1.0f;
		for (std::size_t index = 0; index < 4; ++index)
			push[16 + index] = impl->material_tint_active ? impl->material_tint[index] : 1.0f;
		item.push_constants = push.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
	}

	//从文件加载纹理（解码由纹理模块的 stb 路径承担）
	bool Renderer::texture_load_file(uint64_t& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 从文件加载纹理失败：尚未初始化");
			return false;
		}
		//转交纹理模块解码并创建资源
		return impl->texture.texture_create_from_file(handle_out, path);
	}

	//纹理加载
	bool Renderer::texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块创建
		return impl->texture.texture_create_from_memory(handle_out, pixels, width, height);
	}

	//程序生成棋盘格纹理
	bool Renderer::texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 棋盘格纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块生成
		return impl->texture.texture_create_checkerboard(handle_out, width, height, cell_size);
	}

	//卸载纹理
	void Renderer::texture_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//摘出纹理图像与其视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Texture_Record record{};
		if (!impl->texture.texture_detach(handle, record))
			return;
		//若卸载的是当前绑定视图，则回落内置默认白纹理（避免采样器状态切换时写入已失效视图）
		if (record.view == impl->bound_view)
			texture_bind_set(0);
		//组装延迟条目（图像与视图经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.image = record.image;
		resource.image_allocation = record.allocation;
		resource.view = record.view;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//读回纹理第 0 级像素
	bool Renderer::texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该纹理的采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（内存模块同步读回）
		return impl->texture.texture_readback(handle, pixels, capacity, width_out, height_out);
	}

	//读回纹理第 0 级的指定子区域
	bool Renderer::texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（子区域同步读回）
		return impl->texture.texture_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//设置当前绘制所用纹理
	void Renderer::texture_bind_set(uint64_t texture_handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零时回落内置默认白纹理
		uint64_t target = texture_handle == 0 ? impl->default_texture : texture_handle;
		//取目标纹理的视图与采样器
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(target, view, sampler))
		{
			//目标无效则回落默认白纹理
			target = impl->default_texture;
			if (!impl->texture.texture_bind_get(target, view, sampler))
			{
				logger.error("渲染器:: 纹理绑定失败：默认白纹理不可用");
				return;
			}
		}
		//记录当前绑定视图（采样器状态切换后据此重写 set1）
		impl->bound_view = view;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//把目标纹理写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, sampler, view);
	}

	//设置纹理采样器状态（切换后重新绑定当前纹理，使新采样器真正生效）
	void Renderer::texture_sampler_state_set(const Texture_Sampler_State& state)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
		按设备能力归一各向异性
		设备创建时未启用 samplerAnisotropy 特性时，采样器请求各向异性会创建失败，
		故此处先归一：不支持则置假并告警，倍数钳到 [1, 设备上限]。
		*/
		Texture_Sampler_State effective = state;
		//请求各向异性时按设备能力调整
		if (effective.anisotropy)
		{
			//若设备不支持该特性
			if (!impl->device.anisotropy_enabled_get())
			{
				logger.warn("渲染器:: 设备不支持采样器各向异性，本次请求已忽略");
				effective.anisotropy = false;
				effective.max_anisotropy = 1.0f;
			}
			else
			{
				//倍数钳到 [1, 设备上限]
				float limit = impl->device.max_anisotropy_get();
				if (effective.max_anisotropy > limit)
					effective.max_anisotropy = limit;
				if (effective.max_anisotropy < 1.0f)
					effective.max_anisotropy = 1.0f;
			}
		}
		/*
		切换共享采样器状态并以新采样器重写各帧槽 set1
		采样器被烘焙进各帧槽 set1，故切换后须重写 set1，否则采样仍按旧参数进行；
		切换会销毁并重建共享采样器，故先等全部在途帧完成，避免销毁仍被在途描述符集引用的旧采样器。
		重写以「当前绑定视图」为采样源（纹理与渲染目标共用该语义），未绑过任何源时回落内置默认白纹理。
		*/
		impl->frame.frames_wait();
		impl->texture.sampler_state_set(effective);
		//取重建后的共享采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 采样器状态切换失败：共享采样器不可用");
			return;
		}
		//当前绑定视图为空则回落内置默认白纹理视图
		VkImageView view = impl->bound_view != VK_NULL_HANDLE ? impl->bound_view : default_view;
		//记录当前绑定视图（保持与 set1 一致）
		impl->bound_view = view;
		//以新采样器重写全部帧槽 set1
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, sampler, view);
	}

	//设置后续绘制所用混合档（切档触发图形管线重建）
	void Renderer::blend_mode_set(Blend_Mode mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//同档重复设置为空操作
		if (impl->blend_mode == mode)
			return;
		/*
		触发图形管线重建（内部先等设备空闲，确保旧管线不再被在途帧引用）
		重建成功才更新已记录档位；失败则记录错误并保持档位记录（此时图形管线可能已不可用，
		不改变帧循环时序：本帧仍会清屏与推进，仅绘制项跳过）。
		*/
		if (!impl->pipeline.pipeline_default_blend_set(mode))
		{
			logger.error("渲染器:: 混合档切换失败（保持已记录档位 {}）", static_cast<int32_t>(impl->blend_mode));
			return;
		}
		impl->blend_mode = mode;
		logger.info("渲染器:: 混合档已切换为 {}", static_cast<int32_t>(mode));
	}

	//登记具名图形管线
	bool Renderer::pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader, Blend_Mode blend_mode)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 管线登记失败：尚未初始化");
			return false;
		}
		//加载顶点着色器模块
		VkShaderModule vert_module = VK_NULL_HANDLE;
		if (!impl->shader.module_load(vert_module, vert_shader))
		{
			logger.error("渲染器:: 管线登记失败：顶点着色器 {} 加载失败", vert_shader);
			return false;
		}
		//加载片元着色器模块
		VkShaderModule frag_module = VK_NULL_HANDLE;
		if (!impl->shader.module_load(frag_module, frag_shader))
		{
			logger.error("渲染器:: 管线登记失败：片元着色器 {} 加载失败", frag_shader);
			//回收已加载的顶点着色器模块，避免泄漏
			impl->shader.module_destroy(vert_module);
			return false;
		}
		//登记到管线模块（具名管线与默认场景管线共享同一管线布局）
		uint64_t handle = 0;
		if (!impl->pipeline.pipeline_register(handle, vert_module, frag_module, blend_mode))
		{
			logger.error("渲染器:: 管线登记失败：图形管线创建失败");
			//回收两个着色器模块，避免泄漏
			impl->shader.module_destroy(vert_module);
			impl->shader.module_destroy(frag_module);
			return false;
		}
		//记录具名管线（着色器模块所有权在门面）
		Impl::Extra_Pipeline_Record record{};
		record.handle = handle;
		record.vert_module = vert_module;
		record.frag_module = frag_module;
		impl->extra_pipelines.push_back(record);
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 具名管线已登记（句柄 {}）", handle);
		return true;
	}

	//卸载具名图形管线
	void Renderer::pipeline_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄 0 为默认场景管线，禁止卸载
		if (handle == 0)
			return;
		/*
		先等全部在途帧完成
		图形管线可能仍被在途命令缓冲引用（其绑定状态已被记录进该命令缓冲），
		未等在途帧用尽即销毁会触发校验告警且属未定义行为。
		*/
		impl->frame.frames_wait();
		//在门面记录中查找目标
		for (auto it = impl->extra_pipelines.begin(); it != impl->extra_pipelines.end(); ++it)
		{
			//跳过非目标记录
			if (it->handle != handle)
				continue;
			//注销管线模块中的图形管线
			impl->pipeline.pipeline_release(handle);
			//销毁该管线的两个着色器模块
			if (it->vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->vert_module);
			if (it->frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->frag_module);
			//移除门面记录
			impl->extra_pipelines.erase(it);
			//若卸载的是当前所选管线则回落默认场景管线
			if (impl->bound_pipeline_request == handle)
			{
				impl->bound_pipeline_request = 0;
				impl->pipeline.pipeline_activate(0);
			}
			//若该管线在后处理链中则一并摘除（避免链残留失效句柄导致整帧回落）
			std::size_t chain_before = impl->post_chain.size();
			impl->post_chain.erase(std::remove(impl->post_chain.begin(), impl->post_chain.end(), handle), impl->post_chain.end());
			//若确有摘除则告警
			if (impl->post_chain.size() != chain_before)
			{
				/*
				级序已压缩（第二十五轮修复）
				逐级专设参数按旧下标存放，压缩后会错位到别的级，故整体清除（宁可回到全局参数，不可错配）。
				*/
				for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
					impl->post_stage_param_valid[stage] = false;
				logger.warn("渲染器:: 后处理链已摘除被卸载管线（句柄 {}，剩余 {} 级）；逐级参数已整体清除",
					handle, impl->post_chain.size());
			}
			logger.info("渲染器:: 具名管线已卸载（句柄 {}）", handle);
			return;
		}
	}

	//设置后续绘制所用图形管线
	void Renderer::pipeline_bind_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//目标句柄（0 = 默认场景管线）
		uint64_t target = handle;
		//若句柄非零且尚未登记则回落默认
		if (target != 0 && !impl->pipeline.pipeline_has(target))
		{
			logger.warn("渲染器:: 管线句柄 {} 未登记，回落默认场景管线", handle);
			target = 0;
		}
		//激活目标管线（失败则保持原激活不变）
		if (!impl->pipeline.pipeline_activate(target))
			return;
		impl->bound_pipeline_request = target;
	}

	//登记材质（图形管线 ＋ 纹理 ＋ 混合档）
	bool Renderer::material_load(Material_Handle& handle_out, const Material_Data& data)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 材质登记失败：尚未初始化");
			return false;
		}
		//待登记记录
		Impl::Material_Record record{};
		record.pipeline = data.pipeline;
		record.texture = data.texture;
		/*
		逐物体 PBR（第六十八轮）
		与非数 / 越界字段一律按「不覆盖」处理，避免把非法参数写进实例属性；
		合法域：粗糙度 (0.05, 4]、金属度 [0, 1]。
		*/
		record.pbr_override = data.pbr_override
			&& data.pbr_roughness > 0.05f && data.pbr_roughness <= 4.0f
			&& data.pbr_metallic >= 0.0f && data.pbr_metallic <= 1.0f;
		if (record.pbr_override)
		{
			record.pbr_roughness = data.pbr_roughness;
			record.pbr_metallic = data.pbr_metallic;
		}
		//若声明覆盖但参数非法则告警（按不覆盖登记，保持默认画面）
		if (data.pbr_override && !record.pbr_override)
			logger.warn("渲染器:: 材质 PBR 参数非法（粗糙度 {}、金属度 {}），按不覆盖登记",
				data.pbr_roughness, data.pbr_metallic);
		/*
		逐物体反照率色调（第六十九轮）
		四个分量都在 [0, 1] 才视为覆盖；否则按「不覆盖」登记，不把脏值送进推送常量。
		*/
		record.tint_override = data.tint_override
			&& data.tint[0] >= 0.0f && data.tint[0] <= 1.0f
			&& data.tint[1] >= 0.0f && data.tint[1] <= 1.0f
			&& data.tint[2] >= 0.0f && data.tint[2] <= 1.0f
			&& data.tint[3] >= 0.0f && data.tint[3] <= 1.0f;
		if (record.tint_override)
			for (std::size_t index = 0; index < 4; ++index)
				record.tint[index] = data.tint[index];
		//若声明色调覆盖但参数非法则告警
		if (data.tint_override && !record.tint_override)
			logger.warn("渲染器:: 材质色调非法（{} / {} / {} / {}），按不覆盖登记",
				data.tint[0], data.tint[1], data.tint[2], data.tint[3]);
		//若未指定管线且混合档非不透明：复用 / 登记该档的**共享**具名管线
		if (record.pipeline == 0 && data.blend != Blend_Mode::Opaque)
		{
			//按档位序取缓存下标并确保缓存表足够长
			std::size_t index = static_cast<std::size_t>(data.blend);
			if (impl->blend_pipelines.size() <= index)
				impl->blend_pipelines.resize(index + 1, 0);
			//该档尚未登记则以默认场景着色器对登记一条（此后同档材质共享之）
			if (impl->blend_pipelines[index] == 0)
			{
				if (!pipeline_load(impl->blend_pipelines[index], "场景.vert.spv", "场景.frag.spv", data.blend))
				{
					logger.error("渲染器:: 材质登记失败：具名管线登记失败");
					return false;
				}
			}
			//材质使用该档的共享管线（所有权归门面，卸载不释放）
			record.pipeline = impl->blend_pipelines[index];
		}
		//标记记录有效
		record.valid = true;
		//分配句柄：优先复用第一个空槽，否则追加
		Material_Handle handle = 0;
		for (std::size_t index = 0; index < impl->materials.size(); ++index)
		{
			//若该槽已空
			if (!impl->materials[index].valid)
			{
				impl->materials[index] = record;
				handle = static_cast<Material_Handle>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			impl->materials.push_back(record);
			handle = static_cast<Material_Handle>(impl->materials.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 材质已登记（句柄 {}，管线 {}，纹理 {}）", handle, record.pipeline, record.texture);
		return true;
	}

	//卸载材质
	void Renderer::material_unload(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零或越界即视为无需卸载（幂等）
		if (handle == 0 || handle > impl->materials.size())
			return;
		//取目标记录
		Impl::Material_Record& record = impl->materials[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//复位记录（标记槽为空；具名管线为门面共享资源，不随材质卸载释放）
		record = Impl::Material_Record{};
		logger.info("渲染器:: 材质已卸载（句柄 {}）", handle);
	}

	//设置后续绘制所用材质
	void Renderer::material_bind_set(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零则回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖
		if (handle == 0)
		{
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			return;
		}
		//句柄无效则回落默认并告警
		if (handle > impl->materials.size() || !impl->materials[handle - 1].valid)
		{
			logger.warn("渲染器:: 材质句柄 {} 无效，回落默认管线与默认纹理", handle);
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			return;
		}
		//取目标记录
		const Impl::Material_Record& record = impl->materials[handle - 1];
		//绑定材质管线与纹理（管线句柄未登记 / 纹理句柄无效时各自内部回落默认）
		pipeline_bind_set(record.pipeline);
		texture_bind_set(record.texture);
		//按记录的 PBR 覆盖标志设置 / 清除逐物体材质（第六十八轮）
		if (record.pbr_override)
			impl->material_pbr_active = true;
		else
			impl->material_pbr_active = false;
		impl->material_pbr_roughness = record.pbr_override ? record.pbr_roughness : 1.0f;
		impl->material_pbr_metallic = record.pbr_override ? record.pbr_metallic : 0.0f;
		//按记录同步逐物体色调（第六十九轮）
		impl->material_tint_active = record.tint_override;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = record.tint_override ? record.tint[index] : 1.0f;
	}

	/*
	设置逐物体 PBR 材质（第六十八轮）
	粗糙度合法域 (0.05, 4]、金属度合法域 [0, 1]；任一项越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（实例属性在 mesh_draw 时写入），与绑定类接口的吸附语义一致。
	*/
	bool Renderer::material_pbr_set(float roughness, float metallic)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体材质失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(roughness > 0.05f && roughness <= 4.0f))
		{
			logger.warn("渲染器:: 逐物体材质粗糙度 {} 越界（合法域 (0.05, 4]），已忽略", roughness);
			return false;
		}
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体材质金属度 {} 越界（合法域 [0, 1]），已忽略", metallic);
			return false;
		}
		//写入吸附状态
		impl->material_pbr_active = true;
		impl->material_pbr_roughness = roughness;
		impl->material_pbr_metallic = metallic;
		return true;
	}

	//清除逐物体 PBR 材质（第六十八轮）
	void Renderer::material_pbr_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落全局参数
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
	}

	//当前是否启用逐物体 PBR 材质覆盖（第六十八轮）
	bool Renderer::material_pbr_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_pbr_active;
	}

	//当前逐物体材质粗糙度缩放（第六十八轮）
	float Renderer::material_pbr_roughness(void) const
	{
		//未初始化返回默认 1
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->material_pbr_roughness;
	}

	//当前逐物体材质金属度（第六十八轮）
	float Renderer::material_pbr_metallic(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_pbr_metallic;
	}

	/*
	设置逐物体反照率色调（第六十九轮）
	四个分量须都在 [0, 1]；任一分量越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（推送常量在 mesh_draw 时写入），与绑定类接口的吸附语义一致；
	mesh_draw 显式给出的 color 形参优先于本状态。
	*/
	bool Renderer::material_tint_set(float red, float green, float blue, float alpha)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体色调失败：尚未初始化");
			return false;
		}
		//各分量合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(red >= 0.0f && red <= 1.0f) || !(green >= 0.0f && green <= 1.0f)
			|| !(blue >= 0.0f && blue <= 1.0f) || !(alpha >= 0.0f && alpha <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体色调分量越界（{} / {} / {} / {}），已忽略", red, green, blue, alpha);
			return false;
		}
		//写入吸附状态
		impl->material_tint_active = true;
		impl->material_tint[0] = red;
		impl->material_tint[1] = green;
		impl->material_tint[2] = blue;
		impl->material_tint[3] = alpha;
		return true;
	}

	//清除逐物体反照率色调（第六十九轮）
	void Renderer::material_tint_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落白色（画面与未设置时一致）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
	}

	//当前是否启用逐物体反照率色调（第六十九轮）
	bool Renderer::material_tint_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_tint_active;
	}

	//读取当前逐物体色调（第六十九轮）
	void Renderer::material_tint_get(float out[4]) const
	{
		//空指针直接忽略
		if (out == nullptr)
			return;
		//未初始化返回白色
		if (impl == nullptr || !impl->initialized)
		{
			for (std::size_t index = 0; index < 4; ++index)
				out[index] = 1.0f;
			return;
		}
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = impl->material_tint[index];
	}

	//创建离屏渲染目标
	bool Renderer::render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 创建渲染目标失败：尚未初始化");
			return false;
		}
		//转交渲染目标模块（颜色 / 深度格式取当前交换链格式）
		return impl->render_target.target_create(handle_out, width, height,
			impl->swapchain.format_get(), impl->swapchain.depth_format_get());
	}

	//读回离屏渲染目标的颜色内容
	bool Renderer::render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回）
		return impl->render_target.target_readback(handle, pixels, capacity, width_out, height_out);
	}

	//读回离屏渲染目标的指定子矩形
	bool Renderer::render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（子区域同步拷贝）
		return impl->render_target.target_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//读回离屏渲染目标的深度图像
	bool Renderer::render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（深度同步拷贝）
		return impl->render_target.target_readback_depth(handle, values, capacity, width_out, height_out);
	}

	//销毁离屏渲染目标
	void Renderer::render_target_destroy(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//若销毁的是当前设定的本帧渲染目标则先回落交换链路径
		if (impl->active_target == handle)
			impl->active_target = 0;
		//若销毁的是当前设定的同帧后处理呈现目标则一并回落并清空目标组
		if (impl->present_target == handle)
		{
			impl->present_target = 0;
			impl->present_extra.clear();
		}
		//若销毁的是目标组中的额外中间目标则从组内摘除（链长 ≥ 2 将因此回落单级路径）
		for (std::size_t index = 0; index < impl->present_extra.size(); ++index)
		{
			//命中即原地摘除并结束（句柄唯一）
			if (impl->present_extra[index] == handle)
			{
				impl->present_extra.erase(impl->present_extra.begin() + static_cast<std::ptrdiff_t>(index));
				break;
			}
		}
		//摘出颜色 / 深度图像与视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Render_Target_Record record{};
		if (!impl->render_target.target_detach(handle, record))
			return;
		//组装延迟条目（颜色与深度各一条内存资源）
		Deferred_Entry entry{};
		Deferred_Memory_Resource color{};
		color.image = record.color_image;
		color.image_allocation = record.color_alloc;
		color.view = record.color_view;
		entry.memory_resources.push_back(color);
		Deferred_Memory_Resource depth{};
		depth.image = record.depth_image;
		depth.image_allocation = record.depth_alloc;
		depth.view = record.depth_view;
		entry.memory_resources.push_back(depth);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//设置「本帧渲染到目标」
	void Renderer::render_target_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄非零且未登记则回落交换链路径并告警
		if (handle != 0 && !impl->render_target.target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", handle);
			impl->active_target = 0;
			return;
		}
		impl->active_target = handle;
	}

	//设置「本帧渲染到目标并同帧后处理呈现到交换链」
	void Renderer::render_target_present_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄非零且未登记则关闭该路径并告警
		if (handle != 0 && !impl->render_target.target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，关闭同帧后处理呈现路径", handle);
			impl->present_target = 0;
			impl->present_extra.clear();
			return;
		}
		impl->present_target = handle;
		//目标组重置为「仅该目标」（多级链须改用 render_target_present_chain_set）
		impl->present_extra.clear();
	}

	//设置「同帧后处理链」的中间目标组（2..4 个句柄；首项为场景目标）
	bool Renderer::render_target_present_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 后处理目标组设置失败：尚未初始化");
			return false;
		}
		//若为清除目标组（关闭同帧后处理呈现路径）
		if (count == 0)
		{
			impl->present_target = 0;
			impl->present_extra.clear();
			logger.info("渲染器:: 后处理目标组已清除");
			return true;
		}
		//若句柄数组为空 / 数量越界则整组拒绝
		if (handles == nullptr || count > Impl::PRESENT_TARGET_MAX)
		{
			logger.error("渲染器:: 后处理目标组设置失败：句柄数组为空或数量 {} 越界（上限 {}）",
				count, Impl::PRESENT_TARGET_MAX);
			return false;
		}
		//逐个校验句柄已登记
		for (uint32_t each = 0; each < count; ++each)
		{
			//若某句柄未登记则整组拒绝（保持原目标组不变）
			if (!impl->render_target.target_valid(handles[each]))
			{
				logger.error("渲染器:: 后处理目标组设置失败：第 {} 个句柄 {} 未登记", each, handles[each]);
				return false;
			}
		}
		//写入目标组（首项为场景目标，其余为额外中间目标）
		impl->present_target = handles[0];
		impl->present_extra.assign(handles + 1, handles + count);
		logger.info("渲染器:: 后处理目标组已设置（{} 个）", count);
		return true;
	}

	//设置「同帧后处理链」（若干已登记管线句柄，下标即级序）
	bool Renderer::post_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 后处理链设置失败：尚未初始化");
			return false;
		}
		//若为清除链
		if (count == 0)
		{
			impl->post_chain.clear();
			//链已清空 → 逐级专设参数一并失效
			for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
				impl->post_stage_param_valid[stage] = false;
			logger.info("渲染器:: 后处理链已清除（回落单级默认后处理）");
			return true;
		}
		//句柄数组为空而级数非零则整链拒绝
		if (handles == nullptr)
		{
			logger.error("渲染器:: 后处理链设置失败：句柄数组为空而级数非零");
			return false;
		}
		/*
		级数上限（处置 R-FRAME-12 之①）
		每级都要成对目标里的一级源 / 宿，且每帧后处理仅占 1 个实例位；超过上限即整链拒绝，
		避免调用方误用超长链导致隐性性能坍塌（上限取 8，足够覆盖常见后处理链）。
		*/
		if (count > Impl::POST_CHAIN_MAX_STAGES)
		{
			logger.error("渲染器:: 后处理链设置失败：级数 {} 超过上限 {}", count, Impl::POST_CHAIN_MAX_STAGES);
			return false;
		}
		//逐句柄校验已登记（任一句柄未登记即整链拒绝，保持原链不变）
		for (uint32_t index = 0; index < count; ++index)
		{
			//若该级句柄未登记
			if (!impl->pipeline.pipeline_has(handles[index]))
			{
				logger.error("渲染器:: 后处理链设置失败：第 {} 级句柄 {} 未登记", index, handles[index]);
				return false;
			}
		}
		//写入链（保持各句柄的登记顺序不变）
		impl->post_chain.assign(handles, handles + count);
		//链已变 → 逐级专设参数失效（旧下标对应旧级序）
		for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
			impl->post_stage_param_valid[stage] = false;
		logger.info("渲染器:: 后处理链已设置（{} 级）", count);
		return true;
	}

	//读取当前后处理链级数（0 表未设链）
	uint32_t Renderer::post_chain_count(void) const
	{
		//未初始化则视为空链
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->post_chain.size());
	}

	//读回当前后处理链的各级管线句柄（只读诊断入口）
	uint32_t Renderer::post_chain_get(uint64_t* out, uint32_t capacity) const
	{
		//出参为空 / 未初始化 / 未设链则返回 0
		if (out == nullptr || impl == nullptr || !impl->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>(impl->post_chain.size());
		if (count > capacity)
			count = capacity;
		//按当前级序逐级写出（级序可能因管线卸载而压缩）
		for (uint32_t index = 0; index < count; ++index)
			out[index] = impl->post_chain[index];
		return count;
	}

	//把渲染目标的颜色绑定为当前纹理（供后续绘制采样）
	void Renderer::texture_bind_target(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//取目标颜色图像与视图
		VkImage color_image = VK_NULL_HANDLE;
		VkImageView color_view = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkExtent2D extent = { 0, 0 };
		//句柄无效则静默返回（保持原绑定）
		if (!impl->render_target.target_get(handle, color_image, color_view, depth_view, extent))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，纹理绑定忽略", handle);
			return;
		}
		/*
		取共享采样器
		采样器由纹理模块持有且被烘焙进各帧槽 set1；此处借内置默认白纹理取一次采样器句柄，
		随后以「目标颜色视图 ＋ 该采样器」写入所有帧槽 set1。
		*/
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler shared_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
		{
			logger.error("渲染器:: 渲染目标绑定失败：共享采样器不可用");
			return;
		}
		//记录当前绑定视图（与纹理绑定共用同一语义；采样器状态切换后据此重写 set1）
		impl->bound_view = color_view;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//把目标颜色视图写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, shared_sampler, color_view);
		logger.info("渲染器:: 渲染目标已绑定为当前纹理（句柄 {}）", handle);
	}

	//从 TTF 文件加载字体（字号 = 字形像素高度）
	bool Renderer::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 加载字体失败：尚未初始化");
			return false;
		}
		//转交字体模块（光栅化字形并打包图集）
		return impl->font.font_load_file(handle_out, path, pixel_height);
	}

	//卸载字体（一并清除其文本网格缓存）
	void Renderer::font_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//先清除该字体的文本网格缓存（网格模块就绪时归还网格）
		impl->text.font_meshes_destroy(handle);
		//归还字体（含图集纹理）
		impl->font.font_destroy(handle);
	}

	//绘制文本（默认布局：单行、左对齐）
	bool Renderer::text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y)
	{
		//默认布局即单行左对齐（不换行、行高取字体像素高度）
		return text_draw_layout(font, text, pixel_x, pixel_y, Text_Layout{});
	}

	//度量单行文本（像素；宽不含尾随空白，高恒为字体像素高度）
	bool Renderer::text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const
	{
		//默认零度量
		if (width_out != nullptr)
			*width_out = 0.0f;
		if (height_out != nullptr)
			*height_out = 0.0f;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//取字体记录（只读）
		const Font_Record* record = impl->font.font_get(font);
		if (record == nullptr)
			return false;
		//像素度量（整型）
		uint32_t width = 0;
		uint32_t height = 0;
		if (!Font_Context::text_measure(*record, text, width, height))
			return false;
		//写出浮点出参
		if (width_out != nullptr)
			*width_out = static_cast<float>(width);
		if (height_out != nullptr)
			*height_out = static_cast<float>(height);
		return true;
	}

	//按布局绘制文本（像素坐标，原点在窗口左上、Y 向下）；第四十七轮下沉至 Text_Renderer
	bool Renderer::text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
		const Text_Layout& layout)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//标记进入文字绘制（第五十二轮：文字网格不参与阴影投影）
		impl->in_text_draw = true;
		//转发文字绘制子系统
		const bool drawn = impl->text.draw_layout(font, text, pixel_x, pixel_y, layout);
		//恢复场景绘制标记
		impl->in_text_draw = false;
		return drawn;
	}

	//设置泛光参数（阈值 / 软膝 / 强度）
	void Renderer::bloom_param_set(float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：阈值须正、软膝非负且小于阈值、强度须为有限正值
		bool valid = (threshold > 0.0f) && (knee >= 0.0f) && (knee < threshold)
			&& (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 泛光参数非法（阈值 {}、软膝 {}、强度 {}），整次忽略",
				threshold, knee, intensity);
			return;
		}
		impl->bloom_param[0] = threshold;
		impl->bloom_param[1] = knee;
		impl->bloom_param[2] = intensity;
	}

	//设置某一级后处理链的泛光参数（覆盖全局值）
	void Renderer::post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= Impl::POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级泛光参数设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		//参数校验：阈值须正、软膝非负且小于阈值、强度须为有限正值
		bool valid = (threshold > 0.0f) && (knee >= 0.0f) && (knee < threshold)
			&& (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 逐级泛光参数非法（级 {}：阈值 {}、软膝 {}、强度 {}），整次忽略",
				stage, threshold, knee, intensity);
			return;
		}
		impl->post_stage_param[stage][0] = threshold;
		impl->post_stage_param[stage][1] = knee;
		impl->post_stage_param[stage][2] = intensity;
		impl->post_stage_param[stage][3] = 0.0f;
		impl->post_stage_param_valid[stage] = true;
	}

	//启用 / 关闭视锥剔除
	void Renderer::cull_enabled_set(bool enabled)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->cull_enabled = enabled;
	}

	//查询视锥剔除是否启用
	bool Renderer::cull_enabled(void) const
	{
		//未初始化时返回默认值（开启）
		if (impl == nullptr || !impl->initialized)
			return true;
		return impl->cull_enabled;
	}

	//读取某一级的泛光专设参数（只读诊断）
	uint32_t Renderer::post_stage_param_get(uint32_t stage, float out[4]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return 0;
		//默认写零
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = 0.0f;
		//未初始化 / 级序不在链内 / 该级未设专设值 → 0
		if (impl == nullptr || !impl->initialized || stage >= impl->post_chain.size()
			|| stage >= Impl::POST_CHAIN_MAX_STAGES || !impl->post_stage_param_valid[stage])
			return 0;
		//写出该级专设值
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = impl->post_stage_param[stage][each];
		return 1;
	}

	//设置文字颜色（RGBA，逐分量钳制）
	void Renderer::text_color_set(const float color[4])
	{
		//未初始化或出参为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//转发文字绘制子系统（逐分量钳制）
		impl->text.color_set(color);
	}

	//取该字体累计被跳过的码点数（诊断入口）
	uint64_t Renderer::font_skipped_codepoints(uint64_t font) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->font.font_skipped_codepoints_get(font);
	}

	//相机：设置位置
	void Renderer::camera_position_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.position_set(x, y, z);
	}

	//相机：设置注视点
	void Renderer::camera_target_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.target_set(x, y, z);
	}

	//相机：设置视场角（弧度）
	void Renderer::camera_fov_set(float radians)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.fov_set(radians);
	}

	//相机：设置近远平面
	void Renderer::camera_near_far_set(float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.near_far_set(near_plane, far_plane);
	}

	//应用场景描述（三组入参；第四十七轮）
	void Renderer::scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
		相机参数校验（处置 R-FACADE-10）
		视场角须落在 (0, π)、近平面须为正、远平面须大于近平面；任一非法即整次拒绝，
		保持既有相机与清屏色不变。用「取反比较」写法使 NaN 同样被判为非法。
		*/
		bool camera_valid = (camera.fov > 0.0f) && (camera.fov < 3.14159265f)
			&& (camera.near_plane > 0.0f) && (camera.far_plane > camera.near_plane)
			//正交模式额外要求垂直可见高度为正（否则投影矩阵退化）
			&& (!camera.ortho || camera.ortho_height > 0.0f);
		//若相机参数非法
		if (!camera_valid)
		{
			logger.warn("渲染器:: 场景相机非法（视场角 {}、近平面 {}、远平面 {}），整次拒绝",
				camera.fov, camera.near_plane, camera.far_plane);
			return;
		}
		//相机位置 / 注视点
		impl->camera.position_set(camera.position[0], camera.position[1], camera.position[2]);
		impl->camera.target_set(camera.target[0], camera.target[1], camera.target[2]);
		//视场角与近远平面
		impl->camera.fov_set(camera.fov);
		impl->camera.near_far_set(camera.near_plane, camera.far_plane);
		//投影模式（正交 / 透视；高度合法性已在上方整体校验）
		impl->camera.ortho_set(camera.ortho, camera.ortho_height);
		//清屏色（统一走 clear_color_set：逐分量钳制到 [0,1]）
		clear_color_set(camera.clear_color);
		//光照（统一走 light_set：非法参数由后者整次拒绝）
		light_set(light.direction, light.color, light.intensity, light.ambient);
		/*
		点光源组与镜面高光
		先按方向光重置点光源（light_set 已清空），再逐条追加；单条非法只跳过该条并告警，
		数量超过上限（第六十二轮起 8）的部分同样忽略。
		*/
		for (uint32_t index = 0; index < light.point_count; ++index)
			//超过上限的部分不再登记
			if (index >= Impl::POINT_LIGHT_MAX)
				break;
			else
				light_add_point(light.point_position[index], light.point_color[index],
					light.point_radius[index], light.point_intensity[index], light.ambient,
					light.point_attenuation[index], light.point_specular[index], light.point_shininess[index]);
		/*
		聚光灯组
		在点光源之后逐条追加；两类光源共用 8 盏上限，单条非法（含锥角非法）或已满时只跳过该条。
		*/
		for (uint32_t index = 0; index < light.spot_count; ++index)
			//超过上限的部分不再登记
			if (index >= Impl::POINT_LIGHT_MAX)
				break;
			else
				light_add_spot(light.spot_position[index], light.spot_direction[index],
					light.spot_color[index], light.spot_radius[index], light.spot_intensity[index],
					light.spot_inner_deg[index], light.spot_outer_deg[index], light.ambient,
					light.spot_attenuation[index], light.spot_specular[index], light.spot_shininess[index]);
		specular_set(light.specular_intensity, light.specular_shininess);
		//距离衰减模式（非法值由 light_attenuation_set 拒绝并保持当前模式）
		light_attenuation_set(light.attenuation_mode);
		//距离雾（非法参数由 fog_set 整次忽略）
		fog_set(fog.color, fog.density, fog.start, fog.end);
		//环境光颜色（非法分量由 ambient_color_set 整次忽略）
		ambient_color_set(light.ambient_color);
		//半球环境光（地面颜色 ＋ 混合强度；非法颜色由 ambient_ground_set 整次忽略）
		ambient_ground_set(light.ambient_ground, light.ambient_hemisphere);
		//补光（第二盏方向光；强度 0 = 关闭；第五十轮含逐光高光缩放）
		light_fill_set(light.fill_direction, light.fill_color, light.fill_intensity, light.fill_specular);
		//方向光逐光高光缩放（第五十轮；非法值由 setter 忽略）
		directional_specular_set(light.directional_specular);
		//方向光阴影开关与偏置（第五十二轮）
		shadow_enable_set(light.shadow_enabled != 0);
		shadow_bias_set(light.shadow_bias);
		//阴影软度与强度（第五十三轮；非法值由 setter 整次忽略）
		shadow_softness_set(light.shadow_softness);
		shadow_strength_set(light.shadow_strength);
		//阴影图分辨率（第五十四轮；非法值由 setter 整次忽略）
		shadow_resolution_set(static_cast<uint32_t>(light.shadow_resolution));
		//PBR-lite 镜面（第六十四轮；非法缩放由 setter 整次忽略）
		pbr_enable_set(light.pbr_enabled != 0);
		pbr_roughness_set(light.pbr_roughness);
		//PBR 金属度（第六十五轮；非法值由 setter 整次忽略）
		pbr_metallic_set(light.pbr_metallic);
		//若点光源数量超过上限则告警（已登记前 8 盏）
		if (light.point_count > Impl::POINT_LIGHT_MAX)
			logger.warn("渲染器:: 场景点光源数量 {} 超过上限 {}，仅登记前 {} 盏",
				light.point_count, Impl::POINT_LIGHT_MAX, Impl::POINT_LIGHT_MAX);
		//若聚光灯数量超过上限则告警（点光源与聚光灯共用 8 盏上限）
		if (light.spot_count > Impl::POINT_LIGHT_MAX)
			logger.warn("渲染器:: 场景聚光灯数量 {} 超过上限 {}，仅登记前 {} 盏（与点光源共用上限）",
				light.spot_count, Impl::POINT_LIGHT_MAX, Impl::POINT_LIGHT_MAX);
	}

	//应用场景描述（兼容扁平聚合；内部拆成三组后转发）
	void Renderer::scene_apply(const Scene_Data& scene)
	{
		//相机组
		Camera_Data camera{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			camera.position[index] = scene.position[index];
			camera.target[index] = scene.target[index];
		}
		camera.fov = scene.fov;
		camera.near_plane = scene.near_plane;
		camera.far_plane = scene.far_plane;
		camera.ortho = scene.ortho;
		camera.ortho_height = scene.ortho_height;
		for (std::size_t index = 0; index < 4; ++index)
			camera.clear_color[index] = scene.clear_color[index];
		//光照组
		Light_Data light{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.direction[index] = scene.light_direction[index];
			light.color[index] = scene.light_color[index];
			light.ambient_color[index] = scene.ambient_color[index];
			light.ambient_ground[index] = scene.ambient_ground[index];
			light.fill_direction[index] = scene.fill_direction[index];
			light.fill_color[index] = scene.fill_color[index];
		}
		light.ambient_hemisphere = scene.ambient_hemisphere;
		light.fill_intensity = scene.fill_intensity;
		light.directional_specular = scene.directional_specular;
		light.fill_specular = scene.fill_specular;
		light.shadow_enabled = scene.shadow_enabled;
		light.shadow_bias = scene.shadow_bias;
		light.shadow_softness = scene.shadow_softness;
		light.shadow_strength = scene.shadow_strength;
		light.shadow_resolution = scene.shadow_resolution;
		//PBR-lite 镜面（第六十四轮）
		light.pbr_enabled = scene.pbr_enabled;
		light.pbr_roughness = scene.pbr_roughness;
		//PBR 金属度（第六十五轮）
		light.pbr_metallic = scene.pbr_metallic;
		light.intensity = scene.light_intensity;
		light.ambient = scene.light_ambient;
		light.point_count = scene.light_count;
		for (uint32_t index = 0; index < Impl::POINT_LIGHT_MAX; ++index)
		{
			for (std::size_t each = 0; each < 3; ++each)
			{
				light.point_position[index][each] = scene.point_light_position[index][each];
				light.point_color[index][each] = scene.point_light_color[index][each];
				light.spot_position[index][each] = scene.spot_light_position[index][each];
				light.spot_direction[index][each] = scene.spot_light_direction[index][each];
				light.spot_color[index][each] = scene.spot_light_color[index][each];
			}
			light.point_radius[index] = scene.point_light_radius[index];
			light.point_intensity[index] = scene.point_light_intensity[index];
			light.point_attenuation[index] = scene.point_light_attenuation[index];
			light.point_specular[index] = scene.point_light_specular[index];
			light.point_shininess[index] = scene.point_light_shininess[index];
			light.spot_radius[index] = scene.spot_light_radius[index];
			light.spot_intensity[index] = scene.spot_light_intensity[index];
			light.spot_inner_deg[index] = scene.spot_light_inner_deg[index];
			light.spot_outer_deg[index] = scene.spot_light_outer_deg[index];
			light.spot_attenuation[index] = scene.spot_light_attenuation[index];
			light.spot_specular[index] = scene.spot_light_specular[index];
			light.spot_shininess[index] = scene.spot_light_shininess[index];
		}
		light.specular_intensity = scene.specular_intensity;
		light.specular_shininess = scene.specular_shininess;
		light.spot_count = scene.spot_light_count;
		light.attenuation_mode = scene.attenuation_mode;
		//雾组
		Fog_Data fog{};
		for (std::size_t index = 0; index < 3; ++index)
			fog.color[index] = scene.fog_color[index];
		fog.density = scene.fog_density;
		fog.start = scene.fog_start;
		fog.end = scene.fog_end;
		//转发三组入参
		scene_apply(camera, light, fog);
	}

	//设置方向光与环境光（第二十九轮新增）
	void Renderer::light_set(const float direction[3], const float color[3], float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验
		方向长度须非零（否则无法归一化）、颜色各分量须落在 [0,1]、强度须非负且有限、环境光须落在 [0,1]；
		用「取反比较」写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 光照参数非法（方向 ({}, {}, {})、强度 {}、环境光 {}），整次拒绝",
				direction[0], direction[1], direction[2], intensity, ambient);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->light_direction[index] = direction[index] * inverse_length;
			impl->light_color[index] = color[index];
		}
		impl->light_intensity = intensity;
		impl->light_ambient = ambient;
		//切回「仅方向光」（清空点光源；第三十二轮）
		impl->point_light_count = 0;
	}

	//设置点光源（第三十一轮新增）
	void Renderer::light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return;
		/*
		参数校验（与 light_set 同口径，另加「半径须为正且有限」）
		用取反比较写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、环境光 {}），整次拒绝",
				position[0], position[1], position[2], radius, intensity, ambient);
			return;
		}
		//写入点光源参数（位置即「光位置」，由着色器按模式选择语义）
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->point_lights[0].position[index] = position[index];
			impl->point_lights[0].color[index] = color[index];
			//方向光颜色保持既有值
		}
		impl->point_lights[0].radius = radius;
		impl->point_lights[0].intensity = intensity;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		impl->point_lights[0].is_spot = false;
		impl->point_light_count = 1;
		impl->light_ambient = ambient;
		//关闭方向光（第三十二轮：单点光源入口等价于「仅一个点光源」）
		impl->light_intensity = 0.0f;
	}

	//追加一个点光源（第三十二轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer::light_add_point(const float position[3], const float color[3], float radius, float intensity,
		float ambient, uint32_t attenuation, float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return false;
		/*
		参数校验（与 light_point_set 同口径）＋ 容量校验
		任一非法或已满即返回假并告警，既有光源保持不变。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 追加点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}），拒绝",
				position[0], position[1], position[2], radius, intensity);
			return false;
		}
		//若已达上限
		if (impl->point_light_count >= Impl::POINT_LIGHT_MAX)
		{
			logger.warn("渲染器:: 点光源数量已达上限（{}），拒绝追加", Impl::POINT_LIGHT_MAX);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		light.is_spot = false;
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//追加一个聚光灯（第三十四轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer::light_add_spot(const float position[3], const float direction[3], const float color[3],
		float radius, float intensity, float inner_deg, float outer_deg, float ambient, uint32_t attenuation,
		float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || direction == nullptr || color == nullptr)
			return false;
		/*
		参数校验（点光源同口径 ＋ 方向非退化 ＋ 锥角关系）
		方向长度须非零；锥角须满足 0 < 内角 < 外角 ≤ 90；
		用取反比较写法使 NaN 同样被判为非法，任一非法即拒绝并保持既有光源。
		*/
		float direction_length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (direction_length_squared > 1e-12f) && (radius > 0.0f) && (radius <= 1e6f)
			&& (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			&& (inner_deg > 0.0f) && (inner_deg < outer_deg) && (outer_deg <= 90.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 聚光灯参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、内角 {}、外角 {}），拒绝",
				position[0], position[1], position[2], radius, intensity, inner_deg, outer_deg);
			return false;
		}
		//若已达上限
		if (impl->point_light_count >= Impl::POINT_LIGHT_MAX)
		{
			logger.warn("渲染器:: 光源数量已达上限（{}），拒绝追加聚光灯", Impl::POINT_LIGHT_MAX);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		float inverse_length = 1.0f / std::sqrt(direction_length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
			//方向归一化（内部恒存单位向量）
			light.spot_direction[index] = direction[index] * inverse_length;
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		light.is_spot = true;
		//锥角转余弦（度 → 弧度；内角更小故其余弦更大）
		constexpr float degrees_to_radians = 0.017453292519943295f;
		light.spot_inner_cos = std::cos(inner_deg * degrees_to_radians);
		light.spot_outer_cos = std::cos(outer_deg * degrees_to_radians);
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//清空全部点光源（第三十二轮新增）
	void Renderer::lights_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//清空计数并恢复方向光默认强度（方向 / 颜色 / 环境光保持既有值）
		impl->point_light_count = 0;
		impl->light_intensity = 1.0f;
	}

	//设置镜面高光参数（第三十一轮新增）
	void Renderer::specular_set(float intensity, float shininess)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验（取反比较使 NaN 判非法）
		if (!(intensity >= 0.0f && intensity <= 1e6f) || !(shininess >= 1.0f && shininess <= 1e6f))
		{
			logger.warn("渲染器:: 高光参数非法（强度 {}、光泽度 {}），整次拒绝", intensity, shininess);
			return;
		}
		impl->specular_intensity = intensity;
		impl->specular_shininess = shininess;
	}

	//设置距离衰减模式（第三十六轮新增）
	bool Renderer::light_attenuation_set(uint32_t mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//仅接受线性（0）与平方反比（1）
		if (mode > 1)
		{
			logger.warn("渲染器:: 距离衰减模式非法（{}），仅接受 0（线性）/ 1（平方反比）", mode);
			return false;
		}
		impl->attenuation_mode = mode;
		return true;
	}

	//读取当前距离衰减模式（第三十六轮新增）
	uint32_t Renderer::light_attenuation_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->attenuation_mode;
	}

	//设置距离雾（第三十八轮新增）
	void Renderer::fog_set(const float color[3], float density, float start, float end)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始非负、结束大于起始；
		任一非法即整次忽略并告警（保持既有雾参数）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f) && (end > start) && (end <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 雾参数非法（密度 {}、起始 {}、结束 {}），整次忽略", density, start, end);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			impl->fog_color[index] = color[index];
		impl->fog_density = density;
		impl->fog_start = start;
		impl->fog_end = end;
	}

	//关闭距离雾（第三十八轮新增）
	void Renderer::fog_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//密度归零即关闭（颜色与区间保持既有值，便于再次开启）
		impl->fog_density = 0.0f;
	}

	//设置环境光颜色（第四十轮新增）
	void Renderer::ambient_color_set(const float color[3])
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 环境光颜色非法（{}），整次忽略", color[index]);
				return;
			}
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_color[index] = color[index];
	}

	//设置半球环境光（第四十七轮新增）
	void Renderer::ambient_ground_set(const float ground_color[3], float hemisphere)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || ground_color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(ground_color[index] >= 0.0f && ground_color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 半球环境光颜色非法（{}），整次忽略", ground_color[index]);
				return;
			}
		//强度按 0..1 钳制（非数按 0 = 关闭处理）
		float blend = 0.0f;
		//仅有限值参与混合
		if (hemisphere >= 0.0f && hemisphere <= 1.0f)
			blend = hemisphere;
		else if (hemisphere > 1.0f)
			blend = 1.0f;
		//写入地面颜色与混合强度
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_ground[index] = ground_color[index];
		impl->ambient_ground[3] = blend;
	}

	//设置补光（第四十七轮新增；第二盏方向光；第五十轮支持逐光高光缩放）
	void Renderer::light_fill_set(const float direction[3], const float color[3], float intensity, float specular)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验（口径同 light_set）
		方向长度须非零、颜色各分量须落在 [0,1]、强度须非负且有限、高光缩放须落在 0..100；任一非法即整次拒绝。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool fill_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (specular >= 0.0f) && (specular <= 100.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fill_valid = false;
		//若补光参数非法
		if (!fill_valid)
		{
			logger.warn("渲染器:: 补光参数非法（强度 {}、高光缩放 {}），整次拒绝", intensity, specular);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->fill_direction[index] = direction[index] * inverse_length;
			impl->fill_color[index] = color[index];
		}
		impl->fill_intensity = intensity;
		impl->fill_specular = specular;
	}

	//设置方向光的逐光高光缩放（第五十轮新增）
	void Renderer::directional_specular_set(float scale)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//缩放须落在 0..100（取反比较使 NaN 判非法）
		if (!(scale >= 0.0f && scale <= 100.0f))
		{
			logger.warn("渲染器:: 方向光高光缩放非法（{}），整次忽略", scale);
			return;
		}
		impl->directional_specular = scale;
	}

	//开关方向光阴影（第五十二轮新增）
	void Renderer::shadow_enable_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
	首次开启时懒登记阴影管线
	未启用阴影的进程不承担管线创建开销；登记失败则保持关闭并告警（不改变其余状态）。
	阴影目标为 R8G8B8A8_UNORM（见 init ⑳ ①），故该管线必须按同一颜色格式创建，
	不能经公开 pipeline_load（它固定跟随交换链格式，sRGB 会破坏打包深度）。
	*/
	if (enabled && impl->shadow_pipeline == 0)
	{
		//加载阴影着色器模块
		VkShaderModule shadow_vert = VK_NULL_HANDLE;
		VkShaderModule shadow_frag = VK_NULL_HANDLE;
		if (!impl->shader.module_load(shadow_vert, "阴影.vert.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：顶点着色器加载失败，阴影保持关闭");
			impl->shadow_enabled = false;
			return;
		}
		if (!impl->shader.module_load(shadow_frag, "阴影.frag.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：片元着色器加载失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shadow_enabled = false;
			return;
		}
		//按阴影目标的 UNORM 颜色格式登记具名管线
		uint64_t shadow_pipeline = 0;
		if (!impl->pipeline.pipeline_register(shadow_pipeline, shadow_vert, shadow_frag,
			Blend_Mode::Opaque, VK_FORMAT_R8G8B8A8_UNORM, true))
		{
			logger.error("渲染器:: 阴影管线登记失败：图形管线创建失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shader.module_destroy(shadow_frag);
			impl->shadow_enabled = false;
			return;
		}
		//记录具名管线（着色器模块所有权在门面，随 pipeline_unload / shutdown 回收）
		Impl::Extra_Pipeline_Record record{};
		record.handle = shadow_pipeline;
		record.vert_module = shadow_vert;
		record.frag_module = shadow_frag;
		impl->extra_pipelines.push_back(record);
		impl->shadow_pipeline = shadow_pipeline;
	}
	impl->shadow_enabled = enabled;
	}

	//阴影是否启用（第五十二轮）
	bool Renderer::shadow_enabled(void) const
	{
		//未初始化一律按关闭处理
		return impl != nullptr && impl->initialized && impl->shadow_enabled;
	}

	//设置阴影深度偏置（第五十二轮）
	void Renderer::shadow_bias_set(float bias)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//偏置须落在 0..0.05（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f))
		{
			logger.warn("渲染器:: 阴影深度偏置非法（{}），整次忽略", bias);
			return;
		}
		impl->shadow_bias = bias;
	}

	//当前阴影深度偏置（第五十二轮）
	float Renderer::shadow_bias(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_bias : 0.003f;
	}

	//设置阴影软度（第五十三轮新增）
	void Renderer::shadow_softness_set(float softness)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//软度须落在 0..1（取反比较使 NaN 判非法）
		if (!(softness >= 0.0f && softness <= 1.0f))
		{
			logger.warn("渲染器:: 阴影软度非法（{}），整次忽略", softness);
			return;
		}
		impl->shadow_softness = softness;
	}

	//当前阴影软度（第五十三轮）
	float Renderer::shadow_softness(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_softness : 0.0f;
	}

	//设置阴影强度（第五十三轮新增）
	void Renderer::shadow_strength_set(float strength)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//强度须落在 0..1（取反比较使 NaN 判非法）
		if (!(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 阴影强度非法（{}），整次忽略", strength);
			return;
		}
		impl->shadow_strength = strength;
	}

	//当前阴影强度（第五十三轮）
	float Renderer::shadow_strength(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_strength : 1.0f;
	}

	/*
	设置阴影图分辨率（第五十四轮新增）
	仅接受 512 / 1024 / 2048；切换时先等尽在途帧，再建新目标、重新绑定各帧槽 binding 1，
	全部成功后才销毁旧目标（任一步失败均保持旧目标可用）。
	*/
	void Renderer::shadow_resolution_set(uint32_t resolution)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//仅接受三档边长
		if (resolution != 512 && resolution != 1024 && resolution != 2048)
		{
			logger.warn("渲染器:: 阴影分辨率非法（{}），整次忽略（仅支持 512 / 1024 / 2048）", resolution);
			return;
		}
		//与原值一致则无需重建
		if (resolution == impl->shadow_size)
			return;
		//等尽在途帧，避免重建期间仍被采样
		if (!impl->frame.frames_wait())
		{
			logger.error("渲染器:: 阴影分辨率切换失败：等待在途帧失败");
			return;
		}
		//旧阴影图视图（失败回绑用）
		VkImage old_image = VK_NULL_HANDLE;
		VkImageView old_view = VK_NULL_HANDLE;
		VkImageView old_depth_view = VK_NULL_HANDLE;
		VkExtent2D old_extent{};
		if (!impl->render_target.target_get(impl->shadow_target, old_image, old_view,
			old_depth_view, old_extent))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：旧阴影图查询失败");
			return;
		}
		//先建新目标（失败则旧目标原样保留）；颜色格式固定 UNORM，理由见 init ⑳ ①
		uint64_t candidate = 0;
		if (!impl->render_target.target_create(candidate, resolution, resolution,
			VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get()))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影目标创建失败（保持 {}）", impl->shadow_size);
			return;
		}
		//取新目标颜色视图与默认采样器
		VkImage new_image = VK_NULL_HANDLE;
		VkImageView new_view = VK_NULL_HANDLE;
		VkImageView new_depth_view = VK_NULL_HANDLE;
		VkExtent2D new_extent{};
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->render_target.target_get(candidate, new_image, new_view, new_depth_view, new_extent)
			|| !impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影图查询失败");
			impl->render_target.target_destroy(candidate);
			return;
		}
		//逐帧槽改绑新阴影图
		for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
		{
			//若某槽绑定失败，则把已改绑的槽全部回绑旧图并放弃新目标
			if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, new_view))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 绑定失败，回绑旧阴影图", slot);
				for (std::size_t revert = 0; revert <= slot; ++revert)
					impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
				impl->render_target.target_destroy(candidate);
				return;
			}
		}
		//全部改绑成功后销毁旧目标并更新状态
		impl->render_target.target_destroy(impl->shadow_target);
		impl->shadow_target = candidate;
		impl->shadow_size = resolution;
		//新目标尚无有效内容，下一帧必须重绘阴影图
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影图分辨率已切换为 {}", resolution);
	}

	//当前阴影图分辨率（第五十四轮）
	uint32_t Renderer::shadow_resolution(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_size : Impl::SHADOW_SIZE;
	}

	//累计实际执行的阴影 pass 次数（第五十五轮）
	uint64_t Renderer::shadow_pass_count(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->shadow_pass_count : 0;
	}

	//累计场景 UBO 上传字节数（第六十轮新增）
	uint64_t Renderer::scene_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->scene_upload_bytes : 0;
	}

	//PBR-lite 镜面开关（第六十四轮新增）
	void Renderer::pbr_enable_set(bool enabled)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->pbr_enabled = enabled;
	}

	//PBR-lite 是否启用（第六十四轮新增）
	bool Renderer::pbr_enabled(void) const
	{
		//未初始化返回默认假
		return impl != nullptr && impl->initialized ? impl->pbr_enabled : false;
	}

	//PBR-lite 粗糙度缩放（第六十四轮新增）
	void Renderer::pbr_roughness_set(float scale)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//合法区间 0.05..4；越界或非数整次忽略
		if (!(scale >= 0.05f && scale <= 4.0f))
		{
			logger.warn("渲染器:: PBR 粗糙度缩放 {} 越界（0.05..4），已忽略", scale);
			return;
		}
		impl->pbr_roughness = scale;
	}

	//当前 PBR-lite 粗糙度缩放（第六十四轮新增）
	float Renderer::pbr_roughness(void) const
	{
		//未初始化返回默认 1
		return impl != nullptr && impl->initialized ? impl->pbr_roughness : 1.0f;
	}

	//PBR 金属度（第六十五轮新增）
	void Renderer::pbr_metallic_set(float metallic)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//合法区间 0..1；越界或非数整次忽略
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: PBR 金属度 {} 越界（0..1），已忽略", metallic);
			return;
		}
		impl->pbr_metallic = metallic;
	}

	//当前 PBR 金属度（第六十五轮新增）
	float Renderer::pbr_metallic(void) const
	{
		//未初始化返回默认 0
		return impl != nullptr && impl->initialized ? impl->pbr_metallic : 0.0f;
	}

	//累计实例缓冲上传字节数（第六十一轮新增）
	uint64_t Renderer::instance_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->instance_upload_bytes : 0;
	}

	//读回阴影图（第五十七轮新增）
	bool Renderer::shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out)
	{
		//前置：已初始化、阴影已开启、阴影目标存在且参数齐备
		if (impl == nullptr || !impl->initialized || !impl->shadow_enabled || impl->shadow_target == 0
			|| pixels == nullptr || capacity == 0)
			return false;
		//等尽在途帧，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回打包深度）
		return impl->render_target.target_readback(impl->shadow_target, pixels, capacity, width_out, height_out);
	}

	//设置清屏色（RGBA）
	void Renderer::clear_color_set(const float color[4])
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		逐分量钳制到 [0,1] 后拷贝（自下一次 frame_render 起生效）
		越界与非数（NaN）均被归一：负 / 非数 → 0，大于 1 → 1；故清屏色恒落在合法色域内。
		*/
		for (std::size_t index = 0; index < 4; ++index)
		{
			//当前分量
			float value = color[index];
			//若为负或非数则归 0
			if (!(value >= 0.0f))
				value = 0.0f;
			//若大于 1 则归 1
			else if (value > 1.0f)
				value = 1.0f;
			impl->clear_color[index] = value;
		}
	}

	//UI 通道
	Ui_Channel_Context* Renderer::ui_channel(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return nullptr;
		return &impl->ui_channel;
	}
}
