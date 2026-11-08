#include "../局部命名空间使用.h"
//系统输入桥声明
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
/*
SDL3 头
本实现需要扫描码常量做翻译；本层私有包含根已放开 SDL3 头，公共头不暴露。
*/
#include <SDL3/SDL.h>

namespace engine
{
	namespace input_bridge
	{
		//SDL 扫描码 → 系统中立键位
		Input_Key key_of_scancode(uint32_t scancode)
		{
			//字母 A~Z（SDL 连续区段，按差值从 INPUT_KEY_A 递推）
			if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
				return static_cast<Input_Key>(INPUT_KEY_A + (scancode - SDL_SCANCODE_A));
			//数字 1~9（同理从 INPUT_KEY_1 递推）
			if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
				return static_cast<Input_Key>(INPUT_KEY_1 + (scancode - SDL_SCANCODE_1));
			//功能键 F1~F12
			if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
				return static_cast<Input_Key>(INPUT_KEY_F1 + (scancode - SDL_SCANCODE_F1));
			//小键盘数字 1~9（小键盘 0 不在连续区段，单独列出）
			if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
				return static_cast<Input_Key>(INPUT_KEY_KP_1 + (scancode - SDL_SCANCODE_KP_1));

			switch (scancode)
			{
			//数字 0
			case SDL_SCANCODE_0: return INPUT_KEY_0;
			//主键盘符号
			case SDL_SCANCODE_GRAVE: return INPUT_KEY_GRAVE;
			case SDL_SCANCODE_MINUS: return INPUT_KEY_MINUS;
			case SDL_SCANCODE_EQUALS: return INPUT_KEY_EQUALS;
			case SDL_SCANCODE_LEFTBRACKET: return INPUT_KEY_LEFT_BRACKET;
			case SDL_SCANCODE_RIGHTBRACKET: return INPUT_KEY_RIGHT_BRACKET;
			case SDL_SCANCODE_BACKSLASH: return INPUT_KEY_BACKSLASH;
			//非美式键盘的 # 键与反斜杠同物理位置
			case SDL_SCANCODE_NONUSHASH: return INPUT_KEY_BACKSLASH;
			case SDL_SCANCODE_SEMICOLON: return INPUT_KEY_SEMICOLON;
			case SDL_SCANCODE_APOSTROPHE: return INPUT_KEY_APOSTROPHE;
			case SDL_SCANCODE_COMMA: return INPUT_KEY_COMMA;
			case SDL_SCANCODE_PERIOD: return INPUT_KEY_PERIOD;
			case SDL_SCANCODE_SLASH: return INPUT_KEY_SLASH;
			//控制 / 导航键
			case SDL_SCANCODE_ESCAPE: return INPUT_KEY_ESCAPE;
			case SDL_SCANCODE_RETURN: return INPUT_KEY_ENTER;
			case SDL_SCANCODE_SPACE: return INPUT_KEY_SPACE;
			case SDL_SCANCODE_TAB: return INPUT_KEY_TAB;
			case SDL_SCANCODE_BACKSPACE: return INPUT_KEY_BACKSPACE;
			case SDL_SCANCODE_LEFT: return INPUT_KEY_LEFT;
			case SDL_SCANCODE_RIGHT: return INPUT_KEY_RIGHT;
			case SDL_SCANCODE_UP: return INPUT_KEY_UP;
			case SDL_SCANCODE_DOWN: return INPUT_KEY_DOWN;
			case SDL_SCANCODE_INSERT: return INPUT_KEY_INSERT;
			case SDL_SCANCODE_DELETE: return INPUT_KEY_DELETE;
			case SDL_SCANCODE_HOME: return INPUT_KEY_HOME;
			case SDL_SCANCODE_END: return INPUT_KEY_END;
			case SDL_SCANCODE_PAGEUP: return INPUT_KEY_PAGE_UP;
			case SDL_SCANCODE_PAGEDOWN: return INPUT_KEY_PAGE_DOWN;
			case SDL_SCANCODE_CAPSLOCK: return INPUT_KEY_CAPS_LOCK;
			case SDL_SCANCODE_PRINTSCREEN: return INPUT_KEY_PRINT_SCREEN;
			case SDL_SCANCODE_SCROLLLOCK: return INPUT_KEY_SCROLL_LOCK;
			case SDL_SCANCODE_PAUSE: return INPUT_KEY_PAUSE;
			case SDL_SCANCODE_NUMLOCKCLEAR: return INPUT_KEY_NUM_LOCK;
			//应用键菜单与独立菜单键同物理语义（两者并为一个中立键）
			case SDL_SCANCODE_APPLICATION: return INPUT_KEY_MENU;
			case SDL_SCANCODE_MENU: return INPUT_KEY_MENU;
			//修饰键（左右可区分）
			case SDL_SCANCODE_LSHIFT: return INPUT_KEY_LSHIFT;
			case SDL_SCANCODE_RSHIFT: return INPUT_KEY_RSHIFT;
			case SDL_SCANCODE_LCTRL: return INPUT_KEY_LCTRL;
			case SDL_SCANCODE_RCTRL: return INPUT_KEY_RCTRL;
			case SDL_SCANCODE_LALT: return INPUT_KEY_LALT;
			case SDL_SCANCODE_RALT: return INPUT_KEY_RALT;
			case SDL_SCANCODE_LGUI: return INPUT_KEY_LGUI;
			case SDL_SCANCODE_RGUI: return INPUT_KEY_RGUI;
			//小键盘
			case SDL_SCANCODE_KP_0: return INPUT_KEY_KP_0;
			case SDL_SCANCODE_KP_PERIOD: return INPUT_KEY_KP_DECIMAL;
			case SDL_SCANCODE_KP_DIVIDE: return INPUT_KEY_KP_DIVIDE;
			case SDL_SCANCODE_KP_MULTIPLY: return INPUT_KEY_KP_MULTIPLY;
			case SDL_SCANCODE_KP_MINUS: return INPUT_KEY_KP_MINUS;
			case SDL_SCANCODE_KP_PLUS: return INPUT_KEY_KP_PLUS;
			case SDL_SCANCODE_KP_ENTER: return INPUT_KEY_KP_ENTER;
			default:
				return 0;
			}
		}

