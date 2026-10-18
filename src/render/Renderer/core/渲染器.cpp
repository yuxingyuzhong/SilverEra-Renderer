#include "src/render/Renderer/局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::nothrow
#include <new>

//游戏引擎命名空间
namespace engine
{
	//渲染层版本查询
	string renderer_version(void)
	{
		//渲染层版本号
		return "0.1.0";
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
		//⑧ 帧
		if (!impl->frame.build(impl->device.device_get(), impl->device.physical_device_get(),
			impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->device.present_family_get(), impl->device.present_queue_get(),
			impl->swapchain, effective.frame_overlap))
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
		//⑫ 管线：图形管线
		if (!impl->pipeline.pipeline_build(impl->vert_module, impl->frag_module))
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
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途）
			if (!impl->memory.buffer_create(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], 64,
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
			//把该帧槽 set0 绑定到对应 UBO
			if (!impl->pipeline.scene_bind(slot, impl->scene_ubos[slot]))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
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
		//⑯ 相机：设置宽高比（取当前交换链尺寸）
		{
			VkExtent2D extent = impl->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				impl->camera.aspect_set(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		/*
		⑰ UI 通道
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
		impl->initialized = true;
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
		impl->frame.destroy();
		//网格缓冲依赖内存模块，须先于 memory 销毁
		impl->mesh.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		impl->texture.destroy();
		//场景 UBO 依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->scene_ubos.size(); ++slot)
				//归还场景 UBO 与其内存
				impl->memory.buffer_destroy(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot]);
		impl->scene_ubos.clear();
		impl->scene_ubo_allocs.clear();
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
		impl->pending_transforms.clear();
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

	//每帧推进
	bool Renderer::frame_render(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;

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
			impl->pending_transforms.clear();
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
				//若重建失败仅告警，本帧继续
				if (!impl->swapchain.rebuild(width, height))
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
		//写入该帧槽 UBO
		if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], view_proj, 64, 0))
		{
			logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
			return false;
		}
		/*
		本帧槽 set0 无需重写
		各帧槽的场景 UBO 缓冲句柄自 init 起恒定不变，set0 已在 init 时一次性写入；
		每帧只更新 UBO 内容（上一段的 buffer_write），不重写描述符集。
		*/
		//为本帧待绘制项填装描述符集
		for (auto& item : impl->pending_items)
		{
			//本帧槽的场景 UBO 描述符集（set0）
			item.scene_set = impl->pipeline.scene_set_get(slot);
			//本帧槽的采样器描述符集（set1）
			item.sampler_set = impl->pipeline.sampler_set_get(slot);
			item.descriptor_set_count = 2;
		}

		//交给帧模块走取图像 / 录制 / 提交 / 呈现
		const float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
		bool rebuild_needed = false;
		if (!impl->frame.render(impl->swapchain, impl->pending_items.data(),
			static_cast<uint32_t>(impl->pending_items.size()), rebuild_needed, clear_color,
			impl->ui_channel.draw_callback_get()))
		{
			logger.error("渲染器:: 帧渲染失败");
			return false;
		}
		//消费完毕，清空待绘制项与其推送常量存储（下一帧由 mesh_draw 重新登记）
		impl->pending_items.clear();
		impl->pending_transforms.clear();
		//若帧链路要求重建且本帧尚未重建
		if (rebuild_needed && !rebuilt)
		{
			//尺寸有效时重建交换链
			if (width != 0 && height != 0)
			{
				impl->swapchain.rebuild(width, height);
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

	//加载网格
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
		//转交网格模块销毁
		impl->mesh.mesh_destroy(handle);
	}

	//绘制网格
	bool Renderer::mesh_draw(Mesh_Handle handle, const float* transform)
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
		推送常量：列主序 16 浮点模型矩阵
		transform 为空则用单位矩阵；每次调用追加一块独立存储，
		供 pending_items 中的指针指向，直至下一帧 frame_render 消费完毕。
		*/
		impl->pending_transforms.emplace_back();
		std::array<float, 16>& transform_store = impl->pending_transforms.back();
		if (transform == nullptr)
		{
			//单位矩阵（列主序）
			const float identity[16] = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			};
			for (uint32_t index = 0; index < 16; ++index)
				transform_store[index] = identity[index];
		}
		else
		{
			//拷贝外部模型矩阵
			for (uint32_t index = 0; index < 16; ++index)
				transform_store[index] = transform[index];
		}

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
		item.push_constants = transform_store.data();
		item.push_constant_size = 64;

		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
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
		//转交纹理模块销毁
		impl->texture.texture_destroy(handle);
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
		//把目标纹理写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, sampler, view);
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

	//UI 通道
	Ui_Channel_Context* Renderer::ui_channel(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return nullptr;
		return &impl->ui_channel;
	}
}
