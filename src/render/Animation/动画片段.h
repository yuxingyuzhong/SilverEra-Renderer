#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

/*
渲染动画 · 动画片段数据结构（方向十 · 阶段 B 第 4 轮）
本头 Vulkan-free：只描述片段 / 轨道 / 关键帧与合法性规则，不含采样与播放逻辑（第 6–7 轮）。
轨道按「骨骼名 ＋ 通道」绑定；关键帧时间须非负、有限、严格递增，片段时长须覆盖全部关键帧。
平移与缩放取 value 的 xyz；旋转取 xyzw 四元数。默认空片段不合法，播放器不得进入播放态。
*/
namespace engine
{
	//插值模式
	enum class Animation_Interpolation : uint8_t
	{
		//线性插值
		Linear = 0,
		//阶跃插值（保持前一关键帧的值）
		Step = 1,
	};

	//轨道通道
	enum class Animation_Channel : uint8_t
	{
		//平移（value 的 xyz 有效）
		Translation = 0,
		//旋转（value 为四元数 xyzw）
		Rotation = 1,
		//缩放（value 的 xyz 有效）
		Scale = 2,
	};

	//关键帧
	struct Animation_Key
	{
		//时间（秒；须非负且有限）
		float time = 0.0f;
		//值（平移 / 缩放用 xyz，旋转用 xyzw）
		float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	//轨道（绑定到「骨骼名 ＋ 通道」）
	struct Animation_Track
	{
		//目标骨骼名
		std::string bone;
		//目标通道
		Animation_Channel channel = Animation_Channel::Translation;
		//插值模式
		Animation_Interpolation interpolation = Animation_Interpolation::Linear;
		//关键帧（时间严格递增）
		std::vector<Animation_Key> keys;
	};

	//动画片段
	struct Animation_Clip
	{
		//片段名
		std::string name;
		//片段时长（秒；须不小于任何关键帧时间）
		float duration = 0.0f;
		//轨道集合
		std::vector<Animation_Track> tracks;
	};

	//单轨道合法性（至少一帧；时间非负有限且严格递增；值有限）
	bool animation_track_valid(const Animation_Track& track);
	//片段合法性（至少一轨道；全部轨道合法；时长有限且覆盖所有关键帧）
	bool animation_clip_valid(const Animation_Clip& clip);
	//片段关键帧最大时间（无关键帧返回 0）
	float animation_clip_key_time_max(const Animation_Clip& clip);
	//按骨骼名与通道查找轨道（不存在返回空指针）
	const Animation_Track* animation_track_find(const Animation_Clip& clip, const std::string& bone,
		Animation_Channel channel);
}
