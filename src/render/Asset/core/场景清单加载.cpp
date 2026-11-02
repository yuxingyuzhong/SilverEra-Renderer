#include "src/render/Asset/局部命名空间使用.h"
//模块内共享解析工具（路径转换 / 去空白）
#include "src/render/Asset/core/文本解析工具.h"

//文件读取
#include <fstream>
#include <sstream>
//std::strtof（浮点字段解析，不抛异常）
#include <cstdlib>

namespace engine
{
	namespace
	{
		//清单文件体量上限（2 MiB）
		constexpr uint64_t MAX_FILE_BYTES = 2ull * 1024 * 1024;
		//摆放记录数量上限
		constexpr uint32_t MAX_PLACES = 20000u;

		//浮点解析（必须完整消费且为有限值）
		bool float_parse(const std::string& text, float& value)
		{
			//空串
			if (text.empty())
				return false;
			char* end = nullptr;
			const float parsed = std::strtof(text.c_str(), &end);
			//未完全消费（含尾随字符）或非有限值
			if (end == nullptr || *end != '\0' || !std::isfinite(parsed))
				return false;
			value = parsed;
			return true;
		}

		//读取「ID ＋ 余下整段作为路径」
		bool id_and_path_read(std::istringstream& stream, std::string& id_out, std::string& path_out)
		{
			//ID
			if (!(stream >> id_out))
				return false;
			//余下内容（路径可能含空格）
			std::string rest;
			std::getline(stream, rest);
			path_out = asset_detail::trimmed(rest);
			return !path_out.empty();
		}
	}

	//解析场景清单
	bool scene_manifest_parse(const std::string& path, Scene_Manifest& out)
	{
		//先清空输出
		out.meshes.clear();
		out.materials.clear();
		out.places.clear();
		//路径为空
		if (path.empty())
		{
			logger.warn("资产加载:: 场景清单路径为空");
			return false;
		}

		const std::filesystem::path file_path = asset_detail::path_from_utf8(path);
		std::error_code code;
		const uintmax_t byte_count = std::filesystem::file_size(file_path, code);
		//文件不存在 / 不可读
		if (code)
		{
			logger.warn("资产加载:: 无法读取场景清单 ({})", path);
			return false;
		}
		//空文件或超限
		if (byte_count == 0 || byte_count > MAX_FILE_BYTES)
		{
			logger.warn("资产加载:: 场景清单体量非法（{} 字节，上限 {}）", byte_count, MAX_FILE_BYTES);
			return false;
		}

		std::ifstream file(file_path, std::ios::binary);
		if (!file)
		{
			logger.warn("资产加载:: 场景清单打开失败 ({})", path);
			return false;
		}

		/*
		失败清空守卫
		契约要求「失败时输出为空」；解析途中任何提前返回都要把已收集的内容清掉，
		故用一个小守卫在成功时置位，否则析构时清空。
		*/
		struct 清空守卫
		{
			//被保护的清单
			Scene_Manifest& 清单;
			//是否成功（成功则不清空）
			bool 成功 = false;
			//析构：失败则清空
			~清空守卫(void)
			{
				if (!成功)
				{
					清单.meshes.clear();
					清单.materials.clear();
					清单.places.clear();
				}
			}
		} 守卫{ out };

		std::string line;
		while (std::getline(file, line))
		{
			asset_detail::trim_carriage_return(line);
			std::istringstream stream(line);
			std::string tag;
			//空行
			if (!(stream >> tag))
				continue;
			//注释行
			if (tag[0] == '#')
				continue;

			//网格引用
			if (tag == "mesh")
			{
				Scene_Mesh_Ref ref;
				if (!id_and_path_read(stream, ref.id, ref.path))
				{
					logger.warn("资产加载:: 清单 mesh 行缺字段 ({})", path);
					return false;
				}
				//ID 重复
				for (const Scene_Mesh_Ref& existing : out.meshes)
				{
					if (existing.id == ref.id)
					{
						logger.warn("资产加载:: 清单 mesh ID 重复（{}）", ref.id);
						return false;
					}
				}
				out.meshes.push_back(ref);
			}
			//材质库引用
			else if (tag == "mtl")
			{
				Scene_Mtl_Ref ref;
				if (!id_and_path_read(stream, ref.id, ref.path))
				{
					logger.warn("资产加载:: 清单 mtl 行缺字段 ({})", path);
					return false;
				}
				//ID 重复
				for (const Scene_Mtl_Ref& existing : out.materials)
				{
					if (existing.id == ref.id)
					{
						logger.warn("资产加载:: 清单 mtl ID 重复（{}）", ref.id);
						return false;
					}
				}
				out.materials.push_back(ref);
			}
			//摆放记录
			else if (tag == "place")
			{
				Scene_Place place;
				if (!(stream >> place.mesh_id))
				{
					logger.warn("资产加载:: 清单 place 行缺网格 ID ({})", path);
					return false;
				}
				/*
				材质列可省略：下一个标记若能解析为数字，则视为坐标起始，材质 ID 留空。
				*/
				std::string token;
				if (stream >> token)
				{
					float probe = 0.0f;
					if (float_parse(token, probe))
						place.position[0] = probe;
					else
					{
						place.material_id = token;
						if (!(stream >> token) || !float_parse(token, place.position[0]))
						{
							logger.warn("资产加载:: 清单 place 行坐标非法 ({})", path);
							return false;
						}
					}
				}
				//余下两轴位置与三轴缩放
				{
					float values[5] = {};
					for (std::size_t index = 0; index < 5; ++index)
					{
						if (!(stream >> token) || !float_parse(token, values[index]))
						{
							logger.warn("资产加载:: 清单 place 行坐标非法 ({})", path);
							return false;
						}
					}
					place.position[1] = values[0];
					place.position[2] = values[1];
					place.scale[0] = values[2];
					place.scale[1] = values[3];
					place.scale[2] = values[4];
				}
				//网格 ID 必须存在
				bool mesh_found = false;
				for (const Scene_Mesh_Ref& existing : out.meshes)
				{
					if (existing.id == place.mesh_id)
					{
						mesh_found = true;
						break;
					}
				}
				if (!mesh_found)
				{
					logger.warn("资产加载:: 清单 place 引用了未知网格（{}）", place.mesh_id);
					return false;
				}
				//材质 ID（若非空）必须存在
				if (!place.material_id.empty())
				{
					bool mtl_found = false;
					for (const Scene_Mtl_Ref& existing : out.materials)
					{
						if (existing.id == place.material_id)
						{
							mtl_found = true;
							break;
						}
					}
					if (!mtl_found)
					{
						logger.warn("资产加载:: 清单 place 引用了未知材质（{}）", place.material_id);
						return false;
					}
				}
				if (out.places.size() >= MAX_PLACES)
				{
					logger.warn("资产加载:: 清单摆放记录超限 ({})", path);
					return false;
				}
				out.places.push_back(place);
			}
			//未知指令
			else
			{
				logger.warn("资产加载:: 清单出现未知指令（{}）", tag);
				return false;
			}
		}

		//没有任何摆放记录
		if (out.places.empty())
		{
			logger.warn("资产加载:: 清单未包含任何 place ({})", path);
			return false;
		}
		//成功：守卫不再清空
		守卫.成功 = true;
		return true;
	}
}
