#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//获取片段 / 轨道类型
#include "Engine/EngineRenderer/src/render/Animation/动画片段.h"

/*
渲染动画 · 采样器（方向十 · 阶段 B 第 6 轮）
本头 Vulkan-free：只负责「时间归一 ＋ 单轨道取值」，不推进播放时间（第 7 轮播放器）。
时间规则：循环（loop = true）按片段时长回绕到 [0, duration)；非循环夹紧到 [0, duration]。
取值规则：时间早于首帧取首帧、晚于末帧取末帧；Step 保持左帧；Linear 在相邻帧间插值；
旋转通道线性插值后按四元数长度归一化（nlerp），长度为零时保持插值原值。
前置条件：轨道关键帧已按时间升序（由片段合法性或加载校验保证）。
*/
namespace engine
{
	//采样时间归一（循环回绕 / 非循环夹紧）
	float animation_time_normalize(float time, float duration, bool loop);
	//单轨道采样（空轨道、非法时长或空输出返回假）
	bool animation_track_sample(const Animation_Track& track, float time, float duration, bool loop,
		float out[4]);
}
