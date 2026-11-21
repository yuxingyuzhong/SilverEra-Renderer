//获取骨架与姿态
#include "../动画姿态.h"
//std::isfinite / std::sqrt（参数校验与四元数归一化）
#include <cmath>

namespace engine
{
	//TRS 合成
	void animation_trs_compose(const float translation[3], const float rotation[4], const float scale[3],
		float out[16])
	{
		//四元数归一化（零长度退化为单位旋转）
		float x = rotation[0];
		float y = rotation[1];
		float z = rotation[2];
		float w = rotation[3];
		const float length = std::sqrt(x * x + y * y + z * z + w * w);
		if (length > 0.0f)
		{
			x /= length;
			y /= length;
			z /= length;
			w /= length;
		}
		else
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
			w = 1.0f;
		}
		//旋转矩阵分量
		const float xx = x * x;
		const float yy = y * y;
		const float zz = z * z;
		const float xy = x * y;
		const float xz = x * z;
		const float yz = y * z;
		const float wx = w * x;
		const float wy = w * y;
		const float wz = w * z;
		//列主序：先写旋转，再按列乘缩放，最后写平移
		out[0] = (1.0f - 2.0f * (yy + zz)) * scale[0];
		out[1] = (2.0f * (xy + wz)) * scale[0];
		out[2] = (2.0f * (xz - wy)) * scale[0];
		out[3] = 0.0f;
		out[4] = (2.0f * (xy - wz)) * scale[1];
		out[5] = (1.0f - 2.0f * (xx + zz)) * scale[1];
		out[6] = (2.0f * (yz + wx)) * scale[1];
		out[7] = 0.0f;
		out[8] = (2.0f * (xz + wy)) * scale[2];
		out[9] = (2.0f * (yz - wx)) * scale[2];
		out[10] = (1.0f - 2.0f * (xx + yy)) * scale[2];
		out[11] = 0.0f;
		out[12] = translation[0];
		out[13] = translation[1];
		out[14] = translation[2];
		out[15] = 1.0f;
	}

	//矩阵乘（列主序）
	void animation_matrix_multiply(const float left[16], const float right[16], float out[16])
	{
		for (uint32_t column = 0; column < 4; ++column)
			for (uint32_t row = 0; row < 4; ++row)
			{
				float sum = 0.0f;
				for (uint32_t step = 0; step < 4; ++step)
					sum += left[step * 4 + row] * right[column * 4 + step];
				out[column * 4 + row] = sum;
			}
	}

	//骨架合法性
	bool Animation_Skeleton::valid(void) const
	{
		//至少一根骨骼
		if (bones.empty())
			return false;
		//父下标须为 -1 或小于自身下标
		for (std::size_t index = 0; index < bones.size(); ++index)
		{
			const int32_t parent = bones[index].parent;
			if (parent < -1 || parent >= static_cast<int32_t>(index))
				return false;
		}
		return true;
	}

	//骨骼数量
	uint32_t Animation_Skeleton::bone_count(void) const
	{
		return static_cast<uint32_t>(bones.size());
	}

	//骨骼读取
	const Animation_Bone* Animation_Skeleton::bone_get(uint32_t index) const
	{
		//越界
		if (index >= bones.size())
			return nullptr;
		return &bones[index];
	}

	//按名查骨骼
	int32_t Animation_Skeleton::bone_seek(const std::string& name) const
	{
		for (std::size_t index = 0; index < bones.size(); ++index)
			//名字一致即命中
			if (bones[index].name == name)
				return static_cast<int32_t>(index);
		return -1;
	}

	//按骨架初始化到绑定姿态
	bool Animation_Pose::build(const Animation_Skeleton& skeleton)
	{
		//骨架非法时清空
		if (!skeleton.valid())
		{
			translation.clear();
			rotation.clear();
			scale.clear();
			world.clear();
			return false;
		}
		const std::size_t count = skeleton.bones.size();
		translation.resize(count * 3);
		rotation.resize(count * 4);
		scale.resize(count * 3);
		world.resize(count * 16);
		for (std::size_t index = 0; index < count; ++index)
		{
			const Animation_Bone& bone = skeleton.bones[index];
			//复制绑定 TRS
			for (uint32_t component = 0; component < 3; ++component)
			{
				translation[index * 3 + component] = bone.bind_translation[component];
				scale[index * 3 + component] = bone.bind_scale[component];
			}
			for (uint32_t component = 0; component < 4; ++component)
				rotation[index * 4 + component] = bone.bind_rotation[component];
		}
		//立即求值世界矩阵
		return world_update(skeleton);
	}

	//骨骼局部变换写入
	bool Animation_Pose::local_set(uint32_t bone, const float in_translation[3], const float in_rotation[4],
		const float in_scale[3])
	{
		const std::size_t count = translation.size() / 3;
		//越界或空指针
		if (bone >= count || in_translation == nullptr || in_rotation == nullptr || in_scale == nullptr)
			return false;
		//参数须全部有限
		for (uint32_t component = 0; component < 3; ++component)
			if (!std::isfinite(in_translation[component]) || !std::isfinite(in_scale[component]))
				return false;
		for (uint32_t component = 0; component < 4; ++component)
			if (!std::isfinite(in_rotation[component]))
				return false;
		//写入
		for (uint32_t component = 0; component < 3; ++component)
		{
			translation[bone * 3 + component] = in_translation[component];
			scale[bone * 3 + component] = in_scale[component];
		}
		for (uint32_t component = 0; component < 4; ++component)
			rotation[bone * 4 + component] = in_rotation[component];
		return true;
	}

	//按父下标升序求值世界矩阵
	bool Animation_Pose::world_update(const Animation_Skeleton& skeleton)
	{
		//骨架非法
		if (!skeleton.valid())
			return false;
		const std::size_t count = skeleton.bones.size();
		//姿态尺寸须与骨架一致
		if (translation.size() != count * 3 || rotation.size() != count * 4 || scale.size() != count * 3
			|| world.size() != count * 16)
			return false;
		for (std::size_t index = 0; index < count; ++index)
		{
			//合成局部矩阵
			float local[16] = {};
			animation_trs_compose(translation.data() + index * 3, rotation.data() + index * 4,
				scale.data() + index * 3, local);
			const int32_t parent = skeleton.bones[index].parent;
			//根骨骼：世界矩阵即局部矩阵
			if (parent < 0)
			{
				for (uint32_t component = 0; component < 16; ++component)
					world[index * 16 + component] = local[component];
			}
			else
			{
				//子骨骼：父世界矩阵 × 局部矩阵（父已先于子求值）
				animation_matrix_multiply(world.data() + static_cast<std::size_t>(parent) * 16, local,
					world.data() + index * 16);
			}
		}
		return true;
	}

	//骨骼世界矩阵读取
	bool Animation_Pose::world_get(uint32_t bone, float out[16]) const
	{
		//越界或空输出
		if (out == nullptr || static_cast<std::size_t>(bone) * 16 + 16 > world.size())
			return false;
		for (uint32_t component = 0; component < 16; ++component)
			out[component] = world[bone * 16 + component];
		return true;
	}
}
