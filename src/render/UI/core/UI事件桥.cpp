#include "../局部命名空间使用.h"
//UI 事件桥声明（含系统层输入事件词汇）
#include "Engine/EngineRenderer/src/render/UI/UI事件桥.h"
//UI 域内总线与协议（网关翻译）
#include "UI本地总线.h"
//系统输入桥（Raw 载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅登记 / 订阅者退出）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//日志
#include "Engine/EngineFoundation/src/log/日志系统运行包.h"

namespace engine
{
	//构造（绑定本系统的接收入口）
	Ui_Event_Bridge::Ui_Event_Bridge(void)
	{
		//事件接收入口回到本桥处理
		event_terminal->bind(Port_ID::Evt_Receiver,
			[this](std::shared_ptr<Event> evt) -> void { this->event_process(std::move(evt)); });
	}

	//接入事件系统
	bool Ui_Event_Bridge::attach(const std::string& name)
	{
		//已接入则幂等成功
		if (attached_flag)
			return true;

		//记录订阅者名
		subscriber = name;

		//首次接入生成权限密钥（后续重接入沿用）
		if (acl_key == 0)
		{
			auto key = event_terminal.key();
			//密钥生成失败
			if (!key.has_value())
			{
				logger.error("UI 事件桥:: 接入失败：终端密钥生成失败");
				return false;
			}
			acl_key = key.value();
		}

		//登记中转站订阅者（应用层须已绑定 Broker_Attacher 端口）
		if (!event_terminal.attach(subscriber, acl_key))
		{
			logger.warn("UI 事件桥:: 接入失败：中转站接入端口未绑定或登记失败");
			return false;
		}

		//订阅清单：焦点变更 / 文本模式 / 语义命令 / 文本语义
		nlohmann::json items = nlohmann::json::array();
		for (const char* tag : { input_event::focus_changed, input_event::text_mode,
			input_event::command, input_event::text })
		{
			nlohmann::json item = nlohmann::json::object();
			item[event_protocol::field_category] = input_event::category;
			item[event_protocol::field_tag] = tag;
			items.push_back(item);
		}

		//构造订阅登记事件
		auto evt = event_terminal.build("", "", event_protocol::subscribed_events, event_protocol::add);
		if (!evt)
		{
			logger.error("UI 事件桥:: 订阅事件构造失败");
			return false;
		}
		evt->config[event_protocol::field_object] = subscriber;
		evt->config[event_protocol::field_acl_key] = static_cast<uint64_t>(acl_key);
		evt->config[event_protocol::field_subscribed_events] = std::move(items);
		//发送订阅登记（应用层须已绑定 Evt_Sender 端口）
		if (!event_terminal.send(evt, acl_key))
		{
			logger.error("UI 事件桥:: 订阅事件发送失败（Evt_Sender 端口未绑定？）");
			return false;
		}

		//标记接入
		attached_flag = true;
		logger.info("UI 事件桥:: 已接入（订阅者 {}）", subscriber);
		return true;
	}

	//退出事件系统
	void Ui_Event_Bridge::detach(void)
	{
		//未接入则忽略
		if (!attached_flag)
			return;

		//订阅者退出事件（sender 与对象名一致）
		auto evt = event_terminal.build(subscriber, "Event_Broker",
			event_protocol::subscriber, event_protocol::detach);
		if (evt)
		{
			evt->config[event_protocol::field_object] = subscriber;
			evt->config[event_protocol::field_acl_key] = static_cast<uint64_t>(acl_key);
			event_terminal.send(evt, acl_key);
		}

		//清接入与缓存状态（密钥保留供重接入）
		attached_flag = false;
		focus_main.clear();
		focus_child.clear();
		focus_modal.clear();
		text_mode = false;
		commands.clear();
		texts.clear();
		last_modal.clear();
		last_preedit.clear();
		last_focus_name.clear();
		pending_focus.clear();
		pending_active = false;
		pending_age = 0;
		logger.info("UI 事件桥:: 已退出（订阅者 {}）", subscriber);
	}

	//是否已接入
	bool Ui_Event_Bridge::attached(void) const
	{
		return attached_flag;
	}

	//订阅者名
	const std::string& Ui_Event_Bridge::subscriber_name_get(void) const
	{
		return subscriber;
	}

