//获取片段应用
#include "../动画应用.h"
//获取单轨道采样器
#include "../动画采样.h"
//std::isfinite
#include <cmath>

namespace engine
{
	//按片段在 time 处构建姿态
	bool animation_clip_pose_build(const Animation_Clip& clip, const Animation_Skeleton& skeleton,
		float time, bool loop, Animation_Pose& pose)
	{
		//片段、骨架与时间前置
		if (!animation_clip_valid(clip) || !skeleton.valid() || !std::isfinite(time))
			return false;
		//复位到绑定姿态
		if (!pose.build(skeleton))
			return false;
		//逐骨骼应用通道
		for (uint32_t bone = 0; bone < skeleton.bone_count(); ++bone)
		{
			const Animation_Bone* bone_data = skeleton.bone_get(bone);
			if (bone_data == nullptr)
				return false;
			//局部 TRS（默认沿用绑定值）
			float translation[3] = {
				bone_data->bind_translation[0], bone_data->bind_translation[1], bone_data->bind_translation[2] };
			float rotation[4] = {
				bone_data->bind_rotation[0], bone_data->bind_rotation[1],
				bone_data->bind_rotation[2], bone_data->bind_rotation[3] };
			float scale[3] = {
				bone_data->bind_scale[0], bone_data->bind_scale[1], bone_data->bind_scale[2] };
			//平移通道
			if (const Animation_Track* track = animation_track_find(clip, bone_data->name, Animation_Channel::Translation))
			{
				float value[4] = {};
				if (animation_track_sample(*track, time, clip.duration, loop, value))
					for (uint32_t component = 0; component < 3; ++component)
						translation[component] = value[component];
			}
			//旋转通道
			if (const Animation_Track* track = animation_track_find(clip, bone_data->name, Animation_Channel::Rotation))
			{
				float value[4] = {};
				if (animation_track_sample(*track, time, clip.duration, loop, value))
					for (uint32_t component = 0; component < 4; ++component)
						rotation[component] = value[component];
			}
			//缩放通道
			if (const Animation_Track* track = animation_track_find(clip, bone_data->name, Animation_Channel::Scale))
			{
				float value[4] = {};
				if (animation_track_sample(*track, time, clip.duration, loop, value))
					for (uint32_t component = 0; component < 3; ++component)
						scale[component] = value[component];
			}
			//写回局部变换
			if (!pose.local_set(bone, translation, rotation, scale))
				return false;
		}
		//求值世界矩阵
		return pose.world_update(skeleton);
	}
}
