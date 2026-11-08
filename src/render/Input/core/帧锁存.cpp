#include "../局部命名空间使用.h"

namespace engine
{
	//帧推进：锁存待生效状态为本帧快照并清空单帧边沿
	void Input_Aggregator::frame_advance(double dt, uint32_t window_width, uint32_t window_height, float dpi_scale)
	{
		//帧序号与时间步长
		++frame.frame;
		frame.dt = dt;
		//窗口尺寸：事件优先，缺省取帧推进参数
		if (pending_window_valid)
		{
			frame.window_width = pending_window_width;
			frame.window_height = pending_window_height;
			pending_window_valid = false;
		}
		else if (window_width > 0 && window_height > 0)
		{
			frame.window_width = window_width;
			frame.window_height = window_height;
		}
		//显示缩放（非正时保持上一帧）
		if (dpi_scale > 0.0f)
			frame.dpi_scale = dpi_scale;

		//按住状态快照
		frame.key_down = keys_held;
		//键位边沿锁存（移动后待生效列表自动清空）
		frame.key_pressed = std::move(pending_pressed);
		frame.key_released = std::move(pending_released);
		frame.key_repeat = std::move(pending_repeat);

		//指针位移增量
		frame.pointer.dx = pending_dx;
		frame.pointer.dy = pending_dy;
		pending_dx = 0.0f;
		pending_dy = 0.0f;
		//滚轮增量与齿数
		frame.pointer.wheel_x = pending_wheel_x;
		frame.pointer.wheel_y = pending_wheel_y;
		frame.pointer.wheel_up_ticks = pending_wheel_up_ticks;
		frame.pointer.wheel_down_ticks = pending_wheel_down_ticks;
		pending_wheel_x = 0.0f;
		pending_wheel_y = 0.0f;
		pending_wheel_up_ticks = 0;
		pending_wheel_down_ticks = 0;
		//鼠标按住与边沿
		for (uint32_t index = 0; index < INPUT_MOUSE_BUTTON_COUNT; ++index)
		{
			frame.pointer.button_down[index] = buttons_held[index];
			frame.pointer.button_pressed[index] = pending_button_pressed[index];
			frame.pointer.button_released[index] = pending_button_released[index];
			pending_button_pressed[index] = false;
			pending_button_released[index] = false;
		}

		//文本提交锁存
		frame.text.committed = std::move(pending_committed);
		//预编辑串仅在变化时更新
		if (pending_preedit_changed)
		{
			frame.text.preedit = pending_preedit;
			pending_preedit_changed = false;
		}
		//文本输入启用状态同步
		frame.text.active = text_active;
	}

	//当前帧快照
	const Input_Frame& Input_Aggregator::frame_get(void) const
	{
		return frame;
	}

	//复位全部状态
	void Input_Aggregator::reset(void)
	{
		//关闭平台文本输入
		text_input_enable(false);
		//清空待生效状态
		pending_pressed.clear();
		pending_released.clear();
		pending_repeat.clear();
		pending_committed.clear();
		pending_preedit.clear();
		pending_preedit_changed = false;
		pending_wheel_x = 0.0f;
		pending_wheel_y = 0.0f;
		pending_wheel_up_ticks = 0;
		pending_wheel_down_ticks = 0;
		pending_dx = 0.0f;
		pending_dy = 0.0f;
		//清空窗口尺寸待生效状态
		pending_window_valid = false;
		pending_window_width = 0;
		pending_window_height = 0;
		//清空按住状态与鼠标边沿
		keys_held.fill(0);
		for (uint32_t index = 0; index < INPUT_MOUSE_BUTTON_COUNT; ++index)
		{
			buttons_held[index] = false;
			pending_button_pressed[index] = false;
			pending_button_released[index] = false;
		}
		//复位本帧快照
		frame = Input_Frame{};
	}
}
