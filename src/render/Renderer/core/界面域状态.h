#pragma once
//依赖：core/渲染器实现.h 已提供模块类型

//游戏引擎命名空间
namespace engine
{
	//界面域状态（从 Impl 迁出的成员，名字保持不变以减少改写）
	struct Ui_State
	{
		//UI 通道（供上层接入 ImGui Vulkan 后端；不代管 UI）
		Ui_Channel_Context ui_channel;

		Ui_Context ui;

		Ui_Renderer ui_render;

		//UI 纯色管线句柄（0 = 未登记）
		uint64_t ui_solid_pipeline = 0;

		//UI 文字管线句柄（0 = 未登记）
		uint64_t ui_text_pipeline = 0;

		//UI 管线是否已尝试登记
		bool ui_pipeline_ready = false;

		/*
		系统输入 / UI 事件桥
		ui_event 持有事件终端并负责 Raw、鼠标焦点、模态、文本会话的出站与
		焦点变更 / 文本模式 / 语义命令 / 文本语义的入站。
		*/
		Ui_Event_Bridge ui_event;

		//是否正处于文字绘制回调内（第五十二轮；文字网格不参与阴影投影）
		bool in_text_draw = false;

		//字体模块（TTF → 字形图集）
		Font_Context font;

		/*
		文字绘制子系统（第四十七轮下沉）
		持有文字管线句柄、文字颜色与「(字体, 文本, 布局) → 网格」缓存；
		字形补烘 / 管线登记 / 纹理绑定 / 网格创建 / 绘制登记经宿主回调转交本实现体。
		*/
		Text_Renderer text;

		/*
		原生 UI 帧输入聚合器
		input 为渲染层自持的帧输入聚合器；ui 为控件树与交互；ui_render 负责 UI 阶段的 GPU 绘制。
		*/
		Input_Aggregator input;

		/*
		文字描边参数（第一百四十轮；经推送常量 offset 80 下发）
		text_outline_uv_width = 描边半宽（图集 UV；0 = 关闭），text_outline_color = 描边色（0..1）。
		*/
		float text_outline_uv_width = 0.0f;

		float text_outline_color[3] = { 0.0f, 0.0f, 0.0f };
	};
}
