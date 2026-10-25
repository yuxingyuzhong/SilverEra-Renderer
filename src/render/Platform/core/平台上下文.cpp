#include "src/render/Platform/局部命名空间使用.h"
//获取日志系统
#include "Engine/EngineCore/src/tools/Logging/日志系统运行包.h"

namespace engine
{
	//析构
	Platform_Context::~Platform_Context(void)
	{
		//释放已创建的平台对象
		destroy();
	}

	//构建
	bool Platform_Context::build(uint32_t width, uint32_t height, const string& title, bool hidden)
	{
		//若窗口创建失败
		if (!window_create(width, height, title, hidden))
		{
			logger.error("平台上下文:: 窗口创建失败，构建中止");
			//逆序回收已建部分（窗口创建内部已自清理，此处兜底）
			destroy();
			return false;
		}

		//记录构建完成
		logger.info("平台上下文:: 平台基础设施构建完成");
		return true;
	}

	//销毁
	void Platform_Context::destroy(void)
	{
		//若表面已创建（必须在窗口销毁之前销毁表面）
		if (surface != VK_NULL_HANDLE)
		{
			logger.debug("平台上下文:: 销毁 Vulkan 表面");
			//销毁表面需要创建时的实例句柄
			vkDestroySurfaceKHR(instance, surface, nullptr);
			surface = VK_NULL_HANDLE;
		}
		//清除实例记录
		instance = VK_NULL_HANDLE;
		//销毁窗口与视频子系统
		window_destroy();
		//复位事件标志
		close_flag = false;
		resize_flag = false;
		minimize_flag = false;
	}

	//是否已构建完成
	bool Platform_Context::valid(void) const
	{
		return window != nullptr;
	}

	//窗口是否请求退出
	bool Platform_Context::close_requested(void) const
	{
		return close_flag;
	}

	//取走并清空「窗口尺寸已变化，交换链待重建」标志
	bool Platform_Context::resized_consume(void)
	{
		//取走标志
		bool resized = resize_flag;
		//清空标志（一次性消费，避免重复重建）
		resize_flag = false;
		return resized;
	}

	//窗口是否处于最小化（挂起）
	bool Platform_Context::minimized(void) const
	{
		return minimize_flag;
	}

	//取窗口客户区尺寸（像素）
	void Platform_Context::window_size_get(uint32_t& width_out, uint32_t& height_out) const
	{
		//窗口尺寸（SDL 以 int 返回，单位为像素）
		int width = 0;
		int height = 0;
		//若窗口已创建且像素尺寸查询成功
		if (window != nullptr && SDL_GetWindowSizeInPixels(window, &width, &height))
		{
			width_out = static_cast<uint32_t>(width);
			height_out = static_cast<uint32_t>(height);
			return;
		}
		//未创建或查询失败：回退 0×0（上层据此挂起，不重建 0 尺寸交换链）
		width_out = 0;
		height_out = 0;
	}

	//请求改变窗口客户区尺寸（像素）
	bool Platform_Context::window_size_set(uint32_t width, uint32_t height)
	{
		//前置：窗口必须已创建
		if (window == nullptr)
		{
			logger.error("平台上下文:: 窗口尚未创建，无法改变尺寸");
			return false;
		}
		//前置：宽高不得为零
		if (width == 0 || height == 0)
		{
			logger.warn("平台上下文:: 请求尺寸为零，忽略（K7）");
			return false;
		}

		//请求 SDL 改变窗口尺寸（SDL 以 int 接收，单位为像素）
		if (!SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height)))
		{
			logger.error("平台上下文:: 窗口尺寸设置失败：{}", SDL_GetError());
			return false;
		}
		//同步置「交换链待重建」标志（隐藏窗口下 SDL 不一定派发尺寸变化事件）
		resize_flag = true;
		return true;
	}

	//全屏切换（第一百一十一轮）
	bool Platform_Context::fullscreen_set(bool fullscreen)
	{
		//前置：窗口必须已创建
		if (window == nullptr)
		{
			logger.error("平台上下文:: 窗口尚未创建，无法切换全屏");
			return false;
		}
		//请求 SDL 切换全屏
		if (!SDL_SetWindowFullscreen(window, fullscreen))
		{
			logger.warn("平台上下文:: 全屏切换失败：{}", SDL_GetError());
			return false;
		}
		//全屏切换通常伴随客户区尺寸变化，显式置重建标志
		resize_flag = true;
		return true;
	}

	//当前是否处于全屏（第一百一十一轮）
	bool Platform_Context::fullscreen_get(void) const
	{
		//窗口未创建视为否
		if (window == nullptr)
			return false;
		//读取窗口标志
		return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	}

	//窗口显示缩放比例（第一百一十二轮）
	float Platform_Context::display_scale_get(void) const
	{
		//窗口未创建则按 1 处理
		if (window == nullptr)
			return 1.0f;
		//查询显示缩放（非正值防御为 1）
		float scale = SDL_GetWindowDisplayScale(window);
		return scale > 0.0f ? scale : 1.0f;
	}

	//获取原生窗口句柄
	SDL_Window* Platform_Context::window_get(void) const
	{
		return window;
	}

	//获取表面句柄
	VkSurfaceKHR Platform_Context::surface_get(void) const
	{
		return surface;
	}
}
