//获取动画播放器
#include "../动画播放器.h"
//std::isfinite（时间与速度的有限性校验）
#include <cmath>

namespace engine
{
	//绑定片段
	bool Animation_Player::build(const std::shared_ptr<const Animation_Clip>& shared_clip)
	{
		//先清空状态
		clip.reset();
		time = 0.0f;
		speed = 1.0f;
		loop = true;
		is_playing = false;
		//空指针或非法片段
		if (!shared_clip || !animation_clip_valid(*shared_clip))
			return false;
		clip = shared_clip;
		return true;
	}

	//当前片段时长
	float Animation_Player::duration_get(void) const
	{
		return clip ? clip->duration : 0.0f;
	}

	//当前片段
	const Animation_Clip* Animation_Player::clip_get(void) const
	{
		return clip.get();
	}

	//进入播放态
	void Animation_Player::play(void)
	{
		//未绑定片段不进入播放态
		if (clip)
			is_playing = true;
	}

	//进入暂停态
	void Animation_Player::pause(void)
	{
		is_playing = false;
	}

	//是否播放中
	bool Animation_Player::playing(void) const
	{
		return is_playing;
	}

	//当前时间
	float Animation_Player::time_get(void) const
	{
		return time;
	}

	//时间跳转
	void Animation_Player::time_set(float time)
	{
		//未绑定片段直接复位
		if (!clip)
		{
			this->time = 0.0f;
			return;
		}
		//按循环规则归一
		this->time = animation_time_normalize(time, clip->duration, loop);
	}

	//速度写入
	void Animation_Player::speed_set(float speed)
	{
		//仅接受有限非负速度
		if (!std::isfinite(speed) || speed < 0.0f)
			return;
		this->speed = speed;
	}

	//速度读取
	float Animation_Player::speed_get(void) const
	{
		return speed;
	}

	//循环开关
	void Animation_Player::loop_set(bool loop)
	{
		this->loop = loop;
		//切换后按新规则归一当前时间（关闭循环时避免停留在区间外）
		if (clip)
			time = animation_time_normalize(time, clip->duration, loop);
	}

	//循环读取
	bool Animation_Player::loop_get(void) const
	{
		return loop;
	}

	//按 delta 秒推进
	void Animation_Player::time_advance(float delta)
	{
		//仅播放态推进；非有限 delta 忽略
		if (!is_playing || !clip || !std::isfinite(delta))
			return;
		//时间按速度缩放并归一
		time = animation_time_normalize(time + delta * speed, clip->duration, loop);
	}

	//按当前时间采样
	bool Animation_Player::track_sample(const std::string& bone, Animation_Channel channel, float out[4]) const
	{
		//未绑定片段
		if (!clip)
			return false;
		//查找轨道
		const Animation_Track* track = animation_track_find(*clip, bone, channel);
		if (track == nullptr)
			return false;
		//按当前时间采样（时间已在区间内，循环规则不影响结果）
		return animation_track_sample(*track, time, clip->duration, loop, out);
	}
}