	//事件处理入口
	void Ui_Event_Bridge::event_process(std::shared_ptr<Event> evt)
	{
		//空事件 / 非输入分类忽略
		if (!evt || evt->category != input_event::category)
			return;

		//焦点变更：缓存 main / child / modal / text
		if (evt->tag == input_event::focus_changed)
		{
			focus_main = evt->config.value("main", std::string{});
			focus_child = evt->config.value("child", std::string{});
			focus_modal = evt->config.value("modal", std::string{});
			text_mode = evt->config.value("text", text_mode);
			return;
		}
		//文本模式变更：缓存 active
		if (evt->tag == input_event::text_mode)
		{
			text_mode = evt->config.value("active", false);
			return;
		}
		//语义命令：入队（由 frame_apply 分发或供应用层取用）
		if (evt->tag == input_event::command)
		{
			const std::string command = evt->config.value("command", std::string{});
			if (!command.empty())
				commands.push_back(command);
			return;
		}
		//文本语义：取 text 字段入队
		if (evt->tag == input_event::text)
		{
			const std::string text = evt->config.value("text", std::string{});
			if (!text.empty())
				texts.push_back(text);
			return;
		}
	}

	//发送一条事件
	bool Ui_Event_Bridge::event_send(const std::string& tag, const nlohmann::json& config,
		const std::string& receiver)
	{
		//未接入不发送
		if (!attached_flag)
			return false;
		//构造事件
		auto evt = event_terminal.build(subscriber, receiver, input_event::category, tag);
		if (!evt)
		{
			logger.error("UI 事件桥:: 事件构造失败：{}", tag);
			return false;
		}
		evt->config = config;
		//发送
		if (!event_terminal.send(evt, acl_key))
		{
			logger.warn("UI 事件桥:: 事件发送失败：{}", tag);
			return false;
		}
		return true;
	}

	//绑定域内总线（幂等）
	void Ui_Event_Bridge::bus_bind(Ui_Context& ui)
	{
		//同一上下文只注册一次
		if (bound_ui == &ui)
			return;
		//旧上下文解绑
		if (bound_ui != nullptr)
			bound_ui->bus_get().unsubscribe("ui_gateway");
		bound_ui = &ui;
		//注册网关翻译器（持久订阅域内事件）
		ui.bus_get().subscribe("ui_gateway",
			[this](const std::string& tag, const nlohmann::json& config)
			{
				this->translate_out(tag, config);
			});
	}

	//域内事件 → 外部协议翻译
	void Ui_Event_Bridge::translate_out(const std::string& tag, const nlohmann::json& config)
	{
		//未接入事件系统不翻译
		if (!attached_flag)
			return;
		//网关自身的产物不回译（防环）
		const std::string origin = config.value(ui_event::field_origin, std::string{});
		if (origin == ui_event::origin_gateway)
			return;
		//只有点击 / 切换 / 取值需要转成对外命令
		if (tag != ui_event::click && tag != ui_event::toggle && tag != ui_event::value)
			return;
		//取控件句柄
		const Ui_Handle handle = config.value(ui_event::field_handle, static_cast<Ui_Handle>(0));
		if (handle == 0 || bound_ui == nullptr)
			return;
		//未绑定命令的控件不外发（应用层可继续用本地回调）
		const std::string command = bound_ui->command_of(handle);
		if (command.empty())
			return;
		//构造命令载荷并广播（receiver 为空 = 广播）
		nlohmann::json payload = nlohmann::json::object();
		payload["command"] = command;
		event_send(input_event::command, payload, "");
	}

	//帧前准备
	void Ui_Event_Bridge::text_route_prepare(Ui_Context& ui)
	{
		//绑定域内总线（幂等）
		bus_bind(ui);
		/*
		键盘总闸：系统焦点只有落在 UI 主对象上时控件才消费键盘；
		未配置 UI 主对象名时保持开启，供未接入系统的独立使用。
		*/
		const std::string& root = ui.input_root_name_get();
		ui.keyboard_gate_set(root.empty() || focus_main == root);
		//仅当系统文本模式开启且焦点文本框已绑定系统输入名时走系统文本会话
		ui.text_route_set(text_mode && !ui.text_focus_input_name().empty());
	}

