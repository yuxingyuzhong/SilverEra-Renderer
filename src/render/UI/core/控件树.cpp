#include "../局部命名空间使用.h"
//UI 域内总线（控件事件路由中心）
#include "UI本地总线.h"

namespace engine
{
	//构造（建立根节点与域内总线）
	Ui_Context::Ui_Context(void)
	{
		//创建域内总线（控件事件统一经它派发）
		local_bus = std::make_unique<Ui_Local_Bus>();
		build();
	}

	//析构
	Ui_Context::~Ui_Context(void) = default;

	//UI 域内总线
	Ui_Local_Bus& Ui_Context::bus_get(void)
	{
		//懒创建，保证任何调用路径都可用
		if (!local_bus)
			local_bus = std::make_unique<Ui_Local_Bus>();
		return *local_bus;
	}

	//复位控件树并重建根节点
	void Ui_Context::build(void)
	{
		//清空全部状态
		nodes.clear();
		hovered = 0;
		pressed = 0;
		focused = 0;
		slider_drag = 0;
		input_root_name.clear();
		input_outside_name.clear();
		modal_input_name.clear();
		pressed_input_name.clear();
		press_outside = false;
		press_any = false;
		text_route = false;
		command_bindings.clear();
		keyboard_gate = true;
		//建立根节点（句柄 1）
		Ui_Node root{};
		root.handle = 1;
		root.parent = 0;
		root.type = Ui_Node_Type::Root;
		root.color = Ui_Color{ 0.05f, 0.05f, 0.10f, 1.0f };
		root.text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		nodes.push_back(root);
	}

	//清空控件树与交互状态
	void Ui_Context::shutdown(void)
	{
		//根节点也一并清除，不保留空树
		nodes.clear();
		hovered = 0;
		pressed = 0;
		focused = 0;
		slider_drag = 0;
		font = 0;
		input_root_name.clear();
		input_outside_name.clear();
		modal_input_name.clear();
		pressed_input_name.clear();
		press_outside = false;
		press_any = false;
		text_route = false;
		command_bindings.clear();
		keyboard_gate = true;
	}

	//根节点句柄
	Ui_Handle Ui_Context::root_get(void) const
	{
		//空树返回 0
		if (nodes.empty())
			return 0;
		return 1;
	}

	//按句柄查找控件（可写）
	Ui_Node* Ui_Context::node_find(Ui_Handle handle)
	{
		//句柄 0 无效
		if (handle == 0)
			return nullptr;
		//下标越界
		const std::size_t index = static_cast<std::size_t>(handle - 1);
		if (index >= nodes.size())
			return nullptr;
		//槽位已销毁
		if (!nodes[index].alive)
			return nullptr;
		return &nodes[index];
	}

	//按句柄查找控件（只读）
	const Ui_Node* Ui_Context::node_find(Ui_Handle handle) const
	{
		//句柄 0 无效
		if (handle == 0)
			return nullptr;
		//下标越界
		const std::size_t index = static_cast<std::size_t>(handle - 1);
		if (index >= nodes.size())
			return nullptr;
		//槽位已销毁
		if (!nodes[index].alive)
			return nullptr;
		return &nodes[index];
	}

	//释放槽位
	void Ui_Context::node_release(Ui_Handle handle)
	{
		//查找目标槽位
		Ui_Node* node = node_find(handle);
		//若不存在则忽略
		if (node == nullptr)
			return;
		//标记销毁并复位交互状态
		node->alive = false;
		node->hovered = false;
		node->pressed = false;
		node->focused = false;
		node->system_focused = false;
		node->clicked = false;
		//模态根被销毁时清除模态名
		if (!node->input_name.empty() && modal_input_name == node->input_name)
			modal_input_name.clear();
		//从命令绑定中移除该控件
		for (auto it = command_bindings.begin(); it != command_bindings.end();)
		{
			if (it->second == handle)
				it = command_bindings.erase(it);
			else
				++it;
		}
		//清理指向该节点的交互句柄
		if (hovered == handle)
			hovered = 0;
		if (pressed == handle)
			pressed = 0;
		if (focused == handle)
			focused = 0;
		if (slider_drag == handle)
			slider_drag = 0;
	}

