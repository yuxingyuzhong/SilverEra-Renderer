//获取动画采样器
#include "../动画采样.h"
//std::fmod / std::sqrt（时间回绕与四元数归一化）
#include <cmath>

namespace engine
{
	//采样时间归一
	float animation_time_normalize(float time, float duration, bool loop)
	{
		//时长非法时按零时间处理（避免除零与回绕失真）
		if (!std::isfinite(duration) || duration <= 0.0f)
			return 0.0f;
		//时间非法时按零时间处理
		if (!std::isfinite(time))
			return 0.0f;
		//循环：回绕到 [0, duration)
		if (loop)
		{
			float normalized = std::fmod(time, duration);
			if (normalized < 0.0f)
				normalized += duration;
			return normalized;
		}
		//非循环：夹紧到闭区间
		if (time < 0.0f)
			return 0.0f;
		if (time > duration)
			return duration;
		return time;
	}

	//单轨道采样
	bool animation_track_sample(const Animation_Track& track, float time, float duration, bool loop,
		float out[4])
	{
		//空轨道或空输出
		if (track.keys.empty() || out == nullptr)
			return false;
		//时长与时间须有限且时长为正
		if (!std::isfinite(duration) || duration <= 0.0f || !std::isfinite(time))
			return false;
		//归一后的采样时间
		const float sample_time = animation_time_normalize(time, duration, loop);
		//首帧与末帧
		const Animation_Key& first = track.keys.front();
		const Animation_Key& last = track.keys.back();
		//早于首帧取首帧
		if (sample_time <= first.time)
		{
			for (uint32_t component = 0; component < 4; ++component)
				out[component] = first.value[component];
			return true;
		}
		//晚于末帧取末帧
		if (sample_time >= last.time)
		{
			for (uint32_t component = 0; component < 4; ++component)
				out[component] = last.value[component];
			return true;
		}
		//定位左帧（保证 index + 1 有效）
		std::size_t index = 0;
		while (index + 1 < track.keys.size() && sample_time >= track.keys[index + 1].time)
			++index;
		const Animation_Key& left = track.keys[index];
		const Animation_Key& right = track.keys[index + 1];
		//阶跃插值：保持左帧值
		if (track.interpolation == Animation_Interpolation::Step)
		{
			for (uint32_t component = 0; component < 4; ++component)
				out[component] = left.value[component];
			return true;
		}
		//线性插值
		const float span = right.time - left.time;
		const float alpha = span > 0.0f ? (sample_time - left.time) / span : 0.0f;
		for (uint32_t component = 0; component < 4; ++component)
			out[component] = left.value[component] + (right.value[component] - left.value[component]) * alpha;
		//旋转通道按四元数长度归一化
		if (track.channel == Animation_Channel::Rotation)
		{
			const float length = std::sqrt(out[0] * out[0] + out[1] * out[1]
				+ out[2] * out[2] + out[3] * out[3]);
			if (length > 0.0f)
				for (uint32_t component = 0; component < 4; ++component)
					out[component] /= length;
		}
		return true;
	}
}
