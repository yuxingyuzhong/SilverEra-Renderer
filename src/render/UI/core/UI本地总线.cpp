#include "../局部命名空间使用.h"
//UI 域内总线声明
#include "UI本地总线.h"
//日志
#include "Engine/EngineFoundation/src/log/日志系统运行包.h"

namespace engine
{
	//构造（生成中心终端密钥并绑定发送端口）
	Ui_Local_Bus::Ui_Local_Bus(void)
	{
		//生成中心终端权限密钥
		auto key = center.key();
		if (key.has_value())
			acl_key = key.value();
		//中心终端的发送端口重定位到本总线（终端 → 终端级联）
		center->bind(Port_ID::Evt_Sender,
			[this](std::shared_ptr<Event> evt) -> void { this->accept(std::move(evt)); });
	}

	//中心终端
	Event_Terminal& Ui_Local_Bus::terminal(void)
	{
		return center;
	}

	//接入一个内部节点
	void Ui_Local_Bus::attach_node(Event_Terminal& node)
	{
		//把节点的发送端口重定位到本总线（节点密钥由调用方生成）
		node->bind(Port_ID::Evt_Sender,
			[this](std::shared_ptr<Event> evt) -> void { this->accept(std::move(evt)); });
	}

	//订阅域内事件
	void Ui_Local_Bus::subscribe(const std::string& name, Handler handler)
	{
		//空名或空回调拒绝
		if (name.empty() || !handler)
			return;
		//同名覆盖
		for (auto& item : subscribers)
		{
			if (item.first == name)
			{
				item.second = std::move(handler);
				return;
			}
		}
		//追加新订阅
		subscribers.emplace_back(name, std::move(handler));
	}

	//取消订阅
	void Ui_Local_Bus::unsubscribe(const std::string& name)
	{
		//移除同名订阅
		for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
		{
			if (it->first == name)
			{
				subscribers.erase(it);
				return;
			}
		}
	}

	//发布域内事件
	bool Ui_Local_Bus::publish(const std::string& tag, const nlohmann::json& config)
	{
		//空标签拒绝
		if (tag.empty())
			return false;
		//构造域内事件
		auto evt = center.build("ui_local", "", ui_event::category, tag);
		if (!evt)
			return false;
		evt->config = config;
		//经中心终端发送（发送端口已重定位到 accept）
		if (!center.send(evt, acl_key))
		{
			logger.warn("UI 本地总线:: 事件发送失败：{}", tag);
			return false;
		}
		return true;
	}

	//外部注入
	bool Ui_Local_Bus::inject(const std::string& tag, const nlohmann::json& config)
	{
		return publish(tag, config);
	}

	//派发本帧队列
	void Ui_Local_Bus::frame_flush(const Handler& outbound)
	{
		//重入保护：派发期间再派发直接返回
		if (flushing)
			return;
		flushing = true;
		//交换本帧队列（派发期间新发布的事件留到下一轮）
		std::vector<Entry> batch{};
		batch.swap(pending);
		//逐条派发
		for (const Entry& entry : batch)
		{
			//网关翻译外发
			if (outbound)
				outbound(entry.tag, entry.config);
			//持久订阅者
			for (const auto& item : subscribers)
			{
				if (item.second)
					item.second(entry.tag, entry.config);
			}
		}
		flushing = false;
	}

	//待派发事件数
	std::size_t Ui_Local_Bus::pending_count(void) const
	{
		return pending.size();
	}

	//订阅者数
	std::size_t Ui_Local_Bus::subscriber_count(void) const
	{
		return subscribers.size();
	}

	//中心终端发送端口回调：入队
	void Ui_Local_Bus::accept(std::shared_ptr<Event> evt)
	{
		//空事件忽略
		if (!evt)
			return;
		//只接收域内分类，防止外部事件误入
		if (evt->category != ui_event::category)
			return;
		pending.push_back(Entry{ evt->tag, evt->config });
	}
}
