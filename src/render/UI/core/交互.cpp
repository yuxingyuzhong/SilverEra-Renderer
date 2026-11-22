#include "../局部命名空间使用.h"
//UI 域内总线（控件事件路由中心）
#include "UI本地总线.h"
/*
SDL3 头
本实现需要扫描码常量（退格 / 删除 / 左右移动）做文本框编辑；
本层私有包含根已放开 SDL3 头，公共头不暴露。
*/
#include <SDL3/SDL.h>

namespace engine
{
	//命中测试（返回最上层可见且可用的控件句柄；无命中返回 0）
	Ui_Handle Ui_Context::hit_test(float px, float py) const
	{
		//逆序遍历：后创建者在上
		for (std::size_t index = nodes.size(); index > 0; --index)
		{
			const Ui_Node& node = nodes[index - 1];
			//跳过已销毁 / 不可见 / 不可用 / 根节点 / 纯文本标签
			if (!node.alive || !node.visible || !node.enabled || node.type == Ui_Node_Type::Root
				|| node.type == Ui_Node_Type::Label)
				continue;
			//命中矩形即返回
			if (node.rect.contains(px, py))
				return node.handle;
		}
		return 0;
	}

	//焦点设置（0 = 清除焦点）
	void Ui_Context::focus_set(Ui_Handle handle)
	{
		//与当前焦点相同则空操作
		if (focused == handle)
			return;
		//清除旧焦点标记
		Ui_Node* old_node = node_find(focused);
		if (old_node != nullptr)
			old_node->focused = false;
		//更新焦点句柄
		focused = handle;
		//设置新焦点标记
		Ui_Node* new_node = node_find(handle);
		if (new_node != nullptr)
			new_node->focused = true;
	}

	//UTF-8 前一码点起点
	static std::size_t utf8_prev(const std::string& text, std::size_t pos)
	{
		//已到串首
		if (pos == 0)
			return 0;
		//向前跳过续字节
		std::size_t index = pos - 1;
		while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
			--index;
		return index;
	}

	//UTF-8 下一码点终点
	static std::size_t utf8_next(const std::string& text, std::size_t pos)
	{
		//已到串尾
		if (pos >= text.size())
			return text.size();
		//向后跳过续字节
		std::size_t index = pos + 1;
		while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
			++index;
		return index;
	}

	//控件是否可获得焦点
	bool Ui_Context::node_focusable(const Ui_Node& node)
	{
		return node.type == Ui_Node_Type::Button || node.type == Ui_Node_Type::Text_Box
			|| node.type == Ui_Node_Type::Checkbox || node.type == Ui_Node_Type::Slider;
	}

	//Tab 焦点导航
	void Ui_Context::focus_navigate(bool forward)
	{
		//可聚焦控件集合（按创建顺序）
		std::vector<Ui_Handle> focusables{};
		for (const Ui_Node& node : nodes)
		{
			//跳过不可聚焦 / 不可见 / 不可用
			if (!node.alive || !node.visible || !node.enabled || !node_focusable(node))
				continue;
			focusables.push_back(node.handle);
		}
		//无可聚焦控件则清除焦点
		if (focusables.empty())
		{
			focus_set(0);
			return;
		}

		//查找当前焦点在集合中的位置
		std::size_t current = focusables.size();
		for (std::size_t index = 0; index < focusables.size(); ++index)
		{
			if (focusables[index] == focused)
			{
				current = index;
				break;
			}
		}

		//无焦点时向前取首个、向后取末个；有焦点则按方向循环移动
		std::size_t target = 0;
		if (current >= focusables.size())
			target = forward ? 0 : focusables.size() - 1;
		else if (forward)
			target = (current + 1) % focusables.size();
		else
			target = (current + focusables.size() - 1) % focusables.size();

		//切换焦点（离开滑动条时终止拖拽）
		slider_drag = 0;
		focus_set(focusables[target]);
	}

