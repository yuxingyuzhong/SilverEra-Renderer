//获取蒙皮资产入口
#include "src/render/Asset/蒙皮资产.h"
//OBJ 网格加载（几何来源）
#include "src/render/Asset/资产加载.h"
//调色板骨骼数上限
#include "src/render/Animation/动画数据.h"
//侧车 JSON
#include <nlohmann/json.hpp>
//路径转换、可用性与体量
#include "Engine/EngineFoundation/src/detail/methods/路径字符串转换.h"
#include "Engine/EngineFoundation/src/detail/methods/文件路径可用性校验.h"
//日志系统
#include "Engine/EngineFoundation/src/log/日志系统运行包.h"

namespace engine
{
	namespace
	{
		//侧车文件体量上限（8 MB）
		constexpr uintmax_t SKIN_FILE_LIMIT = 8ull * 1024ull * 1024ull;

		//读取定长整数数组（类型与范围校验）
		bool joints_read(const nlohmann::json& value, uint16_t* out, uint32_t bone_count)
		{
			if (!value.is_array() || value.size() != 4)
				return false;
			for (std::size_t index = 0; index < 4; ++index)
			{
				if (!value[index].is_number_unsigned() && !value[index].is_number_integer())
					return false;
				const int64_t parsed = value[index].get<int64_t>();
				//关节下标须在 [0, bone_count)
				if (parsed < 0 || parsed >= static_cast<int64_t>(bone_count))
					return false;
				out[index] = static_cast<uint16_t>(parsed);
			}
			return true;
		}

		//读取定长权重数组（0..65535）
		bool weights_read(const nlohmann::json& value, uint16_t* out)
		{
			if (!value.is_array() || value.size() != 4)
				return false;
			for (std::size_t index = 0; index < 4; ++index)
			{
				if (!value[index].is_number_unsigned() && !value[index].is_number_integer())
					return false;
				const int64_t parsed = value[index].get<int64_t>();
				//权重须在 uint16 范围内
				if (parsed < 0 || parsed > 65535)
					return false;
				out[index] = static_cast<uint16_t>(parsed);
			}
			return true;
		}
	}

	//加载蒙皮资产
	bool skinned_asset_load(const std::string& obj_path, const std::string& skin_path,
		Skinned_Mesh_Data& out, uint32_t& bone_count_out)
	{
		//默认无结果
		out = Skinned_Mesh_Data();
		bone_count_out = 0;
		//OBJ 几何
		Mesh_File_Data geometry;
		if (!obj_mesh_load(obj_path, geometry) || geometry.data.vertices.empty() || geometry.data.indices.empty())
		{
			logger.error("蒙皮资产:: OBJ 加载失败或为空（{}）", obj_path);
			return false;
		}
		//侧车路径与体量
		if (!detail::path_check(skin_path))
			return false;
		uintmax_t size = 0;
		std::error_code error_info;
		if (!detail::file_size_get(skin_path, size, error_info) || size == 0 || size > SKIN_FILE_LIMIT)
		{
			logger.error("蒙皮资产:: 侧车文件缺失或超限（{}）", skin_path);
			return false;
		}
		//整文件读取与解析（不抛异常）
		std::ifstream file(detail::string_to_path(skin_path), std::ios::binary);
		if (!file.is_open())
			return false;
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		nlohmann::json config = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
		if (config.is_discarded() || !config.is_object())
			return false;
		//骨骼数
		if (!config.contains("bone_count") || !config["bone_count"].is_number_integer())
			return false;
		const int64_t bone_count = config["bone_count"].get<int64_t>();
		if (bone_count < 1 || bone_count > static_cast<int64_t>(ANIMATION_ACTOR_BONES_MAX))
		{
			logger.error("蒙皮资产:: 骨骼数 {} 超出范围 [1, {}]", bone_count, ANIMATION_ACTOR_BONES_MAX);
			return false;
		}
		//权重表数量须与顶点数一致
		if (!config.contains("weights") || !config["weights"].is_array()
			|| config["weights"].size() != geometry.data.vertices.size())
		{
			logger.error("蒙皮资产:: 权重项数与顶点数不一致（{} vs {}）",
				config.contains("weights") && config["weights"].is_array() ? config["weights"].size() : 0,
				geometry.data.vertices.size());
			return false;
		}
		//逐顶点组装
		Skinned_Mesh_Data assembled;
		assembled.indices = geometry.data.indices;
		assembled.vertices.resize(geometry.data.vertices.size());
		for (std::size_t index = 0; index < geometry.data.vertices.size(); ++index)
		{
			const Vertex& source = geometry.data.vertices[index];
			Skinned_Vertex& target = assembled.vertices[index];
			for (std::size_t component = 0; component < 3; ++component)
			{
				target.position[component] = source.position[component];
				target.normal[component] = source.normal[component];
			}
			for (std::size_t component = 0; component < 2; ++component)
				target.uv[component] = source.uv[component];
			const nlohmann::json& item = config["weights"][index];
			if (!item.is_object() || !item.contains("joint") || !item.contains("weight"))
				return false;
			if (!joints_read(item["joint"], target.joint, static_cast<uint32_t>(bone_count)))
			{
				logger.error("蒙皮资产:: 顶点 {} 关节下标非法（骨骼数 {}）", index, bone_count);
				return false;
			}
			if (!weights_read(item["weight"], target.weight))
			{
				logger.error("蒙皮资产:: 顶点 {} 权重非法", index);
				return false;
			}
		}
		out = std::move(assembled);
		bone_count_out = static_cast<uint32_t>(bone_count);
		return true;
	}
}
