#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	/*
	帧输入快照（渲染层自持原始输入）
	本头保持零 SDL / Vulkan 类型：层外只需读取本结构，事件喂入口见公共扩展头 输入聚合.h。
	键位编码与 SDL_Scancode 数值一致（渲染层内部直接沿用，不再另立键名表）。
	*/
	using Input_Key_Code = uint32_t;
	//键位状态容量（SDL3 扫描码上限 512 以内）
	inline constexpr uint32_t INPUT_KEY_CAPACITY = 512;
	//鼠标按键数（左 / 中 / 右 / X1 / X2）
	inline constexpr uint32_t INPUT_MOUSE_BUTTON_COUNT = 5;

	/*
	指针状态
	x / y 为窗口客户区像素坐标（左上原点、Y 向下）；dx / dy 为本帧位移增量；
	wheel_x / wheel_y 为本帧滚轮增量（刻度）；按键状态按数组下标 0..4。
	*/
	struct Input_Pointer_State
	{
		//当前位置
		float x = 0.0f;
		float y = 0.0f;
		//本帧位移
		float dx = 0.0f;
		float dy = 0.0f;
		//本帧滚轮
		float wheel_x = 0.0f;
		float wheel_y = 0.0f;
		//本帧滚轮齿数（向上 / 向下）
		uint32_t wheel_up_ticks = 0;
		uint32_t wheel_down_ticks = 0;
		//按住状态
		bool button_down[INPUT_MOUSE_BUTTON_COUNT] = {};
		//本帧按下沿
		bool button_pressed[INPUT_MOUSE_BUTTON_COUNT] = {};
		//本帧抬起沿
		bool button_released[INPUT_MOUSE_BUTTON_COUNT] = {};
	};

	//修饰键状态（左右合并）
	struct Input_Modifier_State
	{
		bool ctrl = false;
		bool shift = false;
		bool alt = false;
		bool gui = false;
	};

	/*
	文本状态
	committed 为本帧提交的 UTF-8 文本（SDL_EVENT_TEXT_INPUT 累积）；
	preedit 为输入法组合中的预览串（SDL_EVENT_TEXT_EDITING，持续到下一次变化）；
	active 表示平台文本输入是否已启用（由文本框焦点驱动）。
	*/
	struct Input_Text_State
	{
		std::string committed{};
		std::string preedit{};
		bool active = false;
	};

	/*
	帧输入
	key_down 为跨帧保持的按住状态；key_pressed / key_released / key_repeat 为本帧边沿列表；
	其余字段均由帧推进锁存，下一帧推进时清空。
	*/
	struct Input_Frame
	{
		//帧序号（自 1 起）
		uint64_t frame = 0;
		//本帧时间步长（秒）
		double dt = 0.0;
		//窗口客户区尺寸（像素）
		uint32_t window_width = 0;
		uint32_t window_height = 0;
		//窗口显示缩放（DPI）
		float dpi_scale = 1.0f;
		//窗口是否获得键盘焦点
		bool window_focused = true;
		//指针状态
		Input_Pointer_State pointer{};
		//修饰键
		Input_Modifier_State modifiers{};
		//文本
		Input_Text_State text{};
		//按住状态（下标 = 键位编码）
		std::array<uint8_t, INPUT_KEY_CAPACITY> key_down{};
		//本帧按下沿
		std::vector<Input_Key_Code> key_pressed{};
		//本帧抬起沿
		std::vector<Input_Key_Code> key_released{};
		//本帧重复按下沿（长按自动重复）
		std::vector<Input_Key_Code> key_repeat{};

		//键位是否按住
		bool key_held(Input_Key_Code code) const
		{
			//越界按未按住处理
			if (code >= INPUT_KEY_CAPACITY)
				return false;
			return key_down[code] != 0;
		}
		//键位是否有本帧按下沿
		bool key_pressed_check(Input_Key_Code code) const
		{
			return std::find(key_pressed.begin(), key_pressed.end(), code) != key_pressed.end();
		}
		//键位是否有本帧抬起沿
		bool key_released_check(Input_Key_Code code) const
		{
			return std::find(key_released.begin(), key_released.end(), code) != key_released.end();
		}
	};
}
