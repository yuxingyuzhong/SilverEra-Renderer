#include "../局部命名空间使用.h"
//获取日志系统
#include "Engine/EngineFoundation/src/log/日志系统运行包.h"

namespace engine
{
	//创建窗口
	bool Platform_Context::window_create(uint32_t width, uint32_t height, const string& title, bool hidden)
	{
		/*
		关于 SDL_SetMainReady
		本层是静态库、不接管入口点（不使用 SDL_main）。SDL 仅在 SDL_MAIN_NEEDED
		平台（如 GDK）下要求初始化前调用 SDL_SetMainReady；Windows 属
		SDL_MAIN_AVAILABLE，SDL_MainIsReady 默认为真，故此处无需调用，也避免
		引入 SDL_main.h（其会把 main 重定义为 SDL_main）。K18 由此实测确认。
		*/
		//初始化视频子系统（SDL3 子系统按引用计数，必须与 SDL_QuitSubSystem 成对）
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			logger.error("平台上下文:: SDL 视频子系统初始化失败：{}", SDL_GetError());
			return false;
		}
		//记录视频子系统已初始化
		video_initialized = true;

		/*
		窗口创建标志
		SDL_WINDOW_VULKAN 为 Vulkan 表面必需项；隐藏窗口再附加 SDL_WINDOW_HIDDEN
		（无头用例仍创建真实窗口与表面，只是不显示）。
		*/
		SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
		//可见窗口允许用户拖拽调整尺寸（事件泵已置 resize 标志、帧链路会重建交换链）
		if (!hidden)
			flags |= SDL_WINDOW_RESIZABLE;
		//若需隐藏窗口
		if (hidden)
			flags |= SDL_WINDOW_HIDDEN;

		//创建窗口（标题按 UTF-8 传入，SDL3 直接接收 UTF-8 字符串）
		window = SDL_CreateWindow(title.c_str(), static_cast<int>(width), static_cast<int>(height), flags);
		//若窗口创建失败
		if (window == nullptr)
		{
			logger.error("平台上下文:: 窗口创建失败：{}", SDL_GetError());
			//逆序回收：窗口未建成，只需退出视频子系统（不留半初始化状态）
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			video_initialized = false;
			return false;
		}

		logger.info("平台上下文:: 窗口已创建（{} × {}）", width, height);
		return true;
	}

	//销毁窗口与视频子系统
	void Platform_Context::window_destroy(void)
	{
		//若窗口已创建
		if (window != nullptr)
		{
			logger.debug("平台上下文:: 销毁窗口");
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		//若视频子系统已初始化
		if (video_initialized)
		{
			logger.debug("平台上下文:: 退出 SDL 视频子系统");
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			video_initialized = false;
		}
	}
}