		//SDL 鼠标按键 → 系统中立键位
		Input_Key key_of_mouse_button(uint32_t button)
		{
			//SDL 按键自 1 起，合法范围 1..5
			switch (button)
			{
			case 1: return INPUT_KEY_MOUSE_LEFT;
			case 2: return INPUT_KEY_MOUSE_MIDDLE;
			case 3: return INPUT_KEY_MOUSE_RIGHT;
			case 4: return INPUT_KEY_MOUSE_X1;
			case 5: return INPUT_KEY_MOUSE_X2;
			default: return 0;
			}
		}

		//滚轮方向 → 系统中立键位
		Input_Key key_of_wheel(bool up)
		{
			return up ? INPUT_KEY_WHEEL_UP : INPUT_KEY_WHEEL_DOWN;
		}

		//构造 Input/Raw 载荷
		bool raw_build(const Input_Frame& frame, bool text_mode, nlohmann::json& payload_out)
		{
			//载荷对象
			nlohmann::json payload = nlohmann::json::object();
			payload[input_event::field_frame] = frame.frame;
			payload[input_event::field_dt] = frame.dt;

			//键位边沿集合
			nlohmann::json keys = nlohmann::json::array();
			//追加一条边沿（无法翻译的键位直接丢弃）
			auto push_edge = [&keys](const Input_Key& key, bool pressed, bool repeat) -> void
				{
					if (!input_key_valid(key))
						return;
					nlohmann::json edge = nlohmann::json::object();
					edge[input_event::field_key] = key;
					edge[input_event::field_pressed] = pressed;
					//长按重复沿：是否产出语义命令由系统侧按绑定开关决定
					if (repeat)
						edge[input_event::field_repeat] = true;
					keys.push_back(edge);
				};

			//键盘按下 / 抬起沿
			for (const auto& scancode : frame.key_pressed)
				push_edge(key_of_scancode(scancode), true, false);
			for (const auto& scancode : frame.key_released)
				push_edge(key_of_scancode(scancode), false, false);
			//键盘长按自动重复沿
			for (const auto& scancode : frame.key_repeat)
				push_edge(key_of_scancode(scancode), true, true);
			//鼠标按键边沿
			for (uint32_t index = 0; index < INPUT_MOUSE_BUTTON_COUNT; ++index)
			{
				//SDL 按键编号自 1 起
				const Input_Key key = key_of_mouse_button(index + 1);
				if (frame.pointer.button_pressed[index])
					push_edge(key, true, false);
				if (frame.pointer.button_released[index])
					push_edge(key, false, false);
			}
			//滚轮：每齿一对按下 / 抬起边沿（避免污染系统侧按住集合）
			for (uint32_t tick = 0; tick < frame.pointer.wheel_up_ticks; ++tick)
			{
				push_edge(key_of_wheel(true), true, false);
				push_edge(key_of_wheel(true), false, false);
			}
			for (uint32_t tick = 0; tick < frame.pointer.wheel_down_ticks; ++tick)
			{
				push_edge(key_of_wheel(false), true, false);
				push_edge(key_of_wheel(false), false, false);
			}
			payload[input_event::field_keys] = std::move(keys);

			//权威按住快照（键盘 + 鼠标按键）
			nlohmann::json held = nlohmann::json::array();
			for (uint32_t scancode = 0; scancode < INPUT_KEY_CAPACITY; ++scancode)
			{
				//未按住的键跳过
				if (!frame.key_held(scancode))
					continue;
				const Input_Key key = key_of_scancode(scancode);
				if (input_key_valid(key))
					held.push_back(key);
			}
			for (uint32_t index = 0; index < INPUT_MOUSE_BUTTON_COUNT; ++index)
			{
				if (!frame.pointer.button_down[index])
					continue;
				const Input_Key key = key_of_mouse_button(index + 1);
				if (input_key_valid(key))
					held.push_back(key);
			}
			payload[input_event::field_held] = std::move(held);

			//指针
			nlohmann::json pointer = nlohmann::json::object();
			pointer["x"] = frame.pointer.x;
			pointer["y"] = frame.pointer.y;
			pointer["dx"] = frame.pointer.dx;
			pointer["dy"] = frame.pointer.dy;
			pointer["wheel_x"] = frame.pointer.wheel_x;
			pointer["wheel_y"] = frame.pointer.wheel_y;
			payload[input_event::field_pointer] = std::move(pointer);

			//文本模式（UI 文本框获焦时挂起系统侧键位映射）
			if (text_mode)
				payload[input_event::field_text_mode] = true;

			payload_out = std::move(payload);
			return true;
		}
	}
}
