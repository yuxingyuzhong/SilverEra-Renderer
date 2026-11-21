#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//帧输入快照（SDL-free）
#include "Engine/EngineRenderer/src/render/Input/输入快照.h"

//游戏引擎命名空间
namespace engine
{
	//UI 域内总线（定义在 core/UI本地总线.h）
	class Ui_Local_Bus;

	//UI 控件句柄（0 = 无效）
	using Ui_Handle = uint64_t;

	//UI 控件类型
	enum class Ui_Node_Type : uint8_t
	{
		//根节点（覆盖整个窗口，不绘制）
		Root,
		//面板（矩形背景，不参与交互）
		Panel,
		//按钮（矩形背景 + 居中文本）
		Button,
		//单行文本框（矩形背景 + 左对齐文本 + 光标）
		Text_Box,
		//纯文本标签（不参与交互）
		Label,
		//复选框（方框 + 勾选 + 右侧标签）
		Checkbox,
		//滑动条（轨道 + 滑块，值域 0..1）
		Slider
	};

	//UI 像素矩形（左上原点、Y 向下）
	struct Ui_Rect
	{
		//左上角
		float x = 0.0f;
		float y = 0.0f;
		//宽高
		float w = 0.0f;
		float h = 0.0f;

		//点是否落在矩形内
		bool contains(float px, float py) const
		{
			return px >= x && px < x + w && py >= y && py < y + h;
		}
	};

