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
		//图片（纹理四边形，不参与交互；第八百九十三轮）
		Image,
		//复选框（方框 + 勾选 + 右侧标签）
		Checkbox,
		//滑动条（轨道 + 滑块，值域 0..1）
		Slider,
		//进度条（背景 + 按值填充前景 + 可选居中标签；第九百一十五轮）
		Progress,
		//单选按钮（圆形环 + 选中圆点 + 右侧标签；第九百二十六轮）
		Radio,
		//开关（圆角轨道 + 左右滑块；第九百二十七轮）
		Switch
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

	//UI 锚点（子控件相对父矩形的九个基准位置）
	enum class Ui_Anchor : uint8_t
	{
		//左上
		Top_Left,
		//上中
		Top,
		//右上
		Top_Right,
		//左中
		Left,
		//正中
		Center,
		//右中
		Right,
		//左下
		Bottom_Left,
		//下中
		Bottom,
		//右下
		Bottom_Right
	};

	/*
	UI 锚点布局（纯数据；第八百七十六 / 八百七十七轮）
	按 anchor 决定基准点，offset_x / offset_y 为相对基准点的像素偏移；
	宽度取 width（>0）否则取 width_percent × 父宽，高度同口径；再按 min / max 夹紧。
	active 为真时由控件树在根尺寸变化后统一重算 rect。
	*/
	struct Ui_Layout
	{
		//锚点
		Ui_Anchor anchor = Ui_Anchor::Top_Left;
		//相对锚点的水平偏移（像素）
		float offset_x = 0.0f;
		//相对锚点的垂直偏移（像素）
		float offset_y = 0.0f;
		//期望宽度（像素；<=0 时看 width_percent）
		float width = 0.0f;
		//期望高度（像素；<=0 时看 height_percent）
		float height = 0.0f;
		//百分比宽度（0..1；width <= 0 时生效，超出按 1 处理）
		float width_percent = 0.0f;
		//百分比高度（0..1；height <= 0 时生效，超出按 1 处理）
		float height_percent = 0.0f;
		//最小宽度（像素；<=0 表示不限制）
		float min_width = 0.0f;
		//最小高度（像素；<=0 表示不限制）
		float min_height = 0.0f;
		//最大宽度（像素；<=0 表示不限制）
		float max_width = 0.0f;
		//最大高度（像素；<=0 表示不限制）
		float max_height = 0.0f;
		//是否启用（假 = 使用节点自身矩形）
		bool active = false;
	};

	//按父矩形与布局求子矩形（父矩形宽高非正时返回零矩形）
	Ui_Rect ui_layout_rect_resolve(const Ui_Rect& parent, const Ui_Layout& layout);

	//UI 排列方向（容器对直接子节点的排布方式）
	enum class Ui_Flow : uint8_t
	{
		//不排列（子节点按自身布局 / 矩形定位）
		None,
		//横向排列
		Horizontal,
		//纵向排列
		Vertical
	};

	//UI 主轴分布（换行流式排列的行内分布；第九百五十一轮）
	enum class Ui_Justify : uint8_t
	{
		//起始边（剩余留尾）
		Start,
		//居中
		Center,
		//末端（剩余留首）
		End,
		//两端对齐（行内均匀分布，子项少于两个时按 Start）
		Space_Between
	};

	//UI 交叉轴对齐（排列容器的子节点在非排列方向上的对齐方式）
	enum class Ui_Align : uint8_t
	{
		//起始边
		Start,
		//居中
		Center,
		//结束边
		End,
		//拉伸到内容区尺寸
		Stretch
	};

	//UI 样式角色（决定控件从主题取哪一组颜色）
	enum class Ui_Role : uint8_t
	{
		//自定义（直接使用节点颜色）
		Custom,
		//面板
		Panel,
		//按钮
		Button,
		//文本框
		Text_Box,
		//标签
		Label,
		//复选框
		Checkbox,
		//滑动条
		Slider,
		//进度条（第九百二十二轮）
		Progress
	};

	//UI 视觉状态（主题按状态给出颜色）
	enum class Ui_Visual_State : uint8_t
	{
		//常态
		Normal,
		//悬停
		Hovered,
		//按下
		Pressed,
		//禁用
		Disabled,
		//焦点
		Focused
	};

	/*
	UI 主题色板（第八百八十三轮）
	默认值与既有绘制观感对齐；节点通过 Ui_Role 引用，颜色不再写死在节点里。
	*/
	struct Ui_Theme
	{
		//面板背景
		Ui_Color panel{ 0.12f, 0.12f, 0.16f, 1.0f };
		//文本框背景
		Ui_Color text_box{ 0.10f, 0.10f, 0.14f, 1.0f };
		//按钮常态
		Ui_Color button{ 0.20f, 0.20f, 0.50f, 1.0f };
		//按钮悬停
		Ui_Color button_hover{ 0.32f, 0.32f, 0.62f, 1.0f };
		//按钮按下
		Ui_Color button_pressed{ 0.14f, 0.14f, 0.36f, 1.0f };
		//主文本
		Ui_Color text{ 1.0f, 1.0f, 1.0f, 1.0f };
		//次级文本（禁用）
		Ui_Color text_dim{ 0.55f, 0.55f, 0.55f, 1.0f };
		//强调色（选中 / 滑块 / 焦点）
		Ui_Color accent{ 0.30f, 0.60f, 1.0f, 1.0f };
		//禁用背景
		Ui_Color disabled{ 0.35f, 0.35f, 0.35f, 1.0f };
	};

	//按角色与视觉状态取主题色（Custom 返回白色；未知组合回退到常态色）
	Ui_Color ui_theme_color(const Ui_Theme& theme, Ui_Role role, Ui_Visual_State state);

	/*
	文本阴影（第九百四十轮）
	alpha 为 0 表示关闭；开启时先按偏移绘制一份阴影副本，再绘制正文。
	*/
	struct Ui_Text_Shadow
	{
		//水平偏移（像素）
		float offset_x = 0.0f;
		//垂直偏移（像素）
		float offset_y = 0.0f;
		//阴影颜色（alpha 为 0 表示不绘制）
		Ui_Color color{ 0.0f, 0.0f, 0.0f, 0.0f };
	};

	/*
	行内着色区间（第九百四十一轮）
	begin / end 为节点文本的字节区间 [begin, end)；可重叠，后添加者优先。
	*/
	struct Ui_Text_Span
	{
		//起点（字节偏移）
		std::size_t begin = 0;
		//终点（字节偏移，不含）
		std::size_t end = 0;
		//区间颜色
		Ui_Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
		//区间字体（0 = 使用控件字体；第九百四十二轮）
		uint64_t font = 0;
	};

	/*
	布局快照条目（第九百五十四轮）
	按树序输出子树节点的句柄 / 父句柄 / 类型 / 当前矩形。
	*/
	struct Ui_Layout_Item
	{
		//节点句柄
		Ui_Handle handle = 0;
		//父句柄（0 = 根）
		Ui_Handle parent = 0;
		//控件类型
		Ui_Node_Type type = Ui_Node_Type::Root;
		//当前矩形
		Ui_Rect rect{};
	};

	//UI 控件节点（公开只读视图；可变状态由 Ui_Context 维护）
	struct Ui_Node	{
		//句柄（0 = 无效 / 已销毁）
		Ui_Handle handle = 0;
		//父句柄（0 = 根节点）
		Ui_Handle parent = 0;
		//控件类型
		Ui_Node_Type type = Ui_Node_Type::Root;
		//像素矩形（已叠加滚动偏移；绘制与命中共用）
		Ui_Rect rect{};
		//滚动偏移前的基准矩形（内部使用；每帧滚动重算据此还原）
		Ui_Rect rect_base{};
		//内容滚动偏移（像素；>=0；作用于全部后代，第八百八十九轮）
		float scroll_x = 0.0f;
		float scroll_y = 0.0f;
		//滚动目标容器句柄（0 = 非滚动条；第八百九十轮）
		Ui_Handle scroll_target = 0;
		//滚动内容长度（像素；滚动条用于换算最大偏移）
		float scroll_content = 0.0f;
		//滚动条是否纵向（真 = 按指针纵坐标取值，假 = 横坐标）
		bool scroll_vertical = true;
		//锚点布局（active 为真时由控件树按父矩形重算 rect）
		Ui_Layout layout{};
		//排列方向（None 时不排列直接子节点）
		Ui_Flow flow = Ui_Flow::None;
		//排列间距（像素；负值按 0）
		float flow_gap = 0.0f;
		//排列内边距（像素；四边相同，负值按 0）
		float flow_padding = 0.0f;
		//内容内边距（像素；标签文本区四边内缩；第九百四十七轮）
		float padding = 0.0f;
		//流式排列外边距（像素；子项前后各留该间距；第九百四十七轮）
		float margin = 0.0f;
		//流式主轴弹性权重（>0 参与剩余空间按权重分配；第九百四十八轮）
		float stretch = 0.0f;
		//流式换行（主轴排满后另起一行 / 列；第九百四十九轮）
		bool flow_wrap = false;
		//换行行内主轴分布（第九百五十一轮）
		Ui_Justify flow_justify = Ui_Justify::Start;
		//排列交叉轴对齐
		Ui_Align flow_align = Ui_Align::Start;
		//样式角色（Custom = 直接使用下面的 color）
		Ui_Role role = Ui_Role::Custom;
		//不透明度（0..1；第八百八十五轮）
		float opacity = 1.0f;
		//圆角半径（像素；0 = 直角；第八百八十七轮）
		float corner_radius = 0.0f;
		//描边宽度（像素；<=0 = 无描边）
		float border_width = 0.0f;
		//描边颜色
		Ui_Color border_color{ 0.0f, 0.0f, 0.0f, 1.0f };
		//背景色（面板 / 按钮 / 文本框）
		Ui_Color color{};
		//文本色（按钮标签 / 文本框内容）
		Ui_Color text_color{};
		//按钮标签
		std::string label{};
		//图片纹理句柄（0 = 不绘制；第八百九十三轮）
		uint64_t texture = 0;
		//图片九宫格切分（归一化 0..1；全 0 = 整图拉伸；第九百零六轮）
		float slice_left = 0.0f;
		float slice_top = 0.0f;
		float slice_right = 0.0f;
		float slice_bottom = 0.0f;
		//文本框内容（UTF-8）
		std::string text{};
		//是否自动换行（仅标签；第八百九十四轮）
		bool text_wrap = false;
		//多行行高（像素；0 = 字体高度）
		float line_height = 0.0f;
		//字间距（像素；逐码点推进后追加；第九百三十八轮）
		float letter_spacing = 0.0f;
		//首行缩进（像素；仅首行起始右移、首行可用宽度扣减；第九百三十九轮）
		float text_indent = 0.0f;
		//文本阴影（alpha 为 0 关闭；第九百四十轮）
		Ui_Text_Shadow text_shadow{};
		//行内着色区间（按加入顺序覆盖；第九百四十一轮）
		std::vector<Ui_Text_Span> text_spans{};
		//多行文本垂直对齐（单行默认居中，保持既有观感）
		Ui_Align text_valign = Ui_Align::Center;
		//多行文本水平对齐（第九百二十一轮；Stretch 按 Start 处理）
		Ui_Align text_halign = Ui_Align::Start;
		//单行超宽时尾部省略（第八百九十五轮；仅非自动换行时生效）
		bool text_ellipsis = false;
		//控件专用字体句柄（0 = 使用 UI 全局字体；第八百九十六轮）
		uint64_t text_font = 0;
		//光标位置（字节偏移；第九百三十轮起由 caret_set 维护边界）
		std::size_t caret = 0;
		//选中区间起点 / 终点（字节偏移，begin <= end；第九百三十三轮）
		std::size_t select_begin = 0;
		std::size_t select_end = 0;
		//输入法组合文本（UTF-8；空 = 非组合态；第九百三十六轮）
		std::string preedit{};
		//是否可见
		bool visible = true;
		//是否裁剪直接子节点（假 = 子节点可溢出父矩形；第八百八十八轮）
		bool clip_children = false;
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
		//单选按钮组键（0 = 与同父单选互斥；第九百二十六轮）
		Ui_Handle radio_group = 0;
		//滑动条值（0..1）
		float value = 0.0f;
		//显示数值（供绘制；动画开启时向 value 逐帧趋近；第九百四十五轮）
		float display_value = 0.0f;
		//数值插值速率（每秒；<= 0 关闭；第九百四十五轮）
		float anim_speed = 0.0f;
		//进度条 / 滑动条是否纵向（假 = 横向；第九百二十三轮）
		bool vertical = false;
		//滑动条交互步进（>0 时按该档距量化；0 = 连续；第九百二十四轮）
		float value_step = 0.0f;
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
		/*
		设置 / 读取面板背景纹理（第九百一十轮）
		texture 为 0 时走纯色 / 圆角 / 描边背景；非 0 时以节点颜色着色并支持九宫格切分。
		未知句柄设置返回假，读取返回 0。
		*/
		bool panel_texture_set(Ui_Handle handle, uint64_t texture);
		uint64_t panel_texture_get(Ui_Handle handle) const;
		//创建按钮
		Ui_Handle button_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
			void (*on_click)(Ui_Handle, void*) = nullptr, void* user = nullptr, const Ui_Color& color = Ui_Color{ 0.20f, 0.20f, 0.50f, 1.0f });
		//创建单行文本框
		Ui_Handle text_box_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text);
		/*
		设置 / 读取文本框内容（第九百三十轮）
		设置后光标移到新文本末尾（天然落在 UTF-8 字符边界）；未知句柄设置返回假、读取返回空串。
		*/
		bool text_box_text_set(Ui_Handle handle, const std::string& text);
		std::string text_box_text_get(Ui_Handle handle) const;
		//设置 / 读取光标（按 UTF-8 字符边界回退并夹紧；未知句柄设置返回假、读取返回 0）
		bool caret_set(Ui_Handle handle, std::size_t caret);
		std::size_t caret_get(Ui_Handle handle) const;
		/*
		退格 / 删除编辑（第九百三十一轮）
		backspace 删除光标前一字符并把光标前移；delete 删除光标后一字符、光标不动。
		均按 UTF-8 字符边界处理；返回是否发生编辑（无可删内容 / 未知句柄返回假）。
		*/
		bool text_box_backspace(Ui_Handle handle);
		bool text_box_delete(Ui_Handle handle);
		/*
		光标移动（第九百三十二轮）
		caret_move 按 UTF-8 码点移动 delta 步（正数后移、负数前移），越界夹到串首 / 串尾；
		caret_home / caret_end 移到串首 / 串尾。三者返回光标是否发生移动（未知句柄返回假）。
		*/
		bool caret_move(Ui_Handle handle, int delta);
		bool caret_home(Ui_Handle handle);
		bool caret_end(Ui_Handle handle);
		/*
		选中区间（第九百三十三轮）
		select_set 以 anchor / caret 两个字节偏移表示选区：两者各自按 UTF-8 边界回退并夹紧，
		再归一化为 begin <= end 存回 `select_begin / select_end`；
		select_get 返回归一化区间（未知句柄返回 0, 0）；select_clear 清空选区。
		*/
		bool select_set(Ui_Handle handle, std::size_t anchor, std::size_t caret);
		void select_get(Ui_Handle handle, std::size_t& begin_out, std::size_t& end_out) const;
		bool select_clear(Ui_Handle handle);
		/*
		剪贴板交互（第九百三十五轮）
		clipboard_handlers_set 注册读写回调（传空即清除）；text_box_copy 有选区时复制选区、
		否则复制全文，光标不动；text_box_paste 用剪贴板文本替换选区（无选区则插入光标处），
		光标落到插入末尾并清空选区。
		未注册对应回调、未知句柄、空文本一律返回假且不改动控件。
		*/
		void clipboard_handlers_set(std::string (*get_text)(void* user),
			void (*set_text)(const std::string& text, void* user), void* user);
		bool text_box_copy(Ui_Handle handle);
		bool text_box_paste(Ui_Handle handle);
		/*
		输入法组合文本（第九百三十六轮）
		preedit 为组合期间未上屏的文本（UTF-8），绘制在光标处且不计入正文与选区；
		设置非空即进入组合态，置空即退出。提交由调用方把 preedit 并入正文后清空。
		*/
		bool text_box_preedit_set(Ui_Handle handle, const std::string& preedit);
		std::string text_box_preedit_get(Ui_Handle handle) const;
		//创建纯文本标签（不参与交互）
		Ui_Handle label_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& text,
			const Ui_Color& text_color = Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f });
		//创建图片（纹理铺满矩形；texture 为纹理模块句柄，0 = 暂不绘制）
		Ui_Handle image_create(Ui_Handle parent, const Ui_Rect& rect, uint64_t texture);
		//设置 / 读取图片纹理（未知句柄设置返回假）
		bool image_texture_set(Ui_Handle handle, uint64_t texture);
		uint64_t image_texture_get(Ui_Handle handle) const;
		/*
		设置 / 读取图片九宫格切分（第九百零六轮）
		参数为归一化 UV 切分（0..1）：left / top 为左上切分，right / bottom 为右下切分；
		负值按 0，且 left + right、top + bottom 各自不超过 1（超出按 1 截断）。
		全 0 时退化为整图拉伸；未知句柄设置返回假，读取返回全 0。
		*/
		bool image_slice_set(Ui_Handle handle, float left, float top, float right, float bottom);
		void image_slice_get(Ui_Handle handle, float& left_out, float& top_out,
			float& right_out, float& bottom_out) const;
		/*
		按纹理像素边界设置九宫格切分（第九百零七轮）
		texture_width / texture_height 为纹理像素尺寸（非正返回假），left..bottom 为像素边距；
		换算为归一化切分后复用 image_slice_set 的钳制口径。
		*/
		bool image_slice_pixels_set(Ui_Handle handle, uint32_t texture_width, uint32_t texture_height,
			float left, float top, float right, float bottom);
		//创建复选框（rect 左侧为方框，右侧为标签）
		Ui_Handle checkbox_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
			bool checked = false, void (*on_toggle)(Ui_Handle, bool, void*) = nullptr, void* user = nullptr);
		/*
		创建单选按钮（第九百二十六轮）
		圆形空心环 + 选中中心圆点，label 绘制在右侧；点击选中后同组其余项自动取消。
		组键取 `radio_group`（0 = 同父为组）；再次点击已选中项不取消。
		*/
		Ui_Handle radio_create(Ui_Handle parent, const Ui_Rect& rect, const std::string& label,
			bool checked = false, void (*on_toggle)(Ui_Handle, bool, void*) = nullptr, void* user = nullptr);
		//设置 / 读取单选按钮组键（0 = 同父为组；未知句柄设置返回假、读取返回 0）
		bool radio_group_set(Ui_Handle handle, Ui_Handle group);
		Ui_Handle radio_group_get(Ui_Handle handle) const;
		/*
		创建开关（第九百二十七轮）
		圆角轨道 + 左右滑块；选中时滑块靠右、轨道取主题强调色。
		复用 `checked` 与 `on_toggle`，交互语义同复选框（点击 / 回车 / 语义命令切换）。
		*/
		Ui_Handle switch_create(Ui_Handle parent, const Ui_Rect& rect, bool checked,
			void (*on_toggle)(Ui_Handle, bool, void*) = nullptr, void* user = nullptr);
		//创建滑动条（值域 0..1）
		Ui_Handle slider_create(Ui_Handle parent, const Ui_Rect& rect, float value,
			void (*on_value)(Ui_Handle, float, void*) = nullptr, void* user = nullptr);
		/*
		创建进度条（第九百一十五轮）
		背景取节点颜色，前景取主题强调色并按 value（0..1 夹紧）填充宽度；
		label 非空时按矩形水平居中绘制。进度条不参与交互。
		*/
		Ui_Handle progress_create(Ui_Handle parent, const Ui_Rect& rect, float value,
			const std::string& label = "");
		/*
		设置 / 读取进度条方向（第九百二十三轮）
		纵向时前景自底向上按 value 填充；未知句柄设置返回假、读取返回假。
		*/
		bool progress_vertical_set(Ui_Handle handle, bool vertical);
		bool progress_vertical_get(Ui_Handle handle) const;
		/*
		设置 / 读取滑动条步进（第九百二十四轮）
		step > 0 时，指针交互结果按 step 的整数倍量化并夹紧 0..1；step <= 0 表示连续。
		程序侧 value_set 不受步进约束。未知句柄设置返回假、读取返回 0。
		*/
		bool slider_step_set(Ui_Handle handle, float step);
		float slider_step_get(Ui_Handle handle) const;
		/*
		设置 / 读取滑动条方向（第九百二十五轮）
		与进度条共用 `Ui_Node.vertical` 字段；纵向滑动条自下而上取值，滚动条绑定路径不受影响。
		*/
		bool slider_vertical_set(Ui_Handle handle, bool vertical);
		bool slider_vertical_get(Ui_Handle handle) const;
		//销毁控件（含全部后代）
		bool destroy(Ui_Handle handle);

		//设置矩形
		bool rect_set(Ui_Handle handle, const Ui_Rect& rect);
		//取矩形
		bool rect_get(Ui_Handle handle, Ui_Rect& rect_out) const;
		/*
		设置锚点布局（第八百七十六轮）
		启用后节点矩形由父矩形与布局决定；未知句柄返回假。
		`rect_set` 会把该节点布局置为不启用（显式矩形优先）。
		*/
		bool layout_set(Ui_Handle handle, const Ui_Layout& layout);
		//取锚点布局
		bool layout_get(Ui_Handle handle, Ui_Layout& layout_out) const;
		/*
		设置排列容器（第八百七十八轮）
		按创建顺序把直接子节点的位置排到父内容区内（保留各自尺寸）；
		子节点若同时启用锚点布局，排列只覆盖位置，尺寸仍取布局结果。
		*/
		bool flow_set(Ui_Handle handle, Ui_Flow flow, float gap = 0.0f, float padding = 0.0f);
		//取排列设置
		bool flow_get(Ui_Handle handle, Ui_Flow& flow_out, float& gap_out, float& padding_out) const;
		/*
		设置 / 读取内容内边距与外边距（第九百四十七轮）
		padding 使标签文本区四边内缩；margin 在流式排列中使子项前后各留该间距。
		负值按 0 处理；未知句柄设置返回假、读取返回 0。
		*/
		bool padding_set(Ui_Handle handle, float padding);
		float padding_get(Ui_Handle handle) const;
		bool margin_set(Ui_Handle handle, float margin);
		float margin_get(Ui_Handle handle) const;
		/*
		设置 / 读取流式主轴弹性权重（第九百四十八轮）
		stretch > 0 的子项按权重分配父容器主轴剩余空间；<= 0 不参与。
		未知句柄设置返回假、读取返回 0。
		*/
		bool stretch_set(Ui_Handle handle, float stretch);
		float stretch_get(Ui_Handle handle) const;
		/*
		设置 / 读取流式换行（第九百四十九轮）
		开启后主轴放不下下一个子项时另起一行（横向）/ 一列（纵向），
		行高（列宽）取该行（列）最高（宽）子项；换行模式不参与弹性权重分配。
		未知句柄设置返回假、读取返回假。
		*/
		bool flow_wrap_set(Ui_Handle handle, bool wrap);
		bool flow_wrap_get(Ui_Handle handle) const;
		/*
		设置 / 读取换行行内主轴分布（第九百五十一轮）
		Start / Center / End / Space_Between；弹性权重优先于分布（有弹性分配时不追加分布偏移）。
		未知句柄设置返回假、读取返回 Start。
		*/
		bool flow_justify_set(Ui_Handle handle, Ui_Justify justify);
		Ui_Justify flow_justify_get(Ui_Handle handle) const;
		//设置 / 读取排列交叉轴对齐（未知句柄设置返回假；读取返回 Start）
		bool flow_align_set(Ui_Handle handle, Ui_Align align);
		Ui_Align flow_align_get(Ui_Handle handle) const;
		/*
		设置 / 读取内容滚动偏移（第八百八十九轮）
		偏移单位为像素，负值按 0；作用于该节点的全部后代（矩形在帧内统一平移），
		配合 `clip_children_set(handle, true)` 即构成滚动容器。
		未知句柄设置返回假；读取返回 (0,0)。
		*/
		bool scroll_set(Ui_Handle handle, float x, float y);
		bool scroll_get(Ui_Handle handle, float& x_out, float& y_out) const;
		/*
		绑定滚动条（第八百九十轮）
		bar 通常为滑动条控件；拖动其取值按 0..1 映射到 target 的滚动偏移，
		最大偏移 = max(0, content_extent - target 视口尺寸)；vertical 决定按纵 / 横坐标取值。
		任一未知句柄返回假；解绑用 target = 0。
		*/
		bool scroll_bar_bind(Ui_Handle bar, Ui_Handle target, float content_extent, bool vertical = true);
		//布局重算次数（观测用；根尺寸 / 节点集合 / 布局设置变化时递增）
		uint64_t layout_apply_count(void) const;
		/*
		标记布局待重算（第九百五十三轮）
		下一次 frame_begin 会重算布局；布局属性（矩形 / 排列 / 内外边距 / 弹性 / 换行 / 分布）修改时自动置脏。
		*/
		void layout_dirty_mark(void);
		/*
		布局快照（第九百五十四轮）
		按树序（父先于子、同层按创建顺序）输出指定节点及其全部后代的布局条目。
		未知句柄返回假并清空输出。
		*/
		bool layout_snapshot(Ui_Handle handle, std::vector<Ui_Layout_Item>& items_out) const;
		//设置 / 读取主题色板（第八百八十三轮）
		void theme_set(const Ui_Theme& theme);
		const Ui_Theme& theme_get(void) const;
		//设置 / 读取控件样式角色（未知句柄设置返回假；读取返回 Custom）
		bool role_set(Ui_Handle handle, Ui_Role role);
		Ui_Role role_get(Ui_Handle handle) const;
		//设置 / 读取控件不透明度（0..1，越界夹紧；未知句柄设置返回假）
		bool opacity_set(Ui_Handle handle, float opacity);
		float opacity_get(Ui_Handle handle) const;
		/*
		设置 / 读取圆角半径（第八百八十七轮）
		半径单位像素，负值按 0；绘制时再按短边一半夹紧。
		未知句柄设置返回假，读取返回 0。
		*/
		bool corner_radius_set(Ui_Handle handle, float radius);
		float corner_radius_get(Ui_Handle handle) const;
		/*
		设置 / 读取描边
		width <= 0 表示无描边；未知句柄设置返回假，读取宽度返回 0、颜色返回黑色。
		*/
		bool border_set(Ui_Handle handle, float width, const Ui_Color& color);
		float border_width_get(Ui_Handle handle) const;
		Ui_Color border_color_get(Ui_Handle handle) const;
		//设置背景色
		bool color_set(Ui_Handle handle, const Ui_Color& color);
		//设置按钮标签
		bool label_set(Ui_Handle handle, const std::string& label);
		//设置文本框内容（光标移到末尾）
		bool text_set(Ui_Handle handle, const std::string& text);
		/*
		设置 / 读取多行文本布局（第八百九十四轮）
		wrap：超出矩形宽度时按字符自动换行；line_height <= 0 取字体高度；
		valign：多行整体垂直对齐（默认居中，保持单行观感）。
		未知句柄设置返回假，读取返回默认值。
		*/
		bool text_layout_set(Ui_Handle handle, bool wrap, float line_height, Ui_Align valign);
		void text_layout_get(Ui_Handle handle, bool& wrap_out, float& line_height_out, Ui_Align& valign_out) const;
		//设置 / 读取单行省略（未知句柄设置返回假，读取返回假）
		bool text_ellipsis_set(Ui_Handle handle, bool ellipsis);
		bool text_ellipsis_get(Ui_Handle handle) const;
		/*
		设置 / 读取多行文本水平对齐（第九百二十一轮）
		Start / Center / End 按矩形宽度对齐每行；Stretch 按 Start 处理。
		未知句柄设置返回假，读取返回 Start。
		*/
		bool text_halign_set(Ui_Handle handle, Ui_Align align);
		Ui_Align text_halign_get(Ui_Handle handle) const;
		/*
		设置 / 读取字间距（第九百三十八轮）
		逐码点推进时在字形宽度后追加该像素间距（末字符不追加；负值允许收紧）。
		度量与绘制共用同一推进口径，换行 / 省略 / 对齐随之生效。
		未知句柄设置返回假，读取返回 0。
		*/
		bool letter_spacing_set(Ui_Handle handle, float spacing);
		float letter_spacing_get(Ui_Handle handle) const;
		/*
		设置 / 读取首行缩进（第九百三十九轮）
		仅文本块首行起始右移该像素值，且首行可用宽度相应扣减（换行与省略按扣减后宽度判定）；
		对水平对齐同样生效（对齐后整体再右移）。负值允许外突。
		未知句柄设置返回假，读取返回 0。
		*/
		bool text_indent_set(Ui_Handle handle, float indent);
		float text_indent_get(Ui_Handle handle) const;
		/*
		设置 / 读取文本阴影（第九百四十轮）
		color.a <= 0 表示关闭；开启时先按偏移绘制阴影副本再绘制正文。
		未知句柄设置返回假，读取返回默认值（偏移 0、alpha 0）。
		*/
		bool text_shadow_set(Ui_Handle handle, const Ui_Text_Shadow& shadow);
		Ui_Text_Shadow text_shadow_get(Ui_Handle handle) const;
		/*
		行内着色区间（第九百四十一轮）
		begin / end 为节点文本字节区间，自动对齐到字符边界并夹到文本范围内；
		后添加的区间覆盖先添加者；begin >= end 或未知句柄返回假。
		font 为 0 时使用控件字体，非 0 时该区间用指定字体绘制且行高取该行最大字体。
		*/
		bool text_span_add(Ui_Handle handle, std::size_t begin, std::size_t end, const Ui_Color& color,
			uint64_t font = 0);
		//清空全部行内区间（未知句柄返回假）
		bool text_span_clear(Ui_Handle handle);
		//取行内区间数量（未知句柄返回 0）
		std::size_t text_span_count(Ui_Handle handle) const;
		/*
		设置 / 读取控件专用字体（第八百九十六轮）
		0 表示回退到 UI 全局字体；未知句柄设置返回假，读取返回 0。
		*/
		bool text_font_set(Ui_Handle handle, uint64_t font);
		uint64_t text_font_get(Ui_Handle handle) const;
		//取文本框内容
		bool text_get(Ui_Handle handle, std::string& text_out) const;
		//复选框选中状态读取 / 设置
		bool checked_get(Ui_Handle handle) const;
		bool checked_set(Ui_Handle handle, bool checked);
		//滑动条值读取 / 设置（0..1 钳制）
		bool value_get(Ui_Handle handle, float& value_out) const;
		bool value_set(Ui_Handle handle, float value);
		/*
		数值插值（第九百四十五轮）
		anim_speed 为每秒趋近速率（<= 0 关闭）；开启后每帧按帧时间把显示值向目标值趋近，
		读取接口 value_get 仍返回目标值。未知句柄设置返回假，读取返回 0。
		*/
		bool anim_speed_set(Ui_Handle handle, float speed);
		float anim_speed_get(Ui_Handle handle) const;
		//设置可见
		bool visible_set(Ui_Handle handle, bool visible);
		/*
		设置 / 读取是否裁剪直接子节点（第八百八十八轮）
		开启后子节点绘制被限制在本节点矩形与祖先裁剪的交集内；
		未知句柄设置返回假，读取返回假。
		*/
		bool clip_children_set(Ui_Handle handle, bool clip);
		bool clip_children_get(Ui_Handle handle) const;
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
		//按系统输入对象名查找首个存活控件（0 = 无；第九百二十九轮）
		Ui_Handle node_find_by_input_name(const std::string& name) const;
		//直接子节点数量（只计存活节点）
		std::size_t child_count(Ui_Handle handle) const;
		//按创建顺序取直接子节点（越界或父节点无效返回 0）
		Ui_Handle child_at(Ui_Handle handle, std::size_t index) const;

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
		//剪贴板读取回调（第九百三十五轮；空 = 不支持复制）
		std::string (*clipboard_get)(void* user) = nullptr;
		//剪贴板写入回调（空 = 不支持复制）
		void (*clipboard_set)(const std::string& text, void* user) = nullptr;
		//剪贴板回调用户数据
		void* clipboard_user = nullptr;
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
		//布局脏标记（根尺寸 / 节点集合 / 布局设置变化时置位）
		bool layout_dirty = true;
		//布局重算次数（观测）
		uint64_t layout_apply_count_value = 0;
		//上次布局时的节点集合大小
		std::size_t layout_node_count = 0;
		//上次布局时的根尺寸
		float layout_root_width = -1.0f;
		float layout_root_height = -1.0f;
		//主题色板
		Ui_Theme theme{};

		//按句柄查找控件 —— 非常量重载
		Ui_Node* node_find(Ui_Handle handle);
		//按句柄查找控件 —— 常量重载
		const Ui_Node* node_find(Ui_Handle handle) const;
		//释放槽位（标记销毁并清空辅助状态）
		void node_release(Ui_Handle handle);
		//命中测试（返回最上层可见且可用的控件句柄；无命中返回 0）
		Ui_Handle hit_test(float px, float py) const;
		//按创建顺序重算启用布局的节点矩形（父先于子）
		void layout_apply(void);
		//按创建顺序排列各容器的直接子节点（位于布局重算之后）
		void flow_apply(void);
		//按祖先滚动偏移平移各节点矩形（位于布局 / 排列之后，逐帧执行）
		void scroll_apply(void);
		//焦点设置（0 = 清除焦点）
		void focus_set(Ui_Handle handle);
		//控件是否可获得焦点（按钮 / 文本框 / 复选框 / 滑动条）
		static bool node_focusable(const Ui_Node& node);
		//Tab 焦点导航（forward 为真向前，否则向后）
		void focus_navigate(bool forward);
		//滑动条按指针横坐标更新取值（返回是否发生变化）
		bool slider_value_apply(Ui_Node& node, float pointer_x, float pointer_y);
		//单选按钮选中（同组其余项取消；返回是否发生变化）
		bool radio_select(Ui_Node& node);
		//滚动条取值同步到目标容器（返回是否有偏移变化）
		bool scroll_bar_sync(void);
		//文本框文本编辑（消费本帧文本与编辑键）
		void text_edit(Ui_Node& node, const Input_Frame& input);
	};
}
