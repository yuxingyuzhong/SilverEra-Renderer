//获取片段应用
#include "../动画应用.h"
//获取单轨道采样器
#include "../动画采样.h"
//std::isfinite
#include <cmath>

namespace engine
{
	//按片段在 time 处构建姿态
	bool animation_clip_pose_build(const Animation_Clip& clip,
		const std::shared_ptr<const Animation_Skeleton>& skeleton, float time, bool loop, Animation_Pose& pose)
	{
		//片段、骨架与时间前置
		if (!animation_clip_valid(clip) || !skeleton || !skeleton->valid() || !std::isfinite(time))
			return false;
		//复位到绑定姿态（复用系统层姿态容器）
		if (!pose.build(skeleton))
			return false;
		//逐骨骼应用通道
		for (uint32_t bone = 0; bone < skeleton->bone_count(); ++bone)
		{
			const Animation_Bone* bone_data = skeleton->bone_get(bone);
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
		//求值世界矩阵（系统层中立求值）
		return skeleton_pose_world_update(pose);
	}

	//按两片段混合构建姿态
	bool animation_clip_pose_blend(const Animation_Clip& clip_a, const Animation_Clip& clip_b,
		const std::shared_ptr<const Animation_Skeleton>& skeleton, float time_a, float time_b, float weight,
		bool loop_a, bool loop_b, Animation_Pose& pose)
	{
		//时间与权重前置
		if (!std::isfinite(time_a) || !std::isfinite(time_b) || !std::isfinite(weight))
			return false;
		//权重夹紧到 [0,1]
		if (weight < 0.0f)
			weight = 0.0f;
		if (weight > 1.0f)
			weight = 1.0f;
		//两份源姿态
		Animation_Pose pose_a;
		Animation_Pose pose_b;
		if (!animation_clip_pose_build(clip_a, skeleton, time_a, loop_a, pose_a))
			return false;
		if (!animation_clip_pose_build(clip_b, skeleton, time_b, loop_b, pose_b))
			return false;
		//输出姿态按骨架初始化（尺寸基准）
		if (!pose.build(skeleton))
			return false;
		const uint32_t bone_count = skeleton->bone_count();
		//逐骨骼混合局部 TRS
		for (uint32_t bone = 0; bone < bone_count; ++bone)
		{
			const std::size_t trs3 = static_cast<std::size_t>(bone) * 3;
			const std::size_t quat4 = static_cast<std::size_t>(bone) * 4;
			float translation[3] = {};
			float rotation[4] = {};
			float scale[3] = {};
			//平移与缩放：线性插值
			for (uint32_t component = 0; component < 3; ++component)
			{
				translation[component] = pose_a.local_translation[trs3 + component] * (1.0f - weight)
					+ pose_b.local_translation[trs3 + component] * weight;
				scale[component] = pose_a.local_scale[trs3 + component] * (1.0f - weight)
					+ pose_b.local_scale[trs3 + component] * weight;
			}
			//旋转：点积取反后线性混合（避免绕长弧插值）
			float dot = pose_a.local_rotation[quat4] * pose_b.local_rotation[quat4]
				+ pose_a.local_rotation[quat4 + 1] * pose_b.local_rotation[quat4 + 1]
				+ pose_a.local_rotation[quat4 + 2] * pose_b.local_rotation[quat4 + 2]
				+ pose_a.local_rotation[quat4 + 3] * pose_b.local_rotation[quat4 + 3];
			const float sign = dot < 0.0f ? -1.0f : 1.0f;
			float length_square = 0.0f;
			for (uint32_t component = 0; component < 4; ++component)
			{
				rotation[component] = pose_a.local_rotation[quat4 + component] * (1.0f - weight)
					+ pose_b.local_rotation[quat4 + component] * weight * sign;
				length_square += rotation[component] * rotation[component];
			}
			//归一化（零长度退化为单位旋转）
			const float length = std::sqrt(length_square);
			if (length > 0.0f)
				for (uint32_t component = 0; component < 4; ++component)
					rotation[component] /= length;
			else
			{
				rotation[0] = 0.0f;
				rotation[1] = 0.0f;
				rotation[2] = 0.0f;
				rotation[3] = 1.0f;
			}
			//写回局部变换
			if (!pose.local_set(bone, translation, rotation, scale))
				return false;
		}
		//混合后统一求值世界矩阵
		return skeleton_pose_world_update(pose);
	}
}