	//UI 颜色（RGBA，分量 0..1）
	struct Ui_Color
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
	};

	//UI 控件节点（公开只读视图；可变状态由 Ui_Context 维护）
	struct Ui_Node
	{
		//句柄（0 = 无效 / 已销毁）
		Ui_Handle handle = 0;
		//父句柄（0 = 根节点）
		Ui_Handle parent = 0;
		//控件类型
		Ui_Node_Type type = Ui_Node_Type::Root;
		//像素矩形
		Ui_Rect rect{};
		//背景色（面板 / 按钮 / 文本框）
		Ui_Color color{};
		//文本色（按钮标签 / 文本框内容）
		Ui_Color text_color{};
		//按钮标签
		std::string label{};
		//文本框内容（UTF-8）
		std::string text{};
		//文本框光标（字节偏移）
		std::size_t caret = 0;
		//是否可见
		bool visible = true;
		//是否可用（不可用不参与命中）
		bool enabled = true;
		//是否悬停
		bool hovered = false;
		//是否按下
		bool pressed = false;
		//是否持有焦点
		bool focused = false;
		//是否持有系统侧焦点（由 FocusChanged 事件映射到本控件）
		bool system_focused = false;
		//系统输入对象名（空 = 不参与系统焦点/文本路由）
		std::string input_name{};
		//本帧是否被点击（可由 clicked_consume 取走）
		bool clicked = false;
		//复选框选中状态
		bool checked = false;
		//滑动条值（0..1）
		float value = 0.0f;
		//点击回调
		void (*on_click)(Ui_Handle handle, void* user) = nullptr;
		//复选框切换回调
		void (*on_toggle)(Ui_Handle handle, bool checked, void* user) = nullptr;
		//滑动条取值回调
		void (*on_value)(Ui_Handle handle, float value, void* user) = nullptr;
		//回调用户数据
		void* user = nullptr;
		//是否存活（内部使用；0 = 已销毁）
		bool alive = true;
	};

	/*
	UI 上下文（保留模式控件树）
	控件按创建顺序存放；绘制顺序同存放顺序（父先于子），命中测试按逆序（后创建者在上）。
	交互由 frame_begin 消费帧输入，绘制由渲染层内部的 Ui_Renderer 完成。
	*/
	class Ui_Context
	{
	public:
		//构造（自动建立根节点与域内总线）
		Ui_Context(void);
		//析构（复位控件树与域内总线）
		~Ui_Context(void);
		//不可拷贝、不可移动（持有控件集合）
		Ui_Context(const Ui_Context&) = delete;
		Ui_Context& operator=(const Ui_Context&) = delete;

		//UI 域内总线（首次访问时创建；控件事件统一经它派发）
		Ui_Local_Bus& bus_get(void);

		//复位控件树并重建根节点
		void build(void);
		//清空控件树与交互状态
		void shutdown(void);
		//根节点句柄
		Ui_Handle root_get(void) const;

		//创建面板（parent 无效时挂到根节点）
		Ui_Handle panel_create(Ui_Handle parent, const Ui_Rect& rect, const Ui_Color& color);
		//创建按钮
		Ui_Handle button_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
			void (*on_click)(Ui_Handle, void*) = nullptr, void* user = nullptr, const Ui_Color& color = Ui_Color{ 0.20f, 0.20f, 0.50f, 1.0f });
		//创建单行文本框
		Ui_Handle text_box_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text);
		//创建纯文本标签（不参与交互）
		Ui_Handle label_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text,
			const Ui_Color& text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f });
		//创建复选框（rect 左侧为方框，右侧为标签）
		Ui_Handle checkbox_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
			bool checked = false, void (*on_toggle)(Ui_Handle, bool, void*) = nullptr, void* user = nullptr);
		//创建滑动条（值域 0..1）
		Ui_Handle slider_create(Ui_Handle parent, const Ui_Rect& rect, float value,
			void (*on_value)(Ui_Handle, float, void*) = nullptr, void* user = nullptr);
		//销毁控件（含全部后代）
		bool destroy(Ui_Handle handle);

		//设置矩形
		bool rect_set(Ui_Handle handle, const Ui_Rect& rect);
		//取矩形
		bool rect_get(Ui_Handle handle, Ui_Rect& rect_out) const;
		//设置背景色
		bool color_set(Ui_Handle handle, const Ui_Color& color);
		//设置按钮标签
		bool label_set(Ui_Handle handle, const std::string& label);
		//设置文本框内容（光标移到末尾）
		bool text_set(Ui_Handle handle, const std::string& text);
		//取文本框内容
		bool text_get(Ui_Handle handle, std::string& text_out) const;
		//复选框选中状态读取 / 设置
		bool checked_get(Ui_Handle handle) const;
		bool checked_set(Ui_Handle handle, bool checked);
		//滑动条值读取 / 设置（0..1 钳制）
		bool value_get(Ui_Handle handle, float& value_out) const;
		bool value_set(Ui_Handle handle, float value);
		//设置可见
		bool visible_set(Ui_Handle handle, bool visible);
		//设置可用
		bool enabled_set(Ui_Handle handle, bool enabled);
		//取走单击标记（取走即清零）
		bool clicked_consume(Ui_Handle handle);
		//是否悬停
		bool hovered_get(Ui_Handle handle) const;
		//是否持有焦点
		bool focused_get(Ui_Handle handle) const;
		//当前焦点控件的系统输入对象名（空 = 无焦点或该控件未绑定）
		const std::string& focused_input_name_get(void) const;
		//清除本地焦点（系统焦点离开 UI 主对象时由事件桥调用）
		void focus_clear(void);
		//系统子焦点回写到本地焦点（仅命中同名可聚焦控件；空名不处理）
		void system_focus_sync(const std::string& child);
		//键盘交互总闸（系统焦点不在 UI 主对象时关闭，鼠标交互不受影响）
		void keyboard_gate_set(bool enabled);
		bool keyboard_gate_get(void) const;

		//当前是否有文本框持有焦点（渲染层据此启停平台文本输入）
		bool text_focus_active(void) const;
		//当前焦点文本框的系统输入对象名（空 = 未登记 / 非文本框）
		const std::string& text_focus_input_name(void) const;
		/*
		系统事件交互（由 UI 事件桥调用）
		input_name：把控件绑定到系统输入对象名（空 = 不参与）；
		input_root_name / input_outside_name：UI 主对象名与点击 UI 外部时的目标对象名；
		modal_set / modal_clear：UI 模态根（需已绑定 input_name）；
		command_bind：把系统语义命令绑定到控件；
		system_focus_apply：把 FocusChanged 的主/子/模态/文本状态映射到控件；
		system_text_apply：把系统文本语义写入当前焦点文本框。
		*/
		bool input_name_set(Ui_Handle handle, const std::string& name);
		std::string input_name_get(Ui_Handle handle) const;
		void input_root_name_set(const std::string& name);
		const std::string& input_root_name_get(void) const;
		void input_outside_name_set(const std::string& name);
		const std::string& input_outside_name_get(void) const;
		bool modal_set(Ui_Handle handle);
		void modal_clear(void);
		const std::string& modal_input_name_get(void) const;
		void command_bind(const std::string& command, Ui_Handle handle);
		void command_unbind(const std::string& command);
		bool command_dispatch(const std::string& command);
		//控件绑定的语义命令（未绑定返回空串）
		std::string command_of(Ui_Handle handle) const;
		void system_focus_apply(const std::string& main, const std::string& child,
			const std::string& modal, bool text);
		bool system_focused_get(Ui_Handle handle) const;
		bool system_text_apply(const std::string& text);
		//本帧按下的控件系统输入对象名（空 = 未命中带名字的控件）
		const std::string& pressed_input_name_get(void) const;
		//本帧是否按在 UI 外部（指针未命中任何 UI 控件）
		bool press_outside_get(void) const;
		//本帧是否发生左键按下
		bool press_any_get(void) const;
		//文本是否经系统文本会话路由（由事件桥按系统文本模式设置）
		void text_route_set(bool route);
		bool text_route_get(void) const;
		//设置 UI 使用的字体句柄（0 = 不绘制文本）
		void font_set(uint64_t font);
		//当前 UI 字体句柄
		uint64_t font_get(void) const;

		//控件节点数（含根节点与已销毁槽位）
		std::size_t node_count(void) const;
		//按槽位取控件节点（越界返回空）
		const Ui_Node* node_at(std::size_t index) const;

		/*
		帧交互：消费输入快照
		更新根节点尺寸、悬停 / 按下 / 点击、文本框焦点与文本编辑；每帧调用一次。
		*/
		void frame_begin(const Input_Frame& input);

	private:
		//UI 域内总线（控件树内部事件的路由中心）
		std::unique_ptr<Ui_Local_Bus> local_bus{};
		//控件集合（下标 + 1 = 句柄）
		std::vector<Ui_Node> nodes{};
		//当前悬停控件
		Ui_Handle hovered = 0;
		//当前按下控件
		Ui_Handle pressed = 0;
		//当前焦点控件
		Ui_Handle focused = 0;
		//滑动条拖拽中的控件
		Ui_Handle slider_drag = 0;
		//UI 字体句柄
		uint64_t font = 0;
		//UI 主对象名（系统输入对象）
		std::string input_root_name{};
		//点击 UI 外部时的系统焦点目标名
		std::string input_outside_name{};
		//模态根的系统输入对象名
		std::string modal_input_name{};
		//本帧按下的系统输入对象名
		std::string pressed_input_name{};
		//本帧是否按在 UI 外部
		bool press_outside = false;
		//本帧是否发生左键按下
		bool press_any = false;
		//文本是否经系统文本会话路由
		bool text_route = false;
		//语义命令 → 控件绑定
		std::unordered_map<std::string, Ui_Handle> command_bindings{};
		//键盘交互总闸（默认开启，未接入系统时仍可独立使用）
		bool keyboard_gate = true;

		//按句柄查找控件 —— 非常量重载
		Ui_Node* node_find(Ui_Handle handle);
		//按句柄查找控件 —— 常量重载
		const Ui_Node* node_find(Ui_Handle handle) const;
		//释放槽位（标记销毁并清空辅助状态）
		void node_release(Ui_Handle handle);
		//命中测试（返回最上层可见且可用的控件句柄；无命中返回 0）
		Ui_Handle hit_test(float px, float py) const;
		//焦点设置（0 = 清除焦点）
		void focus_set(Ui_Handle handle);
		//控件是否可获得焦点（按钮 / 文本框 / 复选框 / 滑动条）
		static bool node_focusable(const Ui_Node& node);
		//Tab 焦点导航（forward 为真向前，否则向后）
		void focus_navigate(bool forward);
		//滑动条按指针横坐标更新取值（返回是否发生变化）
		bool slider_value_apply(Ui_Node& node, float pointer_x);
		//文本框文本编辑（消费本帧文本与编辑键）
		void text_edit(Ui_Node& node, const Input_Frame& input);
	};
}
