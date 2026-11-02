#include "src/render/Asset/局部命名空间使用.h"
//模块内共享解析工具（路径转换 / 去空白）
#include "src/render/Asset/core/文本解析工具.h"

//文件读取
#include <fstream>
#include <sstream>

namespace engine
{
	namespace
	{
		//MTL 文件体量上限（16 MiB；材质库远小于网格）
		constexpr uint64_t MAX_FILE_BYTES = 16ull * 1024 * 1024;
		//材质数量上限
		constexpr uint32_t MAX_MATERIALS = 4096u;

		//把数值钳到 [0, 1]（颜色 / 不透明度）
		float clamp_unit(float value)
		{
			if (!std::isfinite(value))
				return 0.0f;
			if (value < 0.0f)
				return 0.0f;
			if (value > 1.0f)
				return 1.0f;
			return value;
		}

		//读三通道颜色（缺分量按 0；越界钳到 [0,1]）
		void color_read(std::istringstream& stream, float (&color)[3])
		{
			float r = 0.0f;
			float g = 0.0f;
			float b = 0.0f;
			stream >> r;
			stream >> g;
			stream >> b;
			color[0] = clamp_unit(r);
			color[1] = clamp_unit(g);
			color[2] = clamp_unit(b);
		}

		/*
		贴图路径提取
		MTL 允许「map_Bump -bm 1.0 tex.png」这类带选项写法，此处取最后一个非选项标记作为路径。
		*/
		std::string texture_path_read(std::istringstream& stream)
		{
			std::string token;
			std::string result;
			while (stream >> token)
			{
				//选项标记（-bm / -s / -o 等）跳过
				if (!token.empty() && token[0] == '-')
					continue;
				result = token;
			}
			return result;
		}
	}

	//从 MTL 文件读取材质库
	bool mtl_materials_load(const std::string& path, std::vector<Mtl_Material>& out)
	{
		//先清空输出
		out.clear();
		//路径为空
		if (path.empty())
		{
			logger.warn("资产加载:: MTL 路径为空");
			return false;
		}

		const std::filesystem::path file_path = asset_detail::path_from_utf8(path);
		std::error_code code;
		const uintmax_t byte_count = std::filesystem::file_size(file_path, code);
		//文件不存在 / 不可读
		if (code)
		{
			logger.warn("资产加载:: 无法读取 MTL 文件 ({})", path);
			return false;
		}
		//空文件或超限
		if (byte_count == 0 || byte_count > MAX_FILE_BYTES)
		{
			logger.warn("资产加载:: MTL 体量非法（{} 字节，上限 {}）", byte_count, MAX_FILE_BYTES);
			return false;
		}

		std::ifstream file(file_path, std::ios::binary);
		if (!file)
		{
			logger.warn("资产加载:: MTL 打开失败 ({})", path);
			return false;
		}

		//当前正在填写的材质与「是否已出现 newmtl」
		Mtl_Material material;
		bool in_material = false;

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

			//新材质
			if (tag == "newmtl")
			{
				//先把上一项收进输出
				if (in_material)
					out.push_back(material);
				if (out.size() >= MAX_MATERIALS)
				{
					logger.warn("资产加载:: MTL 材质数量超限 ({})", path);
					out.clear();
					return false;
				}
				//重置为新材质（默认值来自 Mtl_Material 的成员初始化）
				material = Mtl_Material{};
				std::string name;
				std::getline(stream, name);
				material.name = asset_detail::trimmed(name);
				in_material = true;
				continue;
			}
			//未出现 newmtl：其余行无效
			if (!in_material)
				continue;

			//漫反射颜色
			if (tag == "Kd")
				color_read(stream, material.diffuse);
			//镜面颜色
			else if (tag == "Ks")
				color_read(stream, material.specular);
			//自发光颜色
			else if (tag == "Ke")
				color_read(stream, material.emissive);
			//镜面指数（0..1000）
			else if (tag == "Ns")
			{
				float value = 0.0f;
				stream >> value;
				//非有限值按 0
				if (std::isfinite(value))
				{
					if (value < 0.0f)
						value = 0.0f;
					if (value > 1000.0f)
						value = 1000.0f;
					material.shininess = value;
				}
			}
			//不透明度
			else if (tag == "d")
			{
				float value = 0.0f;
				stream >> value;
				material.opacity = clamp_unit(value);
			}
			//透明度（Tr = 1 − d）
			else if (tag == "Tr")
			{
				float value = 0.0f;
				stream >> value;
				material.opacity = 1.0f - clamp_unit(value);
			}
			//漫反射贴图
			else if (tag == "map_Kd")
				material.diffuse_map = texture_path_read(stream);
			//法线 / 凹凸贴图（map_Bump / bump / norm / map_bump 视同）
			else if (tag == "map_Bump" || tag == "map_bump" || tag == "bump" || tag == "norm")
				material.normal_map = texture_path_read(stream);
			//其余行（Ka / Ni / illum / 参数等）忽略
		}

		//收尾最后一项
		if (in_material)
			out.push_back(material);

		//一个材质都没读到
		if (out.empty())
		{
			logger.warn("资产加载:: MTL 未包含任何 newmtl ({})", path);
			return false;
		}
		return true;
	}
}
