#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//事件终端与事件类型
#include "Engine/EngineFoundation/src/event/Event_Terminal/事件终端.h"
#include "Engine/EngineFoundation/src/event/Event/事件.h"
//输入事件词汇（焦点 / 命令 / 文本 / 文本模式）
#include "Engine/EngineSystem/src/input/common/输入事件.h"
//UI 控件树与帧输入快照
#include "Engine/EngineRenderer/src/render/UI/用户界面.h"
#include "Engine/EngineRenderer/src/render/Input/输入快照.h"
//JSON 载荷
#include <nlohmann/json.hpp>

//游戏引擎命名空间
namespace engine
{
	/*
	UI 事件桥
	渲染层/UI 与 EngineSystem 输入体系之间的唯一事件接触面：

	出站（每帧）：
	    Input/Raw            → 焦点系统（原始输入）
	    Input/MouseFocus     → 焦点系统（UI 控件命中 / 点击 UI 外部）
	    Input/ModalPush/Pop  → 焦点系统（UI 模态根切换，sender 固定为订阅者名）
	    Input/TextSession    → 焦点系统（系统文本模式下 UI 文本框的提交 / 预编辑）

	入站（订阅）：
	    Input/FocusChanged   → 缓存 main / child / modal / text，并映射到控件焦点视觉
	    Input/TextMode       → 缓存在文本模式（控制文本路由与平台文本输入）
	    Input/Command        → 绑定到控件的命令直接激活控件，其余入队供应用层取用
	    Input/Text           → 焦点文本框消费；未消费的入队

	本头属本层实现细节（含系统层头与 JSON），不进公共主头。
	*/
	class Ui_Event_Bridge
	{
	public:
		//事件终端（供应用层绑定 Broker_Attacher / Evt_Sender）
		Event_Terminal event_terminal;

		//构造（绑定本系统的接收入口）
		Ui_Event_Bridge(void);
		//不可拷贝、不可移动（持有事件终端与回调捕获）
		Ui_Event_Bridge(const Ui_Event_Bridge&) = delete;
		Ui_Event_Bridge& operator=(const Ui_Event_Bridge&) = delete;

		//接入事件系统（登记订阅者并订阅焦点 / 文本 / 命令 / 文本语义；端口未绑定时失败）
		bool attach(const std::string& subscriber = "Renderer_UI");
		//退出事件系统（发送订阅者退出事件；幂等）
		void detach(void);
		//是否已接入
		bool attached(void) const;
		//订阅者名
		const std::string& subscriber_name_get(void) const;

		/*
		帧前准备（在 Ui_Context::frame_begin 之前调用）
		以「上一帧焦点状态」决定：文本走本地拼接还是系统回传落文本；
		以及控件键盘交互总闸（系统焦点不在 UI 主对象时关闭）。
		*/
		void text_route_prepare(Ui_Context& ui);
		//发布本帧事件（在 Ui_Context::frame_begin 之后调用）
		void frame_publish(const Input_Frame& frame, Ui_Context& ui);
		//应用已收到的系统事件（在 frame_publish 之后调用，使同帧事件即时生效）
		void frame_apply(Ui_Context& ui);

		//系统文本模式（最近一次 TextMode 广播）
		bool system_text_mode(void) const;
		//系统焦点状态（main / child / modal / text）
		void focus_state_get(std::string& main, std::string& child, std::string& modal, bool& text) const;
		//取一条未消费的语义命令（队首）
		bool command_pop(std::string& command);
		//取一条未消费的文本语义（队首）
		bool text_pop(std::string& text);
	private:
		//绑定域内总线（幂等：同一上下文只注册一次翻译器）
		void bus_bind(Ui_Context& ui);
		//域内事件 → 外部协议翻译（命令绑定控件才外发；网关产物不回译）
		void translate_out(const std::string& tag, const nlohmann::json& config);
		//事件处理入口
		void event_process(std::shared_ptr<Event> evt);
		//发送一条事件（失败仅告警）
		bool event_send(const std::string& tag, const nlohmann::json& config, const std::string& receiver);

	private:
		//已绑定域内总线的 UI 上下文
		Ui_Context* bound_ui = nullptr;
		//订阅者名
		std::string subscriber{};
		//权限密钥
		int64_t acl_key = 0;
		//接入标记
		bool attached_flag = false;

		//焦点缓存
		std::string focus_main{};
		std::string focus_child{};
		std::string focus_modal{};
		//文本模式缓存
		bool text_mode = false;
		//待消费语义命令
		std::vector<std::string> commands{};
		//待消费文本语义
		std::vector<std::string> texts{};

		//上一次已发布的模态根（用于识别 Push / Pop）
		std::string last_modal{};
		//上一次已发布的预编辑串（避免逐帧重复发送）
		std::string last_preedit{};
		//上一次已上报系统的控件焦点名（避免逐帧重复发送）
		std::string last_focus_name{};
		//已上报但系统尚未确认的焦点名（未确认期间不跟随系统回写，避免一帧延迟顶回本地导航）
		std::string pending_focus{};
		//是否存在待确认焦点上报
		bool pending_active = false;
		//待确认焦点的等待帧数（超限视为系统已拒绝）
		uint32_t pending_age = 0;
	};
}
