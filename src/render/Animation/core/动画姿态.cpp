//获取骨架与姿态
#include "../动画姿态.h"
//std::isfinite / std::sqrt（参数校验与四元数归一化）
#include <cmath>
//路径转换与可用性校验（骨架 JSON 加载）
#include "Engine/EngineFoundation/src/detail/methods/路径字符串转换.h"
#include "Engine/EngineFoundation/src/detail/methods/文件路径可用性校验.h"

namespace engine
{
	namespace
	{
		//骨架文件体量上限（16 MB）
		constexpr uintmax_t SKELETON_FILE_LIMIT = 16ull * 1024ull * 1024ull;

		//读取指定长度的浮点数组（类型与有限性校验）
		bool float_array_read(const nlohmann::json& value, float* out, std::size_t count)
		{
			if (!value.is_array() || value.size() != count)
				return false;
			for (std::size_t index = 0; index < count; ++index)
			{
				if (!value[index].is_number())
					return false;
				out[index] = value[index].get<float>();
				if (!std::isfinite(out[index]))
					return false;
			}
			return true;
		}
	}

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

	//蒙皮调色板求值
	bool animation_skin_palette_build(const Animation_Skeleton& skeleton, const Animation_Pose& pose,
		float* palette, uint32_t count)
	{
		//空输出或零骨骼
		if (palette == nullptr || count == 0)
			return false;
		//骨架须合法且数量一致
		if (!skeleton.valid() || skeleton.bones.size() != count)
			return false;
		//姿态世界矩阵须与骨骼数量一致
		if (pose.world.size() != static_cast<std::size_t>(count) * 16)
			return false;
		//逐骨骼求 world × inverse_bind
		for (uint32_t index = 0; index < count; ++index)
			animation_matrix_multiply(pose.world.data() + static_cast<std::size_t>(index) * 16,
				skeleton.bones[index].inverse_bind, palette + static_cast<std::size_t>(index) * 16);
		return true;
	}

	//从 JSON 构建骨架
	bool animation_skeleton_build(const nlohmann::json& config, Animation_Skeleton& receiver)
	{
		receiver.bones.clear();
		if (!config.is_object() || !config.contains("bones") || !config["bones"].is_array())
			return false;
		try
		{
			for (const nlohmann::json& bone_config : config["bones"])
			{
				if (!bone_config.is_object())
					return false;
				if (!bone_config.contains("name") || !bone_config["name"].is_string())
					return false;
				Animation_Bone bone;
				bone.name = bone_config["name"].get<std::string>();
				if (bone.name.empty())
					return false;
				//父下标（可选）
				if (bone_config.contains("parent"))
				{
					if (!bone_config["parent"].is_number_integer())
						return false;
					bone.parent = bone_config["parent"].get<int32_t>();
				}
				//绑定 TRS（可选）
				if (bone_config.contains("bind_translation")
					&& !float_array_read(bone_config["bind_translation"], bone.bind_translation, 3))
					return false;
				if (bone_config.contains("bind_rotation")
					&& !float_array_read(bone_config["bind_rotation"], bone.bind_rotation, 4))
					return false;
				if (bone_config.contains("bind_scale")
					&& !float_array_read(bone_config["bind_scale"], bone.bind_scale, 3))
					return false;
				if (bone_config.contains("inverse_bind")
					&& !float_array_read(bone_config["inverse_bind"], bone.inverse_bind, 16))
					return false;
				receiver.bones.push_back(std::move(bone));
			}
		}
		catch (...)
		{
			receiver.bones.clear();
			return false;
		}
		//整体合法性（父下标须小于自身）
		if (!receiver.valid())
		{
			receiver.bones.clear();
			return false;
		}
		return true;
	}

	//从文件加载骨架
	bool animation_skeleton_load_file(const std::string& path, Animation_Skeleton& receiver)
	{
		receiver.bones.clear();
		//路径与体量
		if (!detail::path_check(path))
			return false;
		uintmax_t size = 0;
		std::error_code error_info;
		if (!detail::file_size_get(path, size, error_info) || size == 0 || size > SKELETON_FILE_LIMIT)
			return false;
		//整文件读取
		std::ifstream file(detail::string_to_path(path), std::ios::binary);
		if (!file.is_open())
			return false;
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		nlohmann::json config = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
		if (config.is_discarded())
			return false;
		return animation_skeleton_build(config, receiver);
	}
}
