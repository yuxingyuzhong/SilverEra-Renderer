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
			switch (event.type)
			{
			//若用户请求退出
			case SDL_EVENT_QUIT:
				close_flag = true;
				break;
			//若窗口尺寸变化（仅置标志，交换链重建推迟到帧链路）
			case SDL_EVENT_WINDOW_RESIZED:
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
}
