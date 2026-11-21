#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//事件终端与事件类型（域内中心路由）
#include "Engine/EngineFoundation/src/event/Event_Terminal/事件终端.h"
#include "Engine/EngineFoundation/src/event/Event/事件.h"
//域内协议词汇
#include "Engine/EngineRenderer/src/render/UI/common/UI事件协议.h"
//JSON 载荷
#include <nlohmann/json.hpp>

//游戏引擎命名空间
namespace engine
{
	/*
	UI 域内总线（2026-11-21 · UI 局域网改造 批次 1）

	以一台 Event_Terminal 为局部路由：
	  - 内部节点（控件树 / 交互 / UI 渲染器 / 事件桥）通过 attach_node 把发送端口
	    绑定到本总线的接收函数，形成「终端 → 终端」的级联；
	  - 总线只保存待派发事件，帧末统一派发，避免控件树遍历中重入；
	  - 本总线不 attach 全局中转站，因此 UI/* 事件不会进入事件总线。
	*/
	class Ui_Local_Bus
	{
	public:
		//订阅回调：标签 + 载荷
		using Handler = std::function<void(const std::string& tag, const nlohmann::json& config)>;

		//构造（生成中心终端密钥并绑定发送端口）
		Ui_Local_Bus(void);
		//析构
		~Ui_Local_Bus(void) = default;
		//不可拷贝、不可移动（持有中心终端与订阅表）
		Ui_Local_Bus(const Ui_Local_Bus&) = delete;
		Ui_Local_Bus& operator=(const Ui_Local_Bus&) = delete;

		//中心终端（供内部节点查询绑定状态）
		Event_Terminal& terminal(void);
		//接入一个内部节点：把它的单事件发送端口重定位到本总线
		void attach_node(Event_Terminal& node);

		//订阅域内事件（同名覆盖）
		void subscribe(const std::string& name, Handler handler);
		//取消订阅
		void unsubscribe(const std::string& name);

		//发布域内事件（入队，帧末统一派发）
		bool publish(const std::string& tag, const nlohmann::json& config);
		//外部注入（网关翻译后的入站事件，与 publish 同队列）
		bool inject(const std::string& tag, const nlohmann::json& config);

		/*
		派发本帧队列
		outbound 非空时先交给它（网关翻译外发），再派发给持久订阅者；
		派发期间新发布的事件留在下一轮，避免重入。
		*/
		void frame_flush(const Handler& outbound = Handler{});

		//待派发事件数（测试与诊断）
		std::size_t pending_count(void) const;
		//订阅者数（测试与诊断）
		std::size_t subscriber_count(void) const;

	private:
		//待派发事件
		struct Entry
		{
			std::string tag{};
			nlohmann::json config{};
		};

		//中心终端发送端口回调：入队
		void accept(std::shared_ptr<Event> evt);

	private:
		//中心终端
		Event_Terminal center{};
		//中心终端权限密钥
		int64_t acl_key = 0;
		//订阅者（名称 → 回调）
		std::vector<std::pair<std::string, Handler>> subscribers{};
		//待派发队列
		std::vector<Entry> pending{};
		//派发中标记（防重入）
		bool flushing = false;
	};
}
