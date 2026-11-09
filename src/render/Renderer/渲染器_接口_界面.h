#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：界面
	*/
	class Renderer_Ui : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		UI 通道（供上层接入 ImGui Vulkan 后端）
		返回渲染层持有的 UI 通道（完整定义见 渲染器_UI通道.h，含 Vulkan / SDL 类型，
		需上层自行包含该扩展头）；未初始化时返回空。
		*/
		Ui_Channel_Context* ui_channel(void);

		/*
		原生 UI 上下文（保留模式控件树）
		返回渲染层持有的 UI 上下文（完整定义见 用户界面.h，SDL / Vulkan-free）；未初始化时返回空。
		控件在本帧 frame_render 之前创建 / 修改即参与本帧交互与绘制。
		*/
		Ui_Context* ui_context(void);

		/*
		帧输入聚合器（渲染层自持原始输入）
		返回渲染层持有的输入聚合器（完整定义见扩展头 输入聚合.h，含 SDL3 类型）；
		未初始化时返回空。外部可直接 event_feed 喂入平台事件（多窗口 / 测试注入），
		正常帧循环由渲染层在事件泵内自动喂入。
		*/
		Input_Aggregator* input_aggregator(void);

		/*
		系统输入 / UI 事件终端（供应用层绑定中转站）
		应用层需在终端上绑定 Broker_Attacher 与 Evt_Sender 两个端口（与其它模块同口径），
		随后调用 input_system_attach；渲染层会自行绑定接收入口并订阅
		FocusChanged / TextMode / Command / Text，同时每帧发布
		Raw / MouseFocus / ModalPush / ModalPop / TextSession。
		*/
		Event_Terminal* input_event_terminal(void);
		//接入系统输入（生成密钥、登记订阅者并订阅事件；端口未绑定时返回假）
		bool input_system_attach(void);
		//退出系统输入（向中转站发送订阅者退出事件；幂等）
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
	};
}
