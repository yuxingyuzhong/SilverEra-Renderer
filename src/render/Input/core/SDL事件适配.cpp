#include "../局部命名空间使用.h"

namespace engine
{
	//析构（若文本输入仍启用则关闭平台文本输入）
	Input_Aggregator::~Input_Aggregator(void)
	{
		//关闭平台文本输入
		if (text_active && window != nullptr)
			SDL_StopTextInput(window);
	}

	//绑定平台窗口
	void Input_Aggregator::window_set(SDL_Window* window)
	{
		this->window = window;
	}

	//喂入单个 SDL 事件
	void Input_Aggregator::event_feed(const SDL_Event* event)
	{
		//若事件为空
		if (event == nullptr)
			return;

		switch (event->type)
		{
		//若为键盘按下
		case SDL_EVENT_KEY_DOWN:
		{
			//键位编码
			const Input_Key_Code code = static_cast<Input_Key_Code>(event->key.scancode);
			//越界忽略
			if (code >= INPUT_KEY_CAPACITY)
				break;
			//刷新修饰键
			frame.modifiers.ctrl = (event->key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)) != 0;
			frame.modifiers.shift = (event->key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)) != 0;
			frame.modifiers.alt = (event->key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) != 0;
			frame.modifiers.gui = (event->key.mod & (SDL_KMOD_LGUI | SDL_KMOD_RGUI)) != 0;
			//若为重复按下
			if (event->key.repeat)
			{
				//登记重复沿（按住状态不重复置位）
				pending_repeat.push_back(code);
				break;
			}
			//若已按住则忽略重复置位
			if (keys_held[code] != 0)
				break;
			//登记按住与按下沿
			keys_held[code] = 1;
			pending_pressed.push_back(code);
			break;
		}
		//若为键盘抬起
		case SDL_EVENT_KEY_UP:
		{
			//键位编码
			const Input_Key_Code code = static_cast<Input_Key_Code>(event->key.scancode);
			//越界忽略
			if (code >= INPUT_KEY_CAPACITY)
				break;
			//刷新修饰键
			frame.modifiers.ctrl = (event->key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)) != 0;
			frame.modifiers.shift = (event->key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)) != 0;
			frame.modifiers.alt = (event->key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) != 0;
			frame.modifiers.gui = (event->key.mod & (SDL_KMOD_LGUI | SDL_KMOD_RGUI)) != 0;
			//若本就未按住则忽略
			if (keys_held[code] == 0)
				break;
			//清除按住并登记抬起沿
			keys_held[code] = 0;
			pending_released.push_back(code);
			break;
		}
		//若为指针移动
		case SDL_EVENT_MOUSE_MOTION:
			//更新当前位置与本帧位移增量
			frame.pointer.x = event->motion.x;
			frame.pointer.y = event->motion.y;
			pending_dx += event->motion.xrel;
			pending_dy += event->motion.yrel;
			break;
		//若为鼠标按键按下
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		{
			//按键下标（SDL 从 1 起，映射到 0..4）
			const int index = static_cast<int>(event->button.button) - 1;
			//越界忽略
			if (index < 0 || index >= static_cast<int>(INPUT_MOUSE_BUTTON_COUNT))
				break;
			//更新位置
			frame.pointer.x = event->button.x;
			frame.pointer.y = event->button.y;
			//若已按住则忽略
			if (buttons_held[index])
				break;
			//登记按住与按下沿
			buttons_held[index] = true;
			pending_button_pressed[index] = true;
			break;
		}
		//若为鼠标按键抬起
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			//按键下标
			const int index = static_cast<int>(event->button.button) - 1;
			//越界忽略
			if (index < 0 || index >= static_cast<int>(INPUT_MOUSE_BUTTON_COUNT))
				break;
			//更新位置
			frame.pointer.x = event->button.x;
			frame.pointer.y = event->button.y;
			//若本就未按住则忽略
			if (!buttons_held[index])
				break;
			//清除按住并登记抬起沿
			buttons_held[index] = false;
			pending_button_released[index] = true;
			break;
		}
		//若为滚轮
		case SDL_EVENT_MOUSE_WHEEL:
			//累积滚轮增量
			pending_wheel_x += event->wheel.x;
			pending_wheel_y += event->wheel.y;
			//按方向累计齿数（每个事件按一齿计）
			if (event->wheel.y > 0.0f)
				++pending_wheel_up_ticks;
			else if (event->wheel.y < 0.0f)
				++pending_wheel_down_ticks;
			break;
		//若为文本提交
		case SDL_EVENT_TEXT_INPUT:
			//若文本非空则累积
			if (event->text.text != nullptr)
				pending_committed += event->text.text;
			break;
		//若为文本预编辑
		case SDL_EVENT_TEXT_EDITING:
			//记录预编辑串
			pending_preedit = event->edit.text != nullptr ? event->edit.text : "";
			pending_preedit_changed = true;
			break;
		//若窗口获得焦点
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			frame.window_focused = true;
			break;
		//若窗口失去焦点
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			frame.window_focused = false;
			break;
		//若窗口尺寸变化
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			//事件数据 1 / 2 为新尺寸
			if (event->window.data1 > 0 && event->window.data2 > 0)
			{
				pending_window_width = static_cast<uint32_t>(event->window.data1);
				pending_window_height = static_cast<uint32_t>(event->window.data2);
				pending_window_valid = true;
			}
			break;
		default:
			break;
		}
	}

	//启用 / 关闭平台文本输入
	void Input_Aggregator::text_input_enable(bool enabled)
	{
		//若状态未变化
		if (enabled == text_active)
			return;
		//更新状态并启停平台文本输入
		text_active = enabled;
		frame.text.active = enabled;
		//未绑定窗口时只记录状态，不调用平台接口
		if (window != nullptr)
		{
			if (enabled)
				SDL_StartTextInput(window);
			else
				SDL_StopTextInput(window);
		}
		//关闭时清空预编辑串
		if (!enabled)
		{
			pending_preedit.clear();
			pending_preedit_changed = true;
		}
	}

	//平台文本输入是否启用
	bool Input_Aggregator::text_input_enabled(void) const
	{
		return text_active;
	}
}
