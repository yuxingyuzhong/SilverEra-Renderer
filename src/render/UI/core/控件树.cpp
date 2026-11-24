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

	//光标位置回退到 UTF-8 字符边界
	static std::size_t caret_boundary_floor(const std::string& text, std::size_t pos)
	{
		//越界先夹到串尾
		if (pos > text.size())
			pos = text.size();
		//向前跳过续字节（串尾本身是合法边界）
		while (pos > 0 && pos < text.size()
			&& (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
			--pos;
		return pos;
	}

	//设置文本框内容
	bool Ui_Context::text_box_text_set(Ui_Handle handle, const std::string& text)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text = text;
		//光标移到新文本末尾（串尾必为字符边界）
		node->caret = node->text.size();
		return true;
	}

	//读取文本框内容
	std::string Ui_Context::text_box_text_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return std::string{};
		return node->text;
	}

	//设置光标位置
	bool Ui_Context::caret_set(Ui_Handle handle, std::size_t caret)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->caret = caret_boundary_floor(node->text, caret);
		return true;
	}

	//读取光标位置
	std::size_t Ui_Context::caret_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->caret;
	}

	//UTF-8 前一码点起点
	static std::size_t utf8_prev_start(const std::string& text, std::size_t pos)
	{
		//已在串首
		if (pos == 0)
			return 0;
		return caret_boundary_floor(text, pos - 1);
	}

	//UTF-8 后一码点终点
	static std::size_t utf8_next_end(const std::string& text, std::size_t pos)
	{
		//已在串尾
		if (pos >= text.size())
			return text.size();
		std::size_t index = pos + 1;
		//向后跳过续字节
		while (index < text.size()
			&& (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
			++index;
		return index;
	}

	//退格：删除光标前一字符
	bool Ui_Context::text_box_backspace(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//光标越界先夹到串尾
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		//已在串首
		if (node->caret == 0)
			return false;
		//删除前一字符并前移光标
		const std::size_t start = utf8_prev_start(node->text, node->caret);
		node->text.erase(start, node->caret - start);
		node->caret = start;
		return true;
	}

	//删除：删除光标后一字符
	bool Ui_Context::text_box_delete(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//光标越界先夹到串尾
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		//已在串尾
		if (node->caret >= node->text.size())
			return false;
		//删除后一字符（光标不动）
		const std::size_t end = utf8_next_end(node->text, node->caret);
		node->text.erase(node->caret, end - node->caret);
		return true;
	}

	//光标按码点移动
	bool Ui_Context::caret_move(Ui_Handle handle, int delta)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//光标越界先夹到串尾
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		const std::size_t origin = node->caret;
		//向后移动
		if (delta > 0)
		{
			for (int step = 0; step < delta && node->caret < node->text.size(); ++step)
				node->caret = utf8_next_end(node->text, node->caret);
		}
		//向前移动
		else if (delta < 0)
		{
			for (int step = 0; step < -delta && node->caret > 0; ++step)
				node->caret = utf8_prev_start(node->text, node->caret);
		}
		return node->caret != origin;
	}

	//光标移到串首
	bool Ui_Context::caret_home(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//已在串首
		if (node->caret == 0)
			return false;
		node->caret = 0;
		return true;
	}

	//光标移到串尾
	bool Ui_Context::caret_end(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//越界先夹到串尾
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		//已在串尾
		if (node->caret == node->text.size())
			return false;
		node->caret = node->text.size();
		return true;
	}

	//设置选中区间
	bool Ui_Context::select_set(Ui_Handle handle, std::size_t anchor, std::size_t caret)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//两端各自回退到字符边界
		const std::size_t first = caret_boundary_floor(node->text, anchor);
		const std::size_t second = caret_boundary_floor(node->text, caret);
		//归一化为 begin <= end
		node->select_begin = std::min(first, second);
		node->select_end = std::max(first, second);
		return true;
	}

	//读取选中区间
	void Ui_Context::select_get(Ui_Handle handle, std::size_t& begin_out, std::size_t& end_out) const
	{
		const Ui_Node* node = node_find(handle);
		//未知句柄返回空区间
		if (node == nullptr)
		{
			begin_out = 0;
			end_out = 0;
			return;
		}
		begin_out = node->select_begin;
		end_out = node->select_end;
	}

	//清空选中区间
	bool Ui_Context::select_clear(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->select_begin = 0;
		node->select_end = 0;
		return true;
	}

	//注册剪贴板读写回调
	void Ui_Context::clipboard_handlers_set(std::string (*get_text)(void* user),
		void (*set_text)(const std::string& text, void* user), void* user)
	{
		clipboard_get = get_text;
		clipboard_set = set_text;
		clipboard_user = user;
	}

	//复制：选区优先，否则复制全文（光标不动；第九百三十五轮）
	bool Ui_Context::text_box_copy(Ui_Handle handle)
	{
		const Ui_Node* node = node_find(handle);
		//未注册写入回调
		if (node == nullptr || clipboard_set == nullptr)
			return false;
		//有选区取选区，否则取全文
		const bool has_selection = node->select_end > node->select_begin;
		const std::string copied = has_selection
			? node->text.substr(node->select_begin, node->select_end - node->select_begin)
			: node->text;
		//空文本不写剪贴板
		if (copied.empty())
			return false;
		clipboard_set(copied, clipboard_user);
		return true;
	}

	//粘贴：替换选区，否则插入光标处（第九百三十五轮）
	bool Ui_Context::text_box_paste(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		//未注册读取回调
		if (node == nullptr || clipboard_get == nullptr)
			return false;
		const std::string incoming = clipboard_get(clipboard_user);
		//空剪贴板不改动
		if (incoming.empty())
			return false;
		//光标越界先夹到串尾
		if (node->caret > node->text.size())
			node->caret = node->text.size();
		//有选区按选区替换，否则在光标处插入
		const bool has_selection = node->select_end > node->select_begin
			&& node->select_end <= node->text.size();
		const std::size_t begin = has_selection ? node->select_begin : node->caret;
		const std::size_t end = has_selection ? node->select_end : node->caret;
		node->text.replace(begin, end - begin, incoming);
		//光标落到插入末尾并清空选区
		node->caret = begin + incoming.size();
		node->select_begin = 0;
		node->select_end = 0;
		return true;
	}

	//设置输入法组合文本
	bool Ui_Context::text_box_preedit_set(Ui_Handle handle, const std::string& preedit)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->preedit = preedit;
		return true;
	}

	//读取输入法组合文本
	std::string Ui_Context::text_box_preedit_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		//未知句柄返回空串
		if (node == nullptr)
			return std::string();
		return node->preedit;
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

	//设置面板背景纹理
	bool Ui_Context::panel_texture_set(Ui_Handle handle, uint64_t texture)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->texture = texture;
		return true;
	}

	//读取面板背景纹理
	uint64_t Ui_Context::panel_texture_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->texture;
	}

	//创建图片
	Ui_Handle Ui_Context::image_create(Ui_Handle parent, const Ui_Rect& rect, uint64_t texture)
	{
		//创建图片节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Image);
		//写入纹理与默认着色（白色按原样采样）
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->texture = texture;
			node->color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//设置图片纹理
	bool Ui_Context::image_texture_set(Ui_Handle handle, uint64_t texture)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->texture = texture;
		return true;
	}

	//读取图片纹理
	uint64_t Ui_Context::image_texture_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->texture;
	}

	//设置图片九宫格切分
	bool Ui_Context::image_slice_set(Ui_Handle handle, float left, float top, float right, float bottom)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//负值按 0，超过 1 按 1
		node->slice_left = std::max(0.0f, std::min(left, 1.0f));
		node->slice_top = std::max(0.0f, std::min(top, 1.0f));
		node->slice_right = std::max(0.0f, std::min(right, 1.0f));
		node->slice_bottom = std::max(0.0f, std::min(bottom, 1.0f));
		//左右 / 上下切分之和不超过 1
		if (node->slice_left + node->slice_right > 1.0f)
			node->slice_right = 1.0f - node->slice_left;
		if (node->slice_top + node->slice_bottom > 1.0f)
			node->slice_bottom = 1.0f - node->slice_top;
		return true;
	}

	//读取图片九宫格切分
	void Ui_Context::image_slice_get(Ui_Handle handle, float& left_out, float& top_out,
		float& right_out, float& bottom_out) const
	{
		const Ui_Node* node = node_find(handle);
		left_out = node != nullptr ? node->slice_left : 0.0f;
		top_out = node != nullptr ? node->slice_top : 0.0f;
		right_out = node != nullptr ? node->slice_right : 0.0f;
		bottom_out = node != nullptr ? node->slice_bottom : 0.0f;
	}

	//按纹理像素边界设置图片九宫格切分
	bool Ui_Context::image_slice_pixels_set(Ui_Handle handle, uint32_t texture_width, uint32_t texture_height,
		float left, float top, float right, float bottom)
	{
		//纹理尺寸非正无法换算
		if (texture_width == 0 || texture_height == 0)
			return false;
		//换算为归一化切分（负边距由 image_slice_set 钳制为 0）
		const float width = static_cast<float>(texture_width);
		const float height = static_cast<float>(texture_height);
		return image_slice_set(handle, left / width, top / height, right / width, bottom / height);
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
			//默认引用复选框角色（主题切换时跟随色板；显式 Custom 仍可覆盖）
			node->role = Ui_Role::Checkbox;
			node->color = Ui_Color{ 0.02f, 0.02f, 0.05f, 0.95f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//创建单选按钮
	Ui_Handle Ui_Context::radio_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
		bool checked, void (*on_toggle)(Ui_Handle, bool, void*), void* user)
	{
		//创建单选按钮节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Radio);
		//写入标签、状态与回调（未选中环用中灰，选中环与圆点用强调色）
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->label = label;
			node->checked = checked;
			node->on_toggle = on_toggle;
			node->user = user;
			node->color = Ui_Color{ 0.55f, 0.55f, 0.55f, 1.0f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//设置单选按钮组键
	bool Ui_Context::radio_group_set(Ui_Handle handle, Ui_Handle group)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->radio_group = group;
		return true;
	}

	//读取单选按钮组键
	Ui_Handle Ui_Context::radio_group_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->radio_group;
	}

	//创建开关
	Ui_Handle Ui_Context::switch_create(Ui_Handle parent, const Ui_Rect& rect, bool checked,
		void (*on_toggle)(Ui_Handle, bool, void*), void* user)
	{
		//创建开关节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Switch);
		//写入状态与回调（未选中轨道用中灰）
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->checked = checked;
			node->on_toggle = on_toggle;
			node->user = user;
			node->color = Ui_Color{ 0.28f, 0.28f, 0.32f, 1.0f };
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
			//默认引用滑动条角色（轨道跟随主题底色；显式 Custom 仍可覆盖）
			node->role = Ui_Role::Slider;
			node->color = Ui_Color{ 0.08f, 0.08f, 0.12f, 1.0f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//创建进度条
	Ui_Handle Ui_Context::progress_create(Ui_Handle parent, const Ui_Rect& rect, float value,
		const std::string& label)
	{
		//创建进度条节点
		const Ui_Handle handle = node_create_impl(nodes, parent, rect, Ui_Node_Type::Progress);
		//写入初值（钳到 0..1）、标签与默认色
		Ui_Node* node = node_find(handle);
		if (node != nullptr)
		{
			node->value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
			node->label = label;
			node->role = Ui_Role::Progress;
			node->color = Ui_Color{ 0.16f, 0.16f, 0.20f, 1.0f };
			node->text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		}
		return handle;
	}

	//设置进度条方向
	bool Ui_Context::progress_vertical_set(Ui_Handle handle, bool vertical)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->vertical = vertical;
		return true;
	}

	//读取进度条方向
	bool Ui_Context::progress_vertical_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->vertical;
	}

	//设置滑动条步进
	bool Ui_Context::slider_step_set(Ui_Handle handle, float step)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//负值与非数按 0（连续）
		node->value_step = step > 0.0f ? step : 0.0f;
		return true;
	}

	//读取滑动条步进
	float Ui_Context::slider_step_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0.0f;
		return node->value_step;
	}

	//设置滑动条方向（共用 vertical 字段）
	bool Ui_Context::slider_vertical_set(Ui_Handle handle, bool vertical)
	{
		return progress_vertical_set(handle, vertical);
	}

	//读取滑动条方向
	bool Ui_Context::slider_vertical_get(Ui_Handle handle) const
	{
		return progress_vertical_get(handle);
	}

	//销毁控件（含全部后代）
	bool Ui_Context::destroy(Ui_Handle handle)	{
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

	//设置多行文本布局
	bool Ui_Context::text_layout_set(Ui_Handle handle, bool wrap, float line_height, Ui_Align valign)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_wrap = wrap;
		//负行高按字体高度
		node->line_height = line_height > 0.0f ? line_height : 0.0f;
		node->text_valign = valign;
		return true;
	}

	//读取多行文本布局
	void Ui_Context::text_layout_get(Ui_Handle handle, bool& wrap_out, float& line_height_out,
		Ui_Align& valign_out) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
		{
			wrap_out = false;
			line_height_out = 0.0f;
			valign_out = Ui_Align::Center;
			return;
		}
		wrap_out = node->text_wrap;
		line_height_out = node->line_height;
		valign_out = node->text_valign;
	}

	//设置单行省略
	bool Ui_Context::text_ellipsis_set(Ui_Handle handle, bool ellipsis)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_ellipsis = ellipsis;
		return true;
	}

	//读取单行省略
	bool Ui_Context::text_ellipsis_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		return node->text_ellipsis;
	}

	//设置多行文本水平对齐
	bool Ui_Context::text_halign_set(Ui_Handle handle, Ui_Align align)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_halign = align;
		return true;
	}

	//读取多行文本水平对齐
	Ui_Align Ui_Context::text_halign_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return Ui_Align::Start;
		return node->text_halign;
	}

	//设置字间距
	bool Ui_Context::letter_spacing_set(Ui_Handle handle, float spacing)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->letter_spacing = spacing;
		return true;
	}

	//读取字间距
	float Ui_Context::letter_spacing_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0.0f;
		return node->letter_spacing;
	}

	//设置首行缩进
	bool Ui_Context::text_indent_set(Ui_Handle handle, float indent)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_indent = indent;
		return true;
	}

	//读取首行缩进
	float Ui_Context::text_indent_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0.0f;
		return node->text_indent;
	}

	//设置文本阴影
	bool Ui_Context::text_shadow_set(Ui_Handle handle, const Ui_Text_Shadow& shadow)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_shadow = shadow;
		return true;
	}

	//读取文本阴影
	Ui_Text_Shadow Ui_Context::text_shadow_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return Ui_Text_Shadow{};
		return node->text_shadow;
	}

	//添加行内着色区间
	bool Ui_Context::text_span_add(Ui_Handle handle, std::size_t begin, std::size_t end, const Ui_Color& color,
		uint64_t font)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		//夹到文本范围内
		std::size_t from = begin > node->text.size() ? node->text.size() : begin;
		std::size_t to = end > node->text.size() ? node->text.size() : end;
		//对齐到字符边界（落在续字节则后移到字符头）
		while (from < node->text.size() && (static_cast<unsigned char>(node->text[from]) & 0xC0) == 0x80)
			++from;
		while (to < node->text.size() && (static_cast<unsigned char>(node->text[to]) & 0xC0) == 0x80)
			++to;
		//空区间不登记
		if (from >= to)
			return false;
		node->text_spans.push_back(Ui_Text_Span{ from, to, color, font });
		return true;
	}

	//清空行内着色区间
	bool Ui_Context::text_span_clear(Ui_Handle handle)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_spans.clear();
		return true;
	}

	//取行内着色区间数量
	std::size_t Ui_Context::text_span_count(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->text_spans.size();
	}

	//设置控件专用字体
	bool Ui_Context::text_font_set(Ui_Handle handle, uint64_t font)
	{
		Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return false;
		node->text_font = font;
		return true;
	}

	//读取控件专用字体
	uint64_t Ui_Context::text_font_get(Ui_Handle handle) const
	{
		const Ui_Node* node = node_find(handle);
		if (node == nullptr)
			return 0;
		return node->text_font;
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

	//按系统输入对象名查找首个存活控件
	Ui_Handle Ui_Context::node_find_by_input_name(const std::string& name) const
	{
		//空名不参与匹配
		if (name.empty())
			return 0;
		//按创建顺序取首个匹配
		for (const Ui_Node& node : nodes)
		{
			if (node.alive && node.input_name == name)
				return node.handle;
		}
		return 0;
	}

	//直接子节点数量
	std::size_t Ui_Context::child_count(Ui_Handle handle) const
	{
		//父节点无效返回 0
		if (node_find(handle) == nullptr)
			return 0;
		std::size_t count = 0;
		//只计存活且父句柄匹配的节点
		for (const Ui_Node& node : nodes)
		{
			if (node.alive && node.parent == handle)
				++count;
		}
		return count;
	}

	//按创建顺序取直接子节点
	Ui_Handle Ui_Context::child_at(Ui_Handle handle, std::size_t index) const
	{
		//父节点无效返回 0
		if (node_find(handle) == nullptr)
			return 0;
		std::size_t current = 0;
		//按创建顺序推进到第 index 个存活子节点
		for (const Ui_Node& node : nodes)
		{
			if (!node.alive || node.parent != handle)
				continue;
			if (current == index)
				return node.handle;
			++current;
		}
		return 0;
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