	//滑动条按指针横坐标更新取值
	bool Ui_Context::slider_value_apply(Ui_Node& node, float pointer_x)
	{
		//归一化位置（矩形宽度为零时按 0）
		float ratio = 0.0f;
		if (node.rect.w > 0.0f)
			ratio = (pointer_x - node.rect.x) / node.rect.w;
		//钳到 0..1
		if (!(ratio >= 0.0f))
			ratio = 0.0f;
		else if (ratio > 1.0f)
			ratio = 1.0f;
		//值未变化则空操作
		if (ratio == node.value)
			return false;
		//写值并通知
		node.value = ratio;
		//发布域内取值事件（回调保留为本地快捷订阅）
		nlohmann::json config = nlohmann::json::object();
		config[ui_event::field_handle] = node.handle;
		config[ui_event::field_value] = node.value;
		bus_get().publish(ui_event::value, config);
		if (node.on_value != nullptr)
			node.on_value(node.handle, node.value, node.user);
		return true;
	}

	//文本框文本编辑（消费本帧文本与编辑键）
	void Ui_Context::text_edit(Ui_Node& node, const Input_Frame& input)
	{
		//光标越界时钳制到末尾
		if (node.caret > node.text.size())
			node.caret = node.text.size();

		//插入本帧提交的文本（系统文本路由开启时由系统回传后再落文本）
		if (!input.text.committed.empty() && !(text_route && !node.input_name.empty()))
		{
			node.text.insert(node.caret, input.text.committed);
			node.caret += input.text.committed.size();
		}

		//退格：删除光标前一码点
		if (input.key_pressed_check(SDL_SCANCODE_BACKSPACE) && node.caret > 0)
		{
			const std::size_t begin = utf8_prev(node.text, node.caret);
			node.text.erase(begin, node.caret - begin);
			node.caret = begin;
		}
		//删除：删除光标后一码点
		if (input.key_pressed_check(SDL_SCANCODE_DELETE) && node.caret < node.text.size())
		{
			const std::size_t end = utf8_next(node.text, node.caret);
			node.text.erase(node.caret, end - node.caret);
		}
		//左移一个码点
		if (input.key_pressed_check(SDL_SCANCODE_LEFT))
			node.caret = utf8_prev(node.text, node.caret);
		//右移一个码点
		if (input.key_pressed_check(SDL_SCANCODE_RIGHT))
			node.caret = utf8_next(node.text, node.caret);

		//光标越界兜底
		if (node.caret > node.text.size())
			node.caret = node.text.size();
	}

