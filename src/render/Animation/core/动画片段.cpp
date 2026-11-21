//获取动画片段数据结构
#include "../动画片段.h"
//std::isfinite（关键帧时间与值的有限性校验）
#include <cmath>

namespace engine
{
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
		//时长须覆盖全部关键帧
		return clip.duration >= animation_clip_key_time_max(clip);
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
}
