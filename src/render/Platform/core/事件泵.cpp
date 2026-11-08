#include "../局部命名空间使用.h"

namespace engine
{
	/*
	轮询 SDL 事件
	逐个取出事件队列，只置平台标志，不做任何 Vulkan 操作：
	交换链重建（K8 要求每帧最多一次）与最小化挂起由上层帧链统一决策。
	*/
	void Platform_Context::events_pump(void)
	{
		//事件对象（循环复用）
		SDL_Event event;
		//持续取出队列中的全部事件
		while (SDL_PollEvent(&event))
		{
			//若已注册事件转发回调则逐个事件转发（供上层平台后端处理输入）
			if (event_callback != nullptr)
				event_callback(&event, event_callback_data);
			switch (event.type)
			{
			//若用户请求退出
			case SDL_EVENT_QUIT:
				close_flag = true;
				break;
			//若窗口尺寸变化或像素尺寸变化（HiDPI 跨不同 DPI 显示器；仅置标志，交换链重建推迟到帧链路）
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				resize_flag = true;
				break;
			//若窗口被最小化
			case SDL_EVENT_WINDOW_MINIMIZED:
				minimize_flag = true;
				break;
			//若窗口从最小化恢复
			case SDL_EVENT_WINDOW_RESTORED:
				minimize_flag = false;
				break;
			default:
				break;
			}
		}
	}

	//设置 SDL 事件转发回调
	void Platform_Context::event_forward_set(void (*callback)(const SDL_Event* event, void* user_data), void* user_data)
	{
		//记录回调与用户数据
		event_callback = callback;
		event_callback_data = user_data;
	}

	//清除 SDL 事件转发回调
	void Platform_Context::event_forward_clear(void)
	{
		//置空回调与用户数据
		event_callback = nullptr;
		event_callback_data = nullptr;
	}
}