	//创建控件（内部公共流程）
	static Ui_Handle node_create_impl(std::vector<Ui_Node>& nodes,
		Ui_Handle parent, const Ui_Rect& rect, Ui_Node_Type type)
	{
		//父节点无效时挂到根节点
		if (parent == 0)
			parent = 1;
		//若父节点不存在则挂到根节点
		if (nodes.empty() || parent > nodes.size() || !nodes[static_cast<std::size_t>(parent - 1)].alive)
			parent = nodes.empty() ? 0 : 1;
		//若控件树为空则拒绝
		if (parent == 0)
			return 0;

		//优先复用最旧的已销毁槽位
		for (std::size_t index = 0; index < nodes.size(); ++index)
		{
			if (nodes[index].alive)
				continue;
			Ui_Node node{};
			node.handle = static_cast<Ui_Handle>(index + 1);
			node.parent = parent;
			node.type = type;
			node.rect = rect;
			node.rect_base = rect;
			node.color = Ui_Color{ 0.0f, 0.0f, 0.0f, 0.0f };
			node.text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
			nodes[index] = node;
			return node.handle;
		}

		//追加新槽位
		Ui_Node node{};
		node.handle = static_cast<Ui_Handle>(nodes.size() + 1);
		node.parent = parent;
		node.type = type;
		node.rect = rect;
		node.rect_base = rect;
		node.color = Ui_Color{ 0.0f, 0.0f, 0.0f, 0.0f };
		node.text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		nodes.push_back(node);
		return node.handle;
	}

	//创建面板
	Ui_Handle Ui_Context::panel_create(Ui_Handle parent, const Ui_Rect& rect, const Ui_Color& color)
	{
		//创建面板节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Panel);
		//写入背景色
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
			node->color = color;
		return handle;
	}

