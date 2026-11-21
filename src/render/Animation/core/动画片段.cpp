//获取动画片段数据结构
#include "../动画片段.h"
//std::isfinite（关键帧时间与值的有限性校验）
#include <cmath>
//路径转换与可用性校验（.anim 文件加载）
#include "Engine/EngineFoundation/src/detail/methods/路径字符串转换.h"
#include "Engine/EngineFoundation/src/detail/methods/文件路径可用性校验.h"

namespace engine
{
	namespace
	{
		//片段文件体量上限（16 MB）
		constexpr uintmax_t ANIMATION_CLIP_FILE_LIMIT = 16ull * 1024ull * 1024ull;

		//通道字符串解析（仅接受固定小写写法）
		bool channel_parse(const std::string& text, Animation_Channel& receiver)
		{
			if (text == "translation")
				receiver = Animation_Channel::Translation;
			else if (text == "rotation")
				receiver = Animation_Channel::Rotation;
			else if (text == "scale")
				receiver = Animation_Channel::Scale;
			else
				return false;
			return true;
		}

		//插值字符串解析（仅接受固定小写写法）
		bool interpolation_parse(const std::string& text, Animation_Interpolation& receiver)
		{
			if (text == "linear")
				receiver = Animation_Interpolation::Linear;
			else if (text == "step")
				receiver = Animation_Interpolation::Step;
			else
				return false;
			return true;
		}
	}

	//单轨道合法性
	bool animation_track_valid(const Animation_Track& track)
	{
		//至少一帧
		if (track.keys.empty())
			return false;
		//上一帧时间（首帧无前驱）
		float previous_time = 0.0f;
		for (std::size_t index = 0; index < track.keys.size(); ++index)
		{
			//当前帧
			const Animation_Key& key = track.keys[index];
			//时间须非负且有限
			if (!std::isfinite(key.time) || key.time < 0.0f)
				return false;
			//时间须严格递增
			if (index > 0 && key.time <= previous_time)
				return false;
			//四分量须全为有限值
			for (uint32_t component = 0; component < 4; ++component)
				if (!std::isfinite(key.value[component]))
					return false;
			previous_time = key.time;
		}
		return true;
	}

	//片段关键帧最大时间
	float animation_clip_key_time_max(const Animation_Clip& clip)
	{
		//无关键帧
		float maximum = 0.0f;
		for (const Animation_Track& track : clip.tracks)
			for (const Animation_Key& key : track.keys)
				//取最大时间
				if (key.time > maximum)
					maximum = key.time;
		return maximum;
	}

	//片段合法性
	bool animation_clip_valid(const Animation_Clip& clip)
	{
		//至少一轨道
		if (clip.tracks.empty())
			return false;
		//时长须非负且有限
		if (!std::isfinite(clip.duration) || clip.duration < 0.0f)
			return false;
		//逐轨道校验
		for (const Animation_Track& track : clip.tracks)
			if (!animation_track_valid(track))
				return false;
		//事件触发点合法性
		if (!animation_events_valid(clip))
			return false;
		//时长须覆盖全部关键帧
		return clip.duration >= animation_clip_key_time_max(clip);
	}

	//事件合法性
	bool animation_events_valid(const Animation_Clip& clip)
	{
		//上一事件时间（首项无前驱）
		float previous_time = 0.0f;
		for (std::size_t index = 0; index < clip.events.size(); ++index)
		{
			const Animation_Event& event = clip.events[index];
			//时间非负有限
			if (!std::isfinite(event.time) || event.time < 0.0f)
				return false;
			//名称非空
			if (event.name.empty())
				return false;
			//时间非递减
			if (index > 0 && event.time < previous_time)
				return false;
			previous_time = event.time;
		}
		return true;
	}

	//按骨骼名与通道查找轨道
	const Animation_Track* animation_track_find(const Animation_Clip& clip, const std::string& bone,
		Animation_Channel channel)
	{
		for (const Animation_Track& track : clip.tracks)
			//骨骼名与通道同时一致即命中
			if (track.channel == channel && track.bone == bone)
				return &track;
		return nullptr;
	}