	//帧交互：消费输入快照
	void Ui_Context::frame_begin(const Input_Frame& input)
	{
		//根节点铺满窗口客户区
		Ui_Node* root = node_find(1);
		if (root != nullptr)
			root->rect = Ui_Rect{ 0.0f, 0.0f,
				static_cast<float>(input.window_width), static_cast<float>(input.window_height) };

		//根尺寸更新后重算锚点布局（父先于子）
		layout_apply();
		//布局结果之上再排列容器直接子节点
		flow_apply();

		//清除上一帧的单击标记
		for (Ui_Node& node : nodes)
			node.clicked = false;

		//复位本帧的系统输入交互标记
		pressed_input_name.clear();
		press_outside = false;
		press_any = false;

		//焦点节点失效时清除焦点
		if (focused != 0)
		{
			const Ui_Node* node = node_find(focused);
			if (node == nullptr || !node_focusable(*node) || !node->visible || !node->enabled)
				focus_set(0);
		}

		//Tab 焦点导航（Shift+Tab 反向；键盘总闸关闭时不响应）
		if (keyboard_gate && input.key_pressed_check(SDL_SCANCODE_TAB))
			focus_navigate(!input.modifiers.shift);

		//刷新悬停目标（窗口失焦时不命中）
		const Ui_Handle previous_hovered = hovered;
		hovered = input.window_focused ? hit_test(input.pointer.x, input.pointer.y) : 0;
		//悬停目标变化：发布域内事件（只走域内总线，不翻译为 Input/MouseFocus）
		if (hovered != previous_hovered)
		{
			nlohmann::json config = nlohmann::json::object();
			config[ui_event::field_handle] = hovered;
			config[ui_event::field_inside] = hovered != 0;
			bus_get().publish(ui_event::hover, config);
		}
		for (Ui_Node& node : nodes)
			node.hovered = node.alive && node.handle == hovered && node_focusable(node);

		//鼠标左键按下沿：确定按下目标与焦点
		if (input.pointer.button_pressed[0])
		{
			pressed = 0;
			slider_drag = 0;
			//本帧发生左键按下
			press_any = true;
			//指针是否未命中任何控件（按在 UI 外部）
			press_outside = hovered == 0;
			Ui_Node* node = node_find(hovered);
			//记录按下控件的系统输入对象名（命中带名字的控件时）
			if (node != nullptr && !node->input_name.empty())
				pressed_input_name = node->input_name;
			if (node != nullptr && node_focusable(*node))
			{
				pressed = node->handle;
				//点击可聚焦控件即获焦（文本框同时进入文本模式）
				focus_set(node->handle);
				//滑动条按下即开始拖拽并更新取值
				if (node->type == Ui_Node_Type::Slider)
				{
					slider_drag = node->handle;
					slider_value_apply(*node, input.pointer.x);
				}
			}
			//点击空白或面板：清除焦点
			else
				focus_set(0);
		}

		//滑动条拖拽（按住期间随指针移动持续更新）
		if (input.pointer.button_down[0] && slider_drag != 0)
		{
			Ui_Node* node = node_find(slider_drag);
			if (node != nullptr && node->visible && node->enabled)
				slider_value_apply(*node, input.pointer.x);
		}

		//鼠标左键抬起沿：同一目标上抬起即判定单击
		if (input.pointer.button_released[0])
		{
			Ui_Node* node = node_find(pressed);
			if (node != nullptr && pressed == hovered)
			{
				//按钮：触发点击回调
				if (node->type == Ui_Node_Type::Button)
				{
					node->clicked = true;
					//发布域内点击事件（回调保留为本地快捷订阅）
					nlohmann::json config = nlohmann::json::object();
					config[ui_event::field_handle] = node->handle;
					bus_get().publish(ui_event::click, config);
					if (node->on_click != nullptr)
						node->on_click(node->handle, node->user);
				}
				//复选框：切换选中并通知
				else if (node->type == Ui_Node_Type::Checkbox)
				{
					node->checked = !node->checked;
					node->clicked = true;
					//发布域内切换事件
					nlohmann::json config = nlohmann::json::object();
					config[ui_event::field_handle] = node->handle;
					config[ui_event::field_checked] = node->checked;
					bus_get().publish(ui_event::toggle, config);
					if (node->on_toggle != nullptr)
						node->on_toggle(node->handle, node->checked, node->user);
				}
			}
			pressed = 0;
			slider_drag = 0;
		}

		//刷新按下视觉状态
		for (Ui_Node& node : nodes)
			node.pressed = node.alive && node.handle == pressed && input.pointer.button_down[0];

		//焦点控件消费键盘：Enter / 空格激活按钮与复选框（键盘总闸关闭时不响应）
		Ui_Node* focus_node = keyboard_gate ? node_find(focused) : nullptr;
		if (focus_node != nullptr)
		{
			//回车（主键盘与小键盘）与空格
			const bool activate = input.key_pressed_check(SDL_SCANCODE_RETURN)
				|| input.key_pressed_check(SDL_SCANCODE_KP_ENTER)
				|| input.key_pressed_check(SDL_SCANCODE_SPACE);
			if (activate && focus_node->type == Ui_Node_Type::Button)
			{
				focus_node->clicked = true;
				//发布域内点击事件（键盘激活）
				nlohmann::json config = nlohmann::json::object();
				config[ui_event::field_handle] = focus_node->handle;
				bus_get().publish(ui_event::click, config);
				if (focus_node->on_click != nullptr)
					focus_node->on_click(focus_node->handle, focus_node->user);
			}
			else if (activate && focus_node->type == Ui_Node_Type::Checkbox)
			{
				focus_node->checked = !focus_node->checked;
				focus_node->clicked = true;
				//发布域内切换事件（键盘激活）
				nlohmann::json config = nlohmann::json::object();
				config[ui_event::field_handle] = focus_node->handle;
				config[ui_event::field_checked] = focus_node->checked;
				bus_get().publish(ui_event::toggle, config);
				if (focus_node->on_toggle != nullptr)
					focus_node->on_toggle(focus_node->handle, focus_node->checked, focus_node->user);
			}
			//焦点文本框消费文本与编辑键
			else if (focus_node->type == Ui_Node_Type::Text_Box)
				text_edit(*focus_node, input);
		}
		//派发本帧域内事件（遍历结束后统一送达订阅者）
		bus_get().frame_flush();
	}

