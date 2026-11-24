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
			|| node.type == Ui_Node_Type::Checkbox || node.type == Ui_Node_Type::Slider
			|| node.type == Ui_Node_Type::Radio || node.type == Ui_Node_Type::Switch
			|| node.type == Ui_Node_Type::Combo;
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

	//方向键焦点导航
	void Ui_Context::focus_navigate_dir(int dx, int dy)
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
		//无焦点时取首个
		const Ui_Node* current = focused != 0 ? node_find(focused) : nullptr;
		if (current == nullptr)
		{
			slider_drag = 0;
			focus_set(focusables.front());
			return;
		}
		//当前节点中心
		const float center_x = current->rect.x + current->rect.w * 0.5f;
		const float center_y = current->rect.y + current->rect.h * 0.5f;
		//最近邻评分（主轴距离 + 交叉轴距离一半）
		Ui_Handle best = 0;
		float best_score = 0.0f;
		for (Ui_Handle handle : focusables)
		{
			//跳过自身
			if (handle == focused)
				continue;
			const Ui_Node* node = node_find(handle);
			if (node == nullptr)
				continue;
			const float node_x = node->rect.x + node->rect.w * 0.5f;
			const float node_y = node->rect.y + node->rect.h * 0.5f;
			const float delta_x = node_x - center_x;
			const float delta_y = node_y - center_y;
			//方向过滤
			if (dx > 0 && delta_x <= 0.0f)
				continue;
			if (dx < 0 && delta_x >= 0.0f)
				continue;
			if (dy > 0 && delta_y <= 0.0f)
				continue;
			if (dy < 0 && delta_y >= 0.0f)
				continue;
			//主轴距离优先，交叉轴距离折半
			const float main_distance = dx != 0
				? (delta_x < 0.0f ? -delta_x : delta_x)
				: (delta_y < 0.0f ? -delta_y : delta_y);
			const float cross_distance = dx != 0
				? (delta_y < 0.0f ? -delta_y : delta_y)
				: (delta_x < 0.0f ? -delta_x : delta_x);
			const float score = main_distance + cross_distance * 0.5f;
			if (best == 0 || score < best_score)
			{
				best = handle;
				best_score = score;
			}
		}
		//无同方向候选则保持焦点
		if (best != 0)
		{
			slider_drag = 0;
			focus_set(best);
		}
	}

	//滑动条按指针坐标更新取值
	bool Ui_Context::slider_value_apply(Ui_Node& node, float pointer_x, float pointer_y)	{
		//纵向判定：滚动条看绑定声明，普通滑动条看节点方向
		const bool scroll_linked = node.scroll_target != 0;
		const bool vertical = scroll_linked ? node.scroll_vertical : node.vertical;
		const float pointer = vertical ? pointer_y : pointer_x;
		const float origin = vertical ? node.rect.y : node.rect.x;
		const float length = vertical ? node.rect.h : node.rect.w;
		//归一化位置（矩形长度为零时按 0）
		float ratio = 0.0f;
		if (length > 0.0f)
			ratio = (pointer - origin) / length;
		//普通纵向滑动条自下而上取值（滚动条保持自上而下）
		if (vertical && !scroll_linked)
			ratio = 1.0f - ratio;
		//钳到 0..1
		if (!(ratio >= 0.0f))
			ratio = 0.0f;
		else if (ratio > 1.0f)
			ratio = 1.0f;
		//按步进量化（0 = 连续）
		if (node.value_step > 0.0f)
		{
			const float stepped = std::floor(ratio / node.value_step + 0.5f) * node.value_step;
			ratio = stepped < 0.0f ? 0.0f : (stepped > 1.0f ? 1.0f : stepped);
		}
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

	//单选按钮选中（同组互斥）
	bool Ui_Context::radio_select(Ui_Node& node)
	{
		//已选中则不再变化（单选不支持点击取消）
		if (node.checked)
			return false;
		//组键：显式组优先，否则同父为组
		const Ui_Handle group = node.radio_group != 0 ? node.radio_group : node.parent;
		//先收集同组待取消项，避免回调期间遍历被修改
		std::vector<Ui_Handle> victims{};
		for (const Ui_Node& other : nodes)
		{
			//跳过已销毁 / 自身 / 非单选 / 未选中项
			if (!other.alive || other.handle == node.handle || other.type != Ui_Node_Type::Radio || !other.checked)
				continue;
			const Ui_Handle other_group = other.radio_group != 0 ? other.radio_group : other.parent;
			//跨组不受影响
			if (other_group != group)
				continue;
			victims.push_back(other.handle);
		}

		//选中自身并通知
		node.checked = true;
		node.clicked = true;
		nlohmann::json config = nlohmann::json::object();
		config[ui_event::field_handle] = node.handle;
		config[ui_event::field_checked] = true;
		bus_get().publish(ui_event::toggle, config);
		if (node.on_toggle != nullptr)
			node.on_toggle(node.handle, true, node.user);

		//逐个取消同组其余项（回调可能已销毁控件，逐项重新查找）
		for (Ui_Handle handle : victims)
		{
			Ui_Node* other = node_find(handle);
			if (other == nullptr || !other->checked)
				continue;
			other->checked = false;
			nlohmann::json other_config = nlohmann::json::object();
			other_config[ui_event::field_handle] = other->handle;
			other_config[ui_event::field_checked] = false;
			bus_get().publish(ui_event::toggle, other_config);
			if (other->on_toggle != nullptr)
				other->on_toggle(other->handle, false, other->user);
		}
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

		//多行文本框：回车在光标处插入换行（第九百六十六轮）
		if (node.multiline
			&& (input.key_pressed_check(SDL_SCANCODE_RETURN) || input.key_pressed_check(SDL_SCANCODE_KP_ENTER)))
		{
			node.text.insert(node.caret, "\n");
			node.caret += 1;
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
		//DPI 缩放：仅开启自动缩放时采用输入帧比例（默认 1，保持既有像素口径；第九百七十一轮）
		ui_scale = (ui_scale_auto && input.dpi_scale > 0.0f) ? input.dpi_scale : 1.0f;
		//交互累计时间（长按 / 双击判定；第九百八十二轮）
		interaction_time += input.dt;
		const float pointer_x = input.pointer.x / ui_scale;
		const float pointer_y = input.pointer.y / ui_scale;
		//根节点铺满窗口客户区
		Ui_Node* root = node_find(1);
		if (root != nullptr)
		{
			const Ui_Rect root_rect{ 0.0f, 0.0f,
				static_cast<float>(input.window_width) / ui_scale,
				static_cast<float>(input.window_height) / ui_scale };
			//根尺寸变化标记布局待重算
			if (root_rect.w != layout_root_width || root_rect.h != layout_root_height)
			{
				layout_root_width = root_rect.w;
				layout_root_height = root_rect.h;
				layout_dirty = true;
			}
			root->rect = root_rect;
			//根节点无滚动偏移，基准矩形同步
			root->rect_base = root_rect;
		}
		//节点集合变化（创建 / 销毁）标记布局待重算
		if (nodes.size() != layout_node_count)
		{
			layout_node_count = nodes.size();
			layout_dirty = true;
		}
		//仅脏时重算：先还原基准矩形，再锚点布局（父先于子），最后排列容器直接子节点
		if (layout_dirty)
		{
			for (Ui_Node& node : nodes)
			{
				//已销毁槽位不参与
				if (node.handle == 0 || !node.alive)
					continue;
				node.rect = node.rect_base;
			}
			layout_apply();
			flow_apply();
			layout_dirty = false;
			layout_apply_count_value += 1;
		}
		//按滚动偏移平移矩形（逐帧执行，保证命中与绘制一致）
		scroll_apply();

		//数值插值：按帧时间把显示值向目标值趋近（第九百四十五轮）
		if (input.dt > 0.0)
		{
			for (Ui_Node& node : nodes)
			{
				//已销毁槽位或未开启插值
				if (node.handle == 0 || !node.alive || node.anim_speed <= 0.0f)
					continue;
				//本帧最大步进
				const float step = node.anim_speed * static_cast<float>(input.dt);
				//目标与显示值之差
				const float diff = node.value - node.display_value;
				const float magnitude = diff >= 0.0f ? diff : -diff;
				//一步内可达则直接到位
				if (magnitude <= step)
					node.display_value = node.value;
				else
					node.display_value += diff >= 0.0f ? step : -step;
			}
		}

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

		//方向键焦点导航（文本框编辑时不拦截；第九百六十轮）
		if (keyboard_gate)
		{
			const Ui_Node* focus_node = focused != 0 ? node_find(focused) : nullptr;
			const bool text_editing = focus_node != nullptr && focus_node->type == Ui_Node_Type::Text_Box;
			//展开的下拉框消费上下键（第九百七十九轮）
			const bool combo_navigating = focus_node != nullptr && focus_node->type == Ui_Node_Type::Combo
				&& focus_node->combo_open;
			if (!text_editing && !combo_navigating)
			{
				int move_x = 0;
				int move_y = 0;
				if (input.key_pressed_check(SDL_SCANCODE_LEFT))
					move_x = -1;
				else if (input.key_pressed_check(SDL_SCANCODE_RIGHT))
					move_x = 1;
				else if (input.key_pressed_check(SDL_SCANCODE_UP))
					move_y = -1;
				else if (input.key_pressed_check(SDL_SCANCODE_DOWN))
					move_y = 1;
				//有方向则导航
				if (move_x != 0 || move_y != 0)
					focus_navigate_dir(move_x, move_y);
			}
		}

		//刷新悬停目标（窗口失焦时不命中）
		const Ui_Handle previous_hovered = hovered;
		hovered = input.window_focused ? hit_test(pointer_x, pointer_y) : 0;
		//悬停目标变化：发布域内事件（只走域内总线，不翻译为 Input/MouseFocus）
		if (hovered != previous_hovered)
		{
			nlohmann::json config = nlohmann::json::object();
			config[ui_event::field_handle] = hovered;
			config[ui_event::field_inside] = hovered != 0;
			bus_get().publish(ui_event::hover, config);
		}
		for (Ui_Node& node : nodes)
		{
			node.hovered = node.alive && node.handle == hovered && node_focusable(node);
			//悬停计时：移出归零（第九百六十三轮）
			node.hover_time = node.hovered ? node.hover_time + static_cast<float>(input.dt) : 0.0f;
		}

		//滚轮：滚动指针下最近的滚动容器（第八百九十一轮）
		bool wheel_scrolled = false;
		if (input.pointer.wheel_y != 0.0f)
		{
			//自指针命中控件向上寻找开启裁剪的容器
			Ui_Handle cursor_handle = hovered;
			while (cursor_handle != 0)
			{
				Ui_Node* cursor_node = node_find(cursor_handle);
				if (cursor_node == nullptr)
					break;
				//命中滚动容器：向上滚动为负、向下为正
				if (cursor_node->clip_children)
				{
					const float step = 24.0f;
					float next_scroll = cursor_node->scroll_y - input.pointer.wheel_y * step;
					if (next_scroll < 0.0f)
						next_scroll = 0.0f;
					if (next_scroll != cursor_node->scroll_y)
					{
						cursor_node->scroll_y = next_scroll;
						wheel_scrolled = true;
					}
					break;
				}
				cursor_handle = cursor_node->parent;
			}
		}

		//列表控件：点选行更新当前项（第九百八十一轮）
		if (input.pointer.button_pressed[0])
		{
			for (Ui_Node& node : nodes)
			{
				//仅处理可用列表
				if (node.handle == 0 || !node.alive || node.type != Ui_Node_Type::List_Box
					|| !node.visible || !node.enabled || node.combo_items.empty())
					continue;
				//点在行内：按等分行高取下标
				if (node.rect.contains(pointer_x, pointer_y))
				{
					const float row_height = node.rect.h / static_cast<float>(node.combo_items.size());
					std::size_t row = 0;
					if (row_height > 0.0f)
						row = static_cast<std::size_t>((pointer_y - node.rect.y) / row_height);
					if (row >= node.combo_items.size())
						row = node.combo_items.size() - 1;
					node.combo_index = row;
					node.clicked = true;
					break;
				}
			}
		}

		//下拉选择框：展开 / 收起与选项点选（第九百六十五轮；第九百八十轮展开区滚动）
		if (input.pointer.button_pressed[0])
		{
			for (Ui_Node& node : nodes)
			{
				//非本帧可交互下拉跳过
				if (node.handle == 0 || !node.alive || node.type != Ui_Node_Type::Combo
					|| !node.visible || !node.enabled)
					continue;
				const float px = pointer_x;
				const float py = pointer_y;
				//点在控件内：切换展开
				if (px >= node.rect.x && px <= node.rect.x + node.rect.w
					&& py >= node.rect.y && py <= node.rect.y + node.rect.h)
				{
					node.combo_open = !node.combo_open;
					node.clicked = true;
					break;
				}
				//展开时点在选项区：更新下标并收起
				if (node.combo_open)
				{
					constexpr std::size_t VISIBLE_ROWS = 8;
					const float list_y = node.rect.y + node.rect.h;
					const std::size_t visible = node.combo_items.size() < VISIBLE_ROWS
						? node.combo_items.size() : VISIBLE_ROWS;
					const float list_h = node.rect.h * static_cast<float>(visible);
					if (px >= node.rect.x && px <= node.rect.x + node.rect.w
						&& py >= list_y && py < list_y + list_h)
					{
						const std::size_t row = static_cast<std::size_t>((py - list_y) / node.rect.h);
						const std::size_t index = node.combo_scroll + row;
						if (index < node.combo_items.size())
							node.combo_index = index;
						node.combo_open = false;
						node.clicked = true;
						break;
					}
				}
			}
		}

		//下拉选择框：展开区滚轮滚动（第九百八十轮）
		if (input.pointer.wheel_y != 0.0f)
		{
			for (Ui_Node& node : nodes)
			{
				//仅处理展开且可用的下拉
				if (node.handle == 0 || !node.alive || node.type != Ui_Node_Type::Combo
					|| !node.visible || !node.enabled || !node.combo_open)
					continue;
				constexpr std::size_t VISIBLE_ROWS = 8;
				const float list_y = node.rect.y + node.rect.h;
				const std::size_t visible = node.combo_items.size() < VISIBLE_ROWS
					? node.combo_items.size() : VISIBLE_ROWS;
				const float list_h = node.rect.h * static_cast<float>(visible);
				//指针须落在展开区内
				if (!(pointer_x >= node.rect.x && pointer_x <= node.rect.x + node.rect.w
					&& pointer_y >= list_y && pointer_y < list_y + list_h))
					continue;
				const std::size_t maximum = node.combo_items.size() > visible
					? node.combo_items.size() - visible : 0;
				//滚轮向下为正：wheel_y < 0 时下移
				long long next = static_cast<long long>(node.combo_scroll);
				next += input.pointer.wheel_y < 0.0f ? 1 : -1;
				if (next < 0)
					next = 0;
				if (static_cast<std::size_t>(next) > maximum)
					next = static_cast<long long>(maximum);
				node.combo_scroll = static_cast<std::size_t>(next);
				break;
			}
		}

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
				//按下即重置长按累计（第九百八十二轮）
				node->press_hold = 0.0f;
				node->long_pressed = false;
				//点击可聚焦控件即获焦（文本框同时进入文本模式）
				focus_set(node->handle);
				//滑动条按下即开始拖拽并更新取值
				if (node->type == Ui_Node_Type::Slider)
				{
					slider_drag = node->handle;
					slider_value_apply(*node, pointer_x, pointer_y);
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
				slider_value_apply(*node, pointer_x, pointer_y);
		}

		//鼠标左键抬起沿：同一目标上抬起即判定单击
		if (input.pointer.button_released[0])
		{
			Ui_Node* node = node_find(pressed);
			if (node != nullptr && pressed == hovered)
			{
				//双击判定：与上次点击间隔不超过 0.35 秒（第九百八十二轮）
				const float now = static_cast<float>(interaction_time);
				if (node->last_click_time >= 0.0f && now - node->last_click_time <= 0.35f)
				{
					node->double_clicked = true;
					node->last_click_time = -1.0f;
				}
				else
					node->last_click_time = now;
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
				//复选框 / 开关：切换选中并通知
				else if (node->type == Ui_Node_Type::Checkbox || node->type == Ui_Node_Type::Switch)
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
				//单选按钮：选中并取消同组
				else if (node->type == Ui_Node_Type::Radio)
					radio_select(*node);
			}
			pressed = 0;
			slider_drag = 0;
		}

		//刷新按下视觉状态
		for (Ui_Node& node : nodes)
			node.pressed = node.alive && node.handle == pressed && input.pointer.button_down[0];

		//长按累计：按住达到 0.6 秒置标记（第九百八十二轮）
		if (pressed != 0)
		{
			Ui_Node* hold_node = node_find(pressed);
			if (hold_node != nullptr)
			{
				hold_node->press_hold += static_cast<float>(input.dt);
				if (hold_node->press_hold >= 0.6f)
					hold_node->long_pressed = true;
			}
		}

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
			else if (activate && (focus_node->type == Ui_Node_Type::Checkbox
				|| focus_node->type == Ui_Node_Type::Switch))
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
			//单选按钮：键盘激活同鼠标
			else if (activate && focus_node->type == Ui_Node_Type::Radio)
				radio_select(*focus_node);
			//下拉框键盘操作（第九百七十九轮）
			else if (focus_node->type == Ui_Node_Type::Combo)
			{
				//展开态：上下键移动、回车确认、Esc 收起
				if (focus_node->combo_open)
				{
					//本帧是否发生选项移动
					bool combo_moved = false;
					if (input.key_pressed_check(SDL_SCANCODE_UP) && focus_node->combo_index > 0)
					{
						focus_node->combo_index -= 1;
						combo_moved = true;
					}
					if (input.key_pressed_check(SDL_SCANCODE_DOWN)
						&& focus_node->combo_index + 1 < focus_node->combo_items.size())
					{
						focus_node->combo_index += 1;
						combo_moved = true;
					}
					if (input.key_pressed_check(SDL_SCANCODE_RETURN)
						|| input.key_pressed_check(SDL_SCANCODE_KP_ENTER))
					{
						focus_node->combo_open = false;
						focus_node->clicked = true;
					}
					else if (input.key_pressed_check(SDL_SCANCODE_ESCAPE))
						focus_node->combo_open = false;
					//仅在键盘移动时保持当前项可见（避免覆盖滚轮滚动；第九百八十轮）
					constexpr std::size_t VISIBLE_ROWS = 8;
					if (combo_moved)
					{
						if (focus_node->combo_index < focus_node->combo_scroll)
							focus_node->combo_scroll = focus_node->combo_index;
						else if (focus_node->combo_index >= focus_node->combo_scroll + VISIBLE_ROWS)
							focus_node->combo_scroll = focus_node->combo_index - (VISIBLE_ROWS - 1);
					}
				}
				//收起态：回车 / 空格展开
				else if (activate)
					focus_node->combo_open = true;
			}
			//焦点文本框消费文本与编辑键
			else if (focus_node->type == Ui_Node_Type::Text_Box)
				text_edit(*focus_node, input);
		}
		//滚动条取值同步到目标容器（拖动后当帧生效）
		if (scroll_bar_sync() || wheel_scrolled)
			scroll_apply();
		//派发本帧域内事件（遍历结束后统一送达订阅者）
		bus_get().frame_flush();
	}

	//滚动条取值同步到目标容器
	bool Ui_Context::scroll_bar_sync(void)
	{
		//是否有偏移发生变化
		bool changed = false;
		for (Ui_Node& bar : nodes)
		{
			//跳过空槽、已销毁与非滚动条
			if (bar.handle == 0 || !bar.alive || bar.scroll_target == 0)
				continue;
			Ui_Node* target = node_find(bar.scroll_target);
			if (target == nullptr)
				continue;
			//视口尺寸取基准矩形（滚动偏移只改 rect）
			const float viewport = bar.scroll_vertical ? target->rect_base.h : target->rect_base.w;
			const float maximum = bar.scroll_content > viewport ? bar.scroll_content - viewport : 0.0f;
			//内容不足一屏：滚动条取值归零（禁用拖动位移）
			if (maximum <= 0.0f)
				bar.value = 0.0f;
			const float offset = bar.value * maximum;
			//写回目标滚动偏移
			if (bar.scroll_vertical)
			{
				if (offset != target->scroll_y)
				{
					target->scroll_y = offset;
					changed = true;
				}
			}
			else if (offset != target->scroll_x)
			{
				target->scroll_x = offset;
				changed = true;
			}
		}
		return changed;
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
		//复选框 / 开关：切换选中
		if (node->type == Ui_Node_Type::Checkbox || node->type == Ui_Node_Type::Switch)
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
		//单选按钮：选中并取消同组
		if (node->type == Ui_Node_Type::Radio)
			return radio_select(*node);
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
