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
		//⑫ 管线 / 描述符 / 采样 / 粒子 / 文字 / 阴影 / 后处理 / 天空 / 阴影资源
		if (!renderer_init_graphics(self_ptr, impl.get(), effective))
			return false;
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
