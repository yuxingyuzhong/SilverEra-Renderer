#include "../局部命名空间使用.h"

//游戏引擎命名空间
namespace engine
{
	//按角色与视觉状态取主题色
	Ui_Color ui_theme_color(const Ui_Theme& theme, Ui_Role role, Ui_Visual_State state)
	{
		//自定义角色不使用主题
		if (role == Ui_Role::Custom)
			return Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		//禁用：标签用次级文本色，其余用禁用背景
		if (state == Ui_Visual_State::Disabled)
			return role == Ui_Role::Label ? theme.text_dim : theme.disabled;
		//按角色取常态色
		switch (role)
		{
		case Ui_Role::Panel:
			return theme.panel;
		case Ui_Role::Text_Box:
			return theme.text_box;
		case Ui_Role::Button:
			//按钮按悬停 / 按下取状态色
			if (state == Ui_Visual_State::Pressed)
				return theme.button_pressed;
			if (state == Ui_Visual_State::Hovered)
				return theme.button_hover;
			return theme.button;
		case Ui_Role::Label:
			return theme.text;
		case Ui_Role::Checkbox:
		case Ui_Role::Slider:
			//控件底面与文本框同色板（选中 / 滑块前景仍为强调色）
			return theme.text_box;
		case Ui_Role::Progress:
			//进度条背景与面板同色板（前景仍为强调色）
			return theme.panel;
		default:
			return theme.panel;
		}
	}
}