	//语义命令分发到绑定控件
	bool Ui_Context::command_dispatch(const std::string& command)
	{		//查找命令绑定
		auto it = command_bindings.find(command);
		if (it == command_bindings.end())
			return false;
		Ui_Node* node = node_find(it->second);
		//目标控件不可用则忽略
		if (node == nullptr || !node->visible || !node->enabled)
			return false;

		//按钮：触发点击
		if (node->type == Ui_Node_Type::Button)
		{
			node->clicked = true;
			//网关命令激活：发布带来源标记的域内点击事件（网关不回译，避免环）
			nlohmann::json config = nlohmann::json::object();
			config[ui_event::field_handle] = node->handle;
			config[ui_event::field_origin] = ui_event::origin_gateway;
			bus_get().publish(ui_event::click, config);
			if (node->on_click != nullptr)
				node->on_click(node->handle, node->user);
			return true;
		}
		//复选框：切换选中
		if (node->type == Ui_Node_Type::Checkbox)
		{
			node->checked = !node->checked;
			node->clicked = true;
			//网关命令激活：发布带来源标记的域内切换事件
			nlohmann::json config = nlohmann::json::object();
			config[ui_event::field_handle] = node->handle;
			config[ui_event::field_checked] = node->checked;
			config[ui_event::field_origin] = ui_event::origin_gateway;
			bus_get().publish(ui_event::toggle, config);
			if (node->on_toggle != nullptr)
				node->on_toggle(node->handle, node->checked, node->user);
			return true;
		}
		return false;
	}

	//控件绑定的语义命令
	std::string Ui_Context::command_of(Ui_Handle handle) const
	{
		//反查命令绑定表
		for (const auto& item : command_bindings)
		{
			if (item.second == handle)
				return item.first;
		}
		return std::string{};
	}

	//把系统焦点状态映射到控件
	void Ui_Context::system_focus_apply(const std::string& main, const std::string& child,
		const std::string& modal, bool text)
	{
		//清除上一帧的系统焦点标记
		for (Ui_Node& node : nodes)
			node.system_focused = false;

		//子对象优先，其次主对象与模态根
		const std::string& target = !child.empty() ? child : main;
		for (Ui_Node& node : nodes)
		{
			if (node.alive && !node.input_name.empty()
				&& (node.input_name == target || node.input_name == modal))
				node.system_focused = true;
		}
		(void)text;
	}

	//把系统文本语义写入焦点文本框
	bool Ui_Context::system_text_apply(const std::string& text)
	{
		//空文本不处理
		if (text.empty())
			return false;
		//焦点必须是已绑定系统输入名的文本框
		Ui_Node* node = node_find(focused);
		if (node == nullptr || node->type != Ui_Node_Type::Text_Box || node->input_name.empty())
			return false;
		//光标越界钳制
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		//插入并推进光标
		node->text.insert(node->caret, text);
		node->caret += text.size();
		return true;
	}
}
