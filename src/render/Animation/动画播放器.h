#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//获取片段 / 轨道类型与采样器
#include "Engine/EngineRenderer/src/render/Animation/动画片段.h"
#include "Engine/EngineRenderer/src/render/Animation/动画采样.h"

/*
渲染动画 · 播放器（方向十 · 阶段 B 第 7 轮）
本头 Vulkan-free：持有共享片段与播放状态（时间 / 速度 / 循环 / 播放态），不直接访问 GPU。
时间推进只在播放态生效；时间与速度均经归一化：循环回绕、非循环夹紧到片段区间。
绑定空指针或非法片段会清空状态并返回假；未绑定时采样与时长查询均返回零值 / 假。
片段切换由再次 build 完成，时间复位到 0 并暂停。
*/
namespace engine
{
	class Animation_Player
	{
	public:
		//默认构造（未绑定片段）
		Animation_Player(void) = default;

		//绑定片段（失败时清空状态）
		bool build(const std::shared_ptr<const Animation_Clip>& shared_clip);
		//当前片段时长（未绑定返回 0）
		float duration_get(void) const;
		//当前片段（未绑定返回空）
		const Animation_Clip* clip_get(void) const;

		//进入播放态
		void play(void);
		//进入暂停态
		void pause(void);
		//是否播放中
		bool playing(void) const;

		//当前时间读取
		float time_get(void) const;
		//时间跳转（按循环规则归一）
		void time_set(float time);
		//速度写入（仅接受有限非负值，非法值忽略）
		void speed_set(float speed);
		//速度读取
		float speed_get(void) const;
		//循环开关
		void loop_set(bool loop);
		//循环读取
		bool loop_get(void) const;

		//按 delta 秒推进（仅播放态；非有限 delta 忽略）
		void time_advance(float delta);
		//按当前时间采样指定骨骼通道（未绑定或轨道缺失返回假）
		bool track_sample(const std::string& bone, Animation_Channel channel, float out[4]) const;

	private:
		//共享片段（只读）
		std::shared_ptr<const Animation_Clip> clip;
		//当前时间（秒）
		float time = 0.0f;
		//播放速度（非负有限）
		float speed = 1.0f;
		//循环开关（默认循环）
		bool loop = true;
		//播放态
		bool is_playing = false;
	};
}