	//从 JSON 配置构建片段
	bool animation_clip_build(const nlohmann::json& config, Animation_Clip& receiver)
	{
		//先清空输出
		receiver = Animation_Clip();
		//根须为对象
		if (!config.is_object())
			return false;
		//时长必填且为数值
		if (!config.contains("duration") || !config["duration"].is_number())
			return false;
		Animation_Clip clip;
		//名称可选
		if (config.contains("name"))
		{
			if (!config["name"].is_string())
				return false;
			clip.name = config["name"].get<std::string>();
		}
		//轨道必填且为非空数组
		if (!config.contains("tracks") || !config["tracks"].is_array() || config["tracks"].empty())
			return false;
		try
		{
			clip.duration = config["duration"].get<float>();
			for (const nlohmann::json& track_config : config["tracks"])
			{
				//逐轨道字段校验
				if (!track_config.is_object())
					return false;
				if (!track_config.contains("bone") || !track_config["bone"].is_string())
					return false;
				if (!track_config.contains("channel") || !track_config["channel"].is_string())
					return false;
				Animation_Track track;
				track.bone = track_config["bone"].get<std::string>();
				//空骨骼名视为非法
				if (track.bone.empty())
					return false;
				//通道解析
				if (!channel_parse(track_config["channel"].get<std::string>(), track.channel))
					return false;
				//插值可选（默认线性）
				if (track_config.contains("interpolation"))
				{
					if (!track_config["interpolation"].is_string())
						return false;
					if (!interpolation_parse(track_config["interpolation"].get<std::string>(), track.interpolation))
						return false;
				}
				//关键帧必填
				if (!track_config.contains("keys") || !track_config["keys"].is_array())
					return false;
				for (const nlohmann::json& key_config : track_config["keys"])
				{
					if (!key_config.is_object())
						return false;
					if (!key_config.contains("time") || !key_config["time"].is_number())
						return false;
					if (!key_config.contains("value") || !key_config["value"].is_array())
						return false;
					//值须为 3 或 4 个分量
					const nlohmann::json& values = key_config["value"];
					if (values.size() != 3 && values.size() != 4)
						return false;
					Animation_Key key;
					key.time = key_config["time"].get<float>();
					for (std::size_t component = 0; component < values.size(); ++component)
					{
						if (!values[component].is_number())
							return false;
						key.value[component] = values[component].get<float>();
					}
					track.keys.push_back(key);
				}
				clip.tracks.push_back(std::move(track));
			}
			//事件可选（时间 + 名称；合法性在整体校验中判定）
			if (config.contains("events"))
			{
				if (!config["events"].is_array())
					return false;
				for (const nlohmann::json& event_config : config["events"])
				{
					if (!event_config.is_object())
						return false;
					if (!event_config.contains("time") || !event_config["time"].is_number())
						return false;
					if (!event_config.contains("name") || !event_config["name"].is_string())
						return false;
					Animation_Event event;
					event.time = event_config["time"].get<float>();
					event.name = event_config["name"].get<std::string>();
					clip.events.push_back(std::move(event));
				}
			}
		}
		catch (...)
		{
			//数值越界等转换异常一律拒绝
			return false;
		}
		//整体合法性（时间递增、时长覆盖等）
		if (!animation_clip_valid(clip))
			return false;
		receiver = std::move(clip);
		return true;
	}

	//从 .anim 文件加载片段
	bool animation_clip_load_file(const std::string& path, Animation_Clip& receiver)
	{
		//先清空输出
		receiver = Animation_Clip();
		//路径有效性
		if (!detail::path_check(path))
			return false;
		//文件体量须在合法区间
		uintmax_t size = 0;
		std::error_code error_info;
		if (!detail::file_size_get(path, size, error_info) || size == 0 || size > ANIMATION_CLIP_FILE_LIMIT)
			return false;
		//整文件读取
		std::ifstream file(detail::string_to_path(path), std::ios::binary);
		if (!file.is_open())
			return false;
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		//解析 JSON（不抛异常）
		nlohmann::json config = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
		if (config.is_discarded())
			return false;
		return animation_clip_build(config, receiver);
	}

	//收集时间区间内的事件
	bool animation_events_collect(const Animation_Clip& clip, float begin, float end, bool loop,
		std::vector<const Animation_Event*>& out)
	{
		//入参前置
		if (!std::isfinite(begin) || !std::isfinite(end) || end <= begin || clip.events.empty())
			return false;
		const float duration = clip.duration;
		//时长缺失时无法回绕
		if (duration <= 0.0f)
			return false;
		//区间长度不短于整段：全部事件按序命中一次
		if (end - begin >= duration)
		{
			for (const Animation_Event& event : clip.events)
				out.push_back(&event);
			return true;
		}
		//非循环：夹紧到 [0, duration] 后单段筛选
		if (!loop)
		{
			const float from = begin < 0.0f ? 0.0f : begin;
			const float to = end > duration ? duration : end;
			for (const Animation_Event& event : clip.events)
				if (event.time >= from && event.time < to)
					out.push_back(&event);
			return true;
		}
		//循环：归一化起点后可能跨过片段末尾
		float from = std::fmod(begin, duration);
		if (from < 0.0f)
			from += duration;
		float to = from + (end - begin);
		if (to <= duration)
		{
			for (const Animation_Event& event : clip.events)
				if (event.time >= from && event.time < to)
					out.push_back(&event);
			return true;
		}
		//跨末尾：先收 [from, duration)，再收 [0, to - duration)
		const float tail = to - duration;
		for (const Animation_Event& event : clip.events)
			if (event.time >= from)
				out.push_back(&event);
		for (const Animation_Event& event : clip.events)
			if (event.time < tail)
				out.push_back(&event);
		return true;
	}
}
