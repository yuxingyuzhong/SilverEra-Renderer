//获取叠加层入口
#include "src/render/Animation/动画应用.h"
//std::isfinite / std::sqrt
#include <cmath>

namespace engine
{
	namespace
	{
		//四元数归一化（零长度退化为单位旋转）
		void quat_normalize(float quaternion[4])
		{
			const float length = std::sqrt(quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1]
				+ quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
			if (length > 0.0f)
			{
				for (uint32_t component = 0; component < 4; ++component)
					quaternion[component] /= length;
				return;
			}
			quaternion[0] = 0.0f;
			quaternion[1] = 0.0f;
			quaternion[2] = 0.0f;
			quaternion[3] = 1.0f;
		}

		//四元数乘法（out = left × right）
		void quat_multiply(const float left[4], const float right[4], float out[4])
		{
			const float x = left[3] * right[0] + left[0] * right[3] + left[1] * right[2] - left[2] * right[1];
			const float y = left[3] * right[1] - left[0] * right[2] + left[1] * right[3] + left[2] * right[0];
			const float z = left[3] * right[2] + left[0] * right[1] - left[1] * right[0] + left[2] * right[3];
			const float w = left[3] * right[3] - left[0] * right[0] - left[1] * right[1] - left[2] * right[2];
			out[0] = x;
			out[1] = y;
			out[2] = z;
			out[3] = w;
		}

		//四元数共轭（单位四元数即逆）
		void quat_conjugate(const float source[4], float out[4])
		{
			out[0] = -source[0];
			out[1] = -source[1];
			out[2] = -source[2];
			out[3] = source[3];
		}
	}

	//叠加层应用
	bool animation_pose_layer_apply(const Animation_Skeleton& skeleton, const Animation_Pose& base,
		const Animation_Pose& overlay, float weight, Animation_Pose& pose)
	{
		//骨架与权重前置
		if (!skeleton.valid() || !std::isfinite(weight))
			return false;
		//权重夹紧到 [0,1]
		if (weight < 0.0f)
			weight = 0.0f;
		if (weight > 1.0f)
			weight = 1.0f;
		const uint32_t bone_count = skeleton.bone_count();
		//尺寸校验（三组局部 TRS 均须与骨骼数一致）
		const std::size_t trs3 = static_cast<std::size_t>(bone_count) * 3;
		const std::size_t quat4 = static_cast<std::size_t>(bone_count) * 4;
		if (base.translation.size() != trs3 || overlay.translation.size() != trs3
			|| base.scale.size() != trs3 || overlay.scale.size() != trs3
			|| base.rotation.size() != quat4 || overlay.rotation.size() != quat4)
			return false;
		//输出姿态按骨架初始化（尺寸基准）
		if (!pose.build(skeleton))
			return false;
		//逐骨骼叠加
		for (uint32_t bone = 0; bone < bone_count; ++bone)
		{
			const Animation_Bone* bone_data = skeleton.bone_get(bone);
			if (bone_data == nullptr)
				return false;
			const std::size_t t3 = static_cast<std::size_t>(bone) * 3;
			const std::size_t r4 = static_cast<std::size_t>(bone) * 4;
			float translation[3] = {};
			float rotation[4] = {};
			float scale[3] = {};
			//平移：base + (overlay - bind) × w
			for (uint32_t component = 0; component < 3; ++component)
				translation[component] = base.translation[t3 + component]
					+ (overlay.translation[t3 + component] - bone_data->bind_translation[component]) * weight;
			//缩放：base × (1 + (overlay / bind - 1) × w)
			for (uint32_t component = 0; component < 3; ++component)
			{
				const float bind_value = bone_data->bind_scale[component] != 0.0f
					? bone_data->bind_scale[component] : 1.0f;
				const float ratio = overlay.scale[t3 + component] / bind_value;
				scale[component] = base.scale[t3 + component] * (1.0f + (ratio - 1.0f) * weight);
			}
			/*
			旋转：delta = overlay × inverse(bind)，按权重向单位四元数插值，再乘 base
			overlay 与绑定一致时 delta 为单位旋转，叠加结果等于 base。
			*/
			float bind_conjugate[4] = {};
			quat_conjugate(bone_data->bind_rotation, bind_conjugate);
			float overlay_local[4] = {
				overlay.rotation[r4], overlay.rotation[r4 + 1], overlay.rotation[r4 + 2], overlay.rotation[r4 + 3] };
			float delta[4] = {};
			quat_multiply(overlay_local, bind_conjugate, delta);
			//向单位四元数线性插值（点积取反避免长弧）
			const float dot = delta[3];
			const float sign = dot < 0.0f ? -1.0f : 1.0f;
			float scaled_delta[4] = {
				delta[0] * weight * sign, delta[1] * weight * sign,
				delta[2] * weight * sign, 1.0f + (delta[3] * sign - 1.0f) * weight };
			float base_local[4] = {
				base.rotation[r4], base.rotation[r4 + 1], base.rotation[r4 + 2], base.rotation[r4 + 3] };
			quat_multiply(scaled_delta, base_local, rotation);
			//归一化后写回
			quat_normalize(rotation);
			if (!pose.local_set(bone, translation, rotation, scale))
				return false;
		}
		//叠加后统一求值世界矩阵
		return pose.world_update(skeleton);
	}
}
