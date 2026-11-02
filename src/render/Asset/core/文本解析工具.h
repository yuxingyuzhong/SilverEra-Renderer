#pragma once
//模块内部解析工具（OBJ / MTL 两个实现共用；仅头文件内联，不引出符号）
#include "Application/Renderer/common/前置头文件包含.h"

namespace engine
{
	namespace asset_detail
	{
		//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；避免窄路径二次转换）
		inline std::filesystem::path path_from_utf8(const std::string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}

		//去掉首尾空白（材质名 / 贴图路径可能带空格）
		inline std::string trimmed(const std::string& text)
		{
			const std::size_t begin = text.find_first_not_of(" \t\r\n");
			//全空白
			if (begin == std::string::npos)
				return std::string();
			const std::size_t end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}

		//去掉行尾回车（Windows 文本）
		inline void trim_carriage_return(std::string& line)
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
		}
	}
}
