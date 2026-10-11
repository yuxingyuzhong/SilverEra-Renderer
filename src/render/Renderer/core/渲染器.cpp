#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"
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
		//⑦ 交换链
		if (!impl->swapchain.build(impl->device.physical_device_get(), impl->device.device_get(),
			impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
			swapchain_width, swapchain_height, effective.vsync))
		{
			logger.error("渲染器:: 交换链创建失败");
			shutdown();
			return false;
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
		//严格逆序销毁（各模块 destroy 幂等，可安全处理半初始化状态；§5.4）
		impl->frame.destroy();
		impl->swapchain.destroy();
		impl->memory.destroy();
		impl->device.destroy();
		impl->platform.destroy();
		//复位状态
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
		//② 若窗口请求退出则交调用方收敛
		if (impl->platform.close_requested())
			return true;
		//③ 若窗口最小化则挂起本帧（避免 0×0 交换链；K7）
		if (impl->platform.minimized())
			return true;

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
				rebuilt = true;
			}
		}

		//⑤ 交给帧模块走取图像 / 录制 / 提交 / 呈现
		//本阶段清屏色为固定常量，Phase 6 起由管线决定
		const float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
		bool rebuild_needed = false;
		if (!impl->frame.render(impl->swapchain, rebuild_needed, clear_color))
		{
			logger.error("渲染器:: 帧渲染失败");
			return false;
		}
		//若帧链路要求重建且本帧尚未重建
		if (rebuild_needed && !rebuilt)
		{
			//尺寸有效时重建交换链
			if (width != 0 && height != 0)
				impl->swapchain.rebuild(width, height);
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
}