	//发布本帧事件
	void Ui_Event_Bridge::frame_publish(const Input_Frame& frame, Ui_Context& ui)
	{
		//未接入不发布
		if (!attached_flag)
			return;

		//原始输入帧
		nlohmann::json raw;
		if (input_bridge::raw_build(frame, ui.text_focus_active(), raw))
			event_send(input_event::raw, raw, "Focus_System");

		//鼠标焦点：按下沿决定 UI 目标（控件 / UI 根 / UI 外部）
		if (ui.press_any_get())
		{
			std::string target{};
			if (!ui.pressed_input_name_get().empty())
				target = ui.pressed_input_name_get();
			else if (!ui.press_outside_get())
				target = ui.input_root_name_get();
			else
				target = ui.input_outside_name_get();
			//有目标才投递
			if (!target.empty())
			{
				nlohmann::json config = nlohmann::json::object();
				config[input_event::field_object] = target;
				event_send(input_event::mouse_focus, config, "Focus_System");
			}
		}

		/*
		控件本地焦点变化 → 上报系统子焦点
		（Tab / 键盘导航只动本地焦点，不上报就会与系统子焦点分裂形成双焦点环）；
		鼠标按下帧由 MouseFocus 已覆盖焦点意图，此处只同步记录以免下一帧重复上报。
		*/
		const std::string& root = ui.input_root_name_get();
		const std::string& local_focus = ui.focused_input_name_get();
		if (ui.press_any_get())
			last_focus_name = local_focus;
		else if (!root.empty() && local_focus != last_focus_name
			&& (!local_focus.empty() || focus_main == root))
		{
			nlohmann::json config = nlohmann::json::object();
			config["host"] = root;
			config["child"] = local_focus;
			//发送成功才登记待确认（系统在下一帧开头应用）
			if (event_send(input_event::child_focus, config, "Focus_System"))
			{
				last_focus_name = local_focus;
				pending_focus = local_focus;
				pending_active = true;
				pending_age = 0;
			}
		}

		//模态切换：先弹旧、再压新（sender 固定为本订阅者，满足归属校验）
		const std::string& desired_modal = ui.modal_input_name_get();
		if (desired_modal != last_modal)
		{
			if (!last_modal.empty())
				event_send(input_event::modal_pop, nlohmann::json::object(), "Focus_System");
			if (!desired_modal.empty())
			{
				nlohmann::json config = nlohmann::json::object();
				config[input_event::field_object] = desired_modal;
				event_send(input_event::modal_push, config, "Focus_System");
			}
			last_modal = desired_modal;
		}

		//文本会话：系统文本模式下把 UI 焦点文本框的提交 / 预编辑送入系统
		if (text_mode && ui.text_route_get() && !ui.text_focus_input_name().empty())
		{
			const std::string& committed = frame.text.committed;
			const std::string& preedit = frame.text.preedit;
			if (!committed.empty() || preedit != last_preedit)
			{
				nlohmann::json config = nlohmann::json::object();
				config["text"] = committed;
				config["preedit"] = preedit;
				config["commit"] = !committed.empty();
				event_send(input_event::text_session, config, "Focus_System");
				last_preedit = preedit;
			}
		}
	}

	//应用已收到的系统事件
	void Ui_Event_Bridge::frame_apply(Ui_Context& ui)
	{
		/*
		系统焦点离开 UI 主对象 → 清除本地焦点（键盘总闸已随之关闭）；
		仍在 UI 内时按系统子焦点回写本地焦点，使系统焦点环与本地焦点环重合。
		*/
		const std::string& root = ui.input_root_name_get();
		if (!root.empty() && !focus_main.empty() && focus_main != root)
			ui.focus_clear();
		else
		{
			//已上报的焦点被系统确认
			if (pending_active && focus_child == pending_focus)
				pending_active = false;
			//系统连续两帧未确认视为拒绝，恢复跟随
			else if (pending_active && ++pending_age >= 2)
				pending_active = false;
			//无待确认请求时才跟随（避免系统一帧延迟把本地导航结果顶回）
			if (!pending_active)
				ui.system_focus_sync(focus_child);
		}
		//同步已上报值，抑制系统回写引发的回声上报
		last_focus_name = ui.focused_input_name_get();

		//焦点状态映射到控件（系统焦点环）
		ui.system_focus_apply(focus_main, focus_child, focus_modal, text_mode);

		//命令：已绑定控件的直接激活并从队列移除；未绑定的留给应用层
		for (auto it = commands.begin(); it != commands.end();)
		{
			if (ui.command_dispatch(*it))
				it = commands.erase(it);
			else
				++it;
		}

		//文本：焦点文本框消费；未消费的留给应用层
		for (auto it = texts.begin(); it != texts.end();)
		{
			if (ui.system_text_apply(*it))
				it = texts.erase(it);
			else
				++it;
		}

		//派发命令激活产生的带来源标记域内事件（网关不回译）
		ui.bus_get().frame_flush();
	}

	//系统文本模式
	bool Ui_Event_Bridge::system_text_mode(void) const
	{
		return text_mode;
	}

	//系统焦点状态
	void Ui_Event_Bridge::focus_state_get(std::string& main, std::string& child,
		std::string& modal, bool& text) const
	{
		main = focus_main;
		child = focus_child;
		modal = focus_modal;
		text = text_mode;
	}

	//取一条未消费的语义命令
	bool Ui_Event_Bridge::command_pop(std::string& command)
	{
		//空队返回假
		if (commands.empty())
			return false;
		command = commands.front();
		commands.erase(commands.begin());
		return true;
	}

	//取一条未消费的文本语义
	bool Ui_Event_Bridge::text_pop(std::string& text)
	{
		//空队返回假
		if (texts.empty())
			return false;
		text = texts.front();
		texts.erase(texts.begin());
		return true;
	}
}
