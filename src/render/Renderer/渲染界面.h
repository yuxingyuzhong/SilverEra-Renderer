#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	struct Render_Core_Context;
	class Renderer;
	struct Ui_State;
	struct Ui_Render_Stats;

	/*
	渲染器域：界面
	UI 通道 / 原生 UI / 输入聚合 / 事件桥 / 字体 / 文字绘制。
	*/
	class Render_Ui
	{
	public:
		Render_Ui(Renderer* owner, const Render_Core_Context& core_context);
		~Render_Ui(void);
		//不可拷贝、不可移动
		Render_Ui(const Render_Ui&) = delete;
		Render_Ui& operator=(const Render_Ui&) = delete;

		//=== 原界面域接口方法（签名原样照抄）===
		/*
		UI 通道（供上层接入 ImGui Vulkan 后端）
		返回渲染层持有的 UI 通道（完整定义见 渲染器_UI通道.h）；未初始化时返回空。
		*/
		Ui_Channel_Context* ui_channel(void);
		//原生 UI 上下文（保留模式控件树；未初始化时返回空）
		Ui_Context* ui_context(void);
		//最近一帧 UI 绘制统计（第八百九十九轮；未初始化时返回全零）
		const Ui_Render_Stats& ui_render_stats(void) const;
		//UI 文本几何 / 文本块布局缓存容量设置（钳到 [1,1024]，清空既有缓存；第九百一十二轮）
		void ui_text_cache_limits_set(uint32_t text_capacity, uint32_t block_capacity);
		//帧输入聚合器（渲染层自持原始输入；未初始化时返回空）
		Input_Aggregator* input_aggregator(void);
		//系统输入 / UI 事件终端（供应用层绑定中转站）
		Event_Terminal* input_event_terminal(void);
		//接入系统输入（端口未绑定时返回假）
		bool input_system_attach(void);
		//退出系统输入（幂等）
		void input_system_detach(void);
		//是否已接入系统输入
		bool input_system_attached(void) const;
		//取一条未被 UI 消费的系统语义命令（队首；成功写出并移除）
		bool ui_command_pop(std::string& command);
		//取一条未被 UI 消费的系统文本语义（队首；成功写出并移除）
		bool ui_text_pop(std::string& text);
		//取系统焦点状态（main / child / modal / 文本模式；未接入时返回假）
		bool ui_focus_state_get(std::string& main, std::string& child,
			std::string& modal, bool& text) const;

		//=== 原文字接口方法（签名原样照抄）===
		//从 TTF 文件加载字体（字号 = 字形像素高度）
		bool font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height);
		//卸载字体（句柄无效则忽略；同时清除该字体的文本网格缓存）
		void font_unload(uint64_t handle);
		//设置文字颜色（RGBA，分量 0..1 逐分量钳制；自下一次文字绘制起生效）
		void text_color_set(const float color[4]);
		//设置文字描边（uv_width 为图集 UV 半宽，0 = 关闭；颜色分量 0..1 逐分量钳制）
		void text_outline_set(float uv_width, const float color[3]);
		//绘制文本（像素坐标，原点在窗口左上、Y 向下）
		bool text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y);
		//度量单行文本（像素；宽度不含尾随空白，高度为字体像素高度）
		bool text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const;
		//按布局绘制文本（换行 / 对齐；其余语义同 text_draw）
		bool text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
			const Text_Layout& layout);
		//取该字体累计被跳过的码点数（诊断；句柄无效返回 0）
		uint64_t font_skipped_codepoints(uint64_t font) const;
		//文字网格缓存命中次数（自 init 起累计）
		uint64_t text_cache_hits(void) const;
		//文字网格缓存未命中次数（自 init 起累计）
		uint64_t text_cache_misses(void) const;
		//文本网格缓存条目上限设置（钳到 [1, 1024]）
		void text_cache_capacity_set(uint32_t capacity);
		//文本网格缓存条目上限（未初始化返回 0）
		uint32_t text_cache_capacity(void) const;
		//当前文本网格缓存条目数（未初始化返回 0）
		uint32_t text_cache_size(void) const;

		//=== 域生命周期与层内成员窄接口 ===
		//初始化（原 renderer_init_ui + 文字宿主装配）
		void init(const Renderer_Config& effective);
		//关闭（原 shutdown 中界面相关部分）
		void shutdown(void);
		//确保 UI 管线就绪
		bool ui_pipeline_ensure(void);
		//UI 阶段绘制入口（command_buffer 用 void* 以免公共头引入 Vulkan）
		static void ui_draw_entry(void* command_buffer, void* user_data);
		//当前是否处于文字绘制过程
		bool text_drawing(void) const;
		//文字推送块描边参数（半宽与 RGB）
		void text_outline_get(float out[4]) const;
		//推进一帧输入、原生 UI 交互与系统事件路由
		void input_frame_advance(uint32_t width, uint32_t height, float display_scale);
		//推进文字帧时钟并回收无在途引用的文字网格
		void text_frame_advance(uint32_t frame_overlap);
		//平台窗口与字体资源装配
		void input_window_bind(void* window);
		bool font_resource_build(void);
		uint64_t font_resource_count(void) const;
	private:
		//门面自身（跨域语义调用）
		Renderer* owner = nullptr;
		const Render_Core_Context* core = nullptr;
		//界面域状态
		std::unique_ptr<Ui_State> state_ptr;
	};
}