	//创建按钮
	Ui_Handle Ui_Context::button_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
		void (*on_click)(Ui_Handle, void*), void* user, const Ui_Color& color)
	{
		//创建按钮节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Button);
		//写入标签、回调与背景色
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->label = label;
			node->on_click = on_click;
			node->user = user;
			node->color = color;
		}
		return handle;
	}

	//创建单行文本框
	Ui_Handle Ui_Context::text_box_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text)
	{
		//创建文本框节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Text_Box);
		//写入内容、光标与配色
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->text = text;
			node->caret = text.size();
			node->color = Ui_Color{ 0.02f, 0.02f, 0.05f, 0.95f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//创建纯文本标签
	Ui_Handle Ui_Context::label_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text,
		const Ui_Color& text_color)
	{
		//创建标签节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Label);
		//写入文本与颜色
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->text = text;
			node->text_color = text_color;
		}
		return handle;
	}

	//创建复选框
	Ui_Handle Ui_Context::checkbox_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
		bool checked, void (*on_toggle)(Ui_Handle, bool, void*), void* user)
	{
		//创建复选框节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Checkbox);
		//写入标签、状态与回调
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->label = label;
			node->checked = checked;
			node->on_toggle = on_toggle;
			node->user = user;
			node->color = Ui_Color{ 0.02f, 0.02f, 0.05f, 0.95f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//创建滑动条
	Ui_Handle Ui_Context::slider_create(Ui_Handle parent, const Ui_Rect& rect, float value,
		void (*on_value)(Ui_Handle, float, void*), void* user)
	{
		//创建滑动条节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Slider);
		//写入初值与回调（初值钳到 0..1）
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
			node->on_value = on_value;
			node->user = user;
			node->color = Ui_Color{ 0.08f, 0.08f, 0.12f, 1.0f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//销毁控件（含全部后代）
	bool Ui_Context::destroy(Ui_Handle handle)
	{
		//根节点禁止销毁
		if (handle == 1)
			return false;
		//目标不存在
		if (node_find(handle) == nullptr)
			return false;

		//逐轮回收后代（每轮只回收直接子节点，直到无子节点）
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (Ui_Node& node : nodes)
			{
				//跳过已销毁节点
				if (!node.alive)
					continue;
				//回收直接子节点
				if (node.parent == handle)
				{
					node_release(node.handle);
					changed = true;
				}
			}
		}
		//回收目标节点自身
		node_release(handle);
		return true;
	}

	//设置矩形
	bool Ui_Context::rect_set(Ui_Handle handle, const Ui_Rect& rect)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->rect = rect;
		node->rect_base = rect;
		//显式矩形优先：取消锚点布局
		node->layout.active = false;
		//矩形变化需重算布局
		layout_dirty = true;
		return true;
	}

	//取矩形
	bool Ui_Context::rect_get(Ui_Handle handle, Ui_Rect& rect_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		rect_out = node->rect;
		return true;
	}

	//设置锚点布局
	bool Ui_Context::layout_set(Ui_Handle handle, const Ui_Layout& layout)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->layout = layout;
		//布局设置变化需重算
		layout_dirty = true;
		//启用时立即按当前父矩形求一次，便于调用方当场读取
		if (layout.active)
		{
			const Ui_Node* parent = node_find(node->parent);
			if (parent != nullptr)
			{
				node->rect = ui_layout_rect_resolve(parent->rect, layout);
				node->rect_base = node->rect;
			}
		}
		return true;
	}

	//取锚点布局
	bool Ui_Context::layout_get(Ui_Handle handle, Ui_Layout& layout_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		layout_out = node->layout;
		return true;
	}

	//设置排列容器
	bool Ui_Context::flow_set(Ui_Handle handle, Ui_Flow flow, float gap, float padding)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->flow = flow;
		node->flow_gap = gap > 0.0f ? gap : 0.0f;
		node->flow_padding = padding > 0.0f ? padding : 0.0f;
		//排列设置变化需重算
		layout_dirty = true;
		return true;
	}

	//取排列设置
	bool Ui_Context::flow_get(Ui_Handle handle, Ui_Flow& flow_out, float& gap_out, float& padding_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		flow_out = node->flow;
		gap_out = node->flow_gap;
		padding_out = node->flow_padding;
		return true;
	}

	//布局重算次数
	uint64_t Ui_Context::layout_apply_count(void) const
	{
		return layout_apply_count_value;
	}

	//设置排列交叉轴对齐
	bool Ui_Context::flow_align_set(Ui_Handle handle, Ui_Align align)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->flow_align = align;
		//对齐变化需重算布局
		layout_dirty = true;
		return true;
	}

	//读取排列交叉轴对齐
	Ui_Align Ui_Context::flow_align_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return Ui_Align::Start;
		return node->flow_align;
	}

	//设置内容滚动偏移
	bool Ui_Context::scroll_set(Ui_Handle handle, float x, float y)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//负偏移按 0
		node->scroll_x = x > 0.0f ? x : 0.0f;
		node->scroll_y = y > 0.0f ? y : 0.0f;
		return true;
	}

	//读取内容滚动偏移
	bool Ui_Context::scroll_get(Ui_Handle handle, float& x_out, float& y_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
		{
			x_out = 0.0f;
			y_out = 0.0f;
			return false;
		}
		x_out = node->scroll_x;
		y_out = node->scroll_y;
		return true;
	}

	//绑定滚动条到目标容器
	bool Ui_Context::scroll_bar_bind(Ui_Handle bar, Ui_Handle target, float content_extent, bool vertical)
	{
		Ui_Node* node = node_find(bar);
		if (node == nullptr)
			return false;
		//非零目标必须存在
		if (target != 0 && node_find(target) == nullptr)
			return false;
		node->scroll_target = target;
		node->scroll_content = content_extent > 0.0f ? content_extent : 0.0f;
		node->scroll_vertical = vertical;
		return true;
	}

	//设置主题色板
	void Ui_Context::theme_set(const Ui_Theme& theme)
	{
		this->theme = theme;
	}

	//读取主题色板
	const Ui_Theme& Ui_Context::theme_get(void) const
	{
		return theme;
	}

	//设置控件样式角色
	bool Ui_Context::role_set(Ui_Handle handle, Ui_Role role)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->role = role;
		return true;
	}

	//读取控件样式角色
	Ui_Role Ui_Context::role_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return Ui_Role::Custom;
		return node->role;
	}

	//设置控件不透明度
	bool Ui_Context::opacity_set(Ui_Handle handle, float opacity)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//越界夹紧
		if (opacity < 0.0f)
			opacity = 0.0f;
		if (opacity > 1.0f)
			opacity = 1.0f;
		node->opacity = opacity;
		return true;
	}

	//读取控件不透明度
	float Ui_Context::opacity_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 1.0f;
		return node->opacity;
	}

	//设置控件圆角半径
	bool Ui_Context::corner_radius_set(Ui_Handle handle, float radius)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//负半径按直角处理
		if (radius < 0.0f)
			radius = 0.0f;
		node->corner_radius = radius;
		return true;
	}

	//读取控件圆角半径
	float Ui_Context::corner_radius_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0.0f;
		return node->corner_radius;
	}

	//设置控件描边
	bool Ui_Context::border_set(Ui_Handle handle, float width, const Ui_Color& color)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//负宽度按无描边处理
		node->border_width = width > 0.0f ? width : 0.0f;
		node->border_color = color;
		return true;
	}

	//读取控件描边宽度
	float Ui_Context::border_width_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0.0f;
		return node->border_width;
	}

	//读取控件描边颜色
	Ui_Color Ui_Context::border_color_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return Ui_Color{ 0.0f, 0.0f, 0.0f, 1.0f };
		return node->border_color;
	}

	//设置背景色
	bool Ui_Context::color_set(Ui_Handle handle, const Ui_Color& color)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->color = color;
		return true;
	}

	//设置按钮标签
	bool Ui_Context::label_set(Ui_Handle handle, const std::string& label)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->label = label;
		return true;
	}

	//设置文本框内容
	bool Ui_Context::text_set(Ui_Handle handle, const std::string& text)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text = text;
		node->caret = text.size();
		return true;
	}

	//取文本框内容
	bool Ui_Context::text_get(Ui_Handle handle, std::string& text_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		text_out = node->text;
		return true;
	}

	//复选框选中状态读取
	bool Ui_Context::checked_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->checked;
	}

	//复选框选中状态设置
	bool Ui_Context::checked_set(Ui_Handle handle, bool checked)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->checked = checked;
		return true;
	}

	//滑动条值读取
	bool Ui_Context::value_get(Ui_Handle handle, float& value_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		value_out = node->value;
		return true;
	}

	//滑动条值设置
	bool Ui_Context::value_set(Ui_Handle handle, float value)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//钳到 0..1（非数按 0）
		if (!(value >= 0.0f))
			value = 0.0f;
		else if (value > 1.0f)
			value = 1.0f;
		node->value = value;
		return true;
	}

	//设置可见
	bool Ui_Context::visible_set(Ui_Handle handle, bool visible)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->visible = visible;
		//可见性影响排列占位，需重算布局
		layout_dirty = true;
		//隐藏时若持有焦点则清除
		if (!visible && focused == handle)
			focus_set(0);
		return true;
	}

	//设置是否裁剪直接子节点
	bool Ui_Context::clip_children_set(Ui_Handle handle, bool clip)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->clip_children = clip;
		return true;
	}

	//读取是否裁剪直接子节点
	bool Ui_Context::clip_children_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->clip_children;
	}

	//设置可用
	bool Ui_Context::enabled_set(Ui_Handle handle, bool enabled)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->enabled = enabled;
		//禁用时若持有焦点则清除
		if (!enabled && focused == handle)
			focus_set(0);
		return true;
	}

	//取走单击标记
	bool Ui_Context::clicked_consume(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		const bool clicked_value = node->clicked;
		node->clicked = false;
		return clicked_value;
	}

	//是否悬停
	bool Ui_Context::hovered_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->hovered;
	}

	//是否持有焦点
	bool Ui_Context::focused_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->focused;
	}

	//当前是否有文本框持有焦点
	bool Ui_Context::text_focus_active(void) const
	{
		const Ui_Node* node = node_find(focused);
		if (node == nullptr)
			return false;
		return node->type == Ui_Node_Type::Text_Box && node->visible && node->enabled;
	}

	//设置 UI 字体句柄
	void Ui_Context::font_set(uint64_t font_handle)
	{
		font = font_handle;
	}

	//当前 UI 字体句柄
	uint64_t Ui_Context::font_get(void) const
	{
		return font;
	}

	//控件节点数
	std::size_t Ui_Context::node_count(void) const
	{
		return nodes.size();
	}

	//按槽位取控件节点
	const Ui_Node* Ui_Context::node_at(std::size_t index) const
	{
		//越界返回空
		if (index >= nodes.size())
			return nullptr;
		//已销毁槽位对外不可见
		if (!nodes[index].alive)
			return nullptr;
		return &nodes[index];
	}

	//控件绑定系统输入对象名
	bool Ui_Context::input_name_set(Ui_Handle handle, const std::string& name)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->input_name = name;
		return true;
	}

	//取控件系统输入对象名
	std::string Ui_Context::input_name_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return {};
		return node->input_name;
	}

	//设置 UI 主对象名
	void Ui_Context::input_root_name_set(const std::string& name)
	{
		input_root_name = name;
	}

	//取 UI 主对象名
	const std::string& Ui_Context::input_root_name_get(void) const
	{
		return input_root_name;
	}

	//设置 UI 外部焦点目标名
	void Ui_Context::input_outside_name_set(const std::string& name)
	{
		input_outside_name = name;
	}

	//取 UI 外部焦点目标名
	const std::string& Ui_Context::input_outside_name_get(void) const
	{
		return input_outside_name;
	}

	//设置模态根
	bool Ui_Context::modal_set(Ui_Handle handle)
	{
		const Ui_Node* node = node_find(handle);
		//模态根必须已绑定系统输入对象名
		if (node == nullptr || node->input_name.empty())
			return false;
		modal_input_name = node->input_name;
		return true;
	}

	//清除模态根
	void Ui_Context::modal_clear(void)
	{
		modal_input_name.clear();
	}

	//取模态根系统输入对象名
	const std::string& Ui_Context::modal_input_name_get(void) const
	{
		return modal_input_name;
	}

	//绑定语义命令到控件
	void Ui_Context::command_bind(const std::string& command, Ui_Handle handle)
	{
		//目标控件不存在则忽略
		if (node_find(handle) == nullptr)
			return;
		command_bindings[command] = handle;
	}

	//解除语义命令绑定
	void Ui_Context::command_unbind(const std::string& command)
	{
		command_bindings.erase(command);
	}

	//控件是否持有系统侧焦点
	bool Ui_Context::system_focused_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->system_focused;
	}

	//当前焦点文本框的系统输入对象名
	const std::string& Ui_Context::text_focus_input_name(void) const
	{
		//静态空串（避免返回悬空引用）
		static const std::string empty{};
		//查找焦点控件
		const Ui_Node* node = node_find(focused);
		if (node == nullptr || node->type != Ui_Node_Type::Text_Box)
			return empty;
		return node->input_name;
	}

	//当前焦点控件的系统输入对象名
	const std::string& Ui_Context::focused_input_name_get(void) const
	{
		//静态空串（避免返回悬空引用）
		static const std::string empty{};
		//查找焦点控件
		const Ui_Node* node = node_find(focused);
		//无焦点或未绑定系统输入名
		if (node == nullptr || node->input_name.empty())
			return empty;
		return node->input_name;
	}

	//清除本地焦点
	void Ui_Context::focus_clear(void)
	{
		focus_set(0);
		//拖拽随焦点一并终止
		slider_drag = 0;
	}

	//系统子焦点回写到本地焦点
	void Ui_Context::system_focus_sync(const std::string& child)
	{
		//系统无子对象时保持本地焦点（未命名控件仍可继续键盘导航）
		if (child.empty())
			return;
		//本地焦点已与系统一致则空操作
		const Ui_Node* current = node_find(focused);
		if (current != nullptr && current->input_name == child)
			return;
		//查找同名可聚焦控件
		for (const Ui_Node& node : nodes)
		{
			//跳过失效 / 非同名 / 不可聚焦 / 不可见 / 不可用
			if (!node.alive || node.input_name != child || !node_focusable(node)
				|| !node.visible || !node.enabled)
				continue;
			focus_set(node.handle);
			//焦点跳转时终止滑动条拖拽
			slider_drag = 0;
			return;
		}
	}

	//设置键盘交互总闸
	void Ui_Context::keyboard_gate_set(bool enabled)
	{
		keyboard_gate = enabled;
	}

	//取键盘交互总闸
	bool Ui_Context::keyboard_gate_get(void) const
	{
		return keyboard_gate;
	}

	//本帧按下的系统输入对象名
	const std::string& Ui_Context::pressed_input_name_get(void) const
	{
		return pressed_input_name;
	}

	//本帧是否按在 UI 外部
	bool Ui_Context::press_outside_get(void) const
	{
		return press_outside;
	}

	//本帧是否发生左键按下
	bool Ui_Context::press_any_get(void) const
	{
		return press_any;
	}

	//设置文本路由开关
	void Ui_Context::text_route_set(bool route)
	{
		text_route = route;
	}

	//取文本路由开关
	bool Ui_Context::text_route_get(void) const
	{
		return text_route;
	}
}
