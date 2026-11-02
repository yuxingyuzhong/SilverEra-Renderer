#include "src/render/Texture/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

//文件读取
#include <fstream>
/*
stb_image 实现（**全层唯一**定义 STB_IMAGE_IMPLEMENTATION 的编译单元）
第三方头以警告抑制包裹包含：本层要求「零本层新增警告」，第三方库自身的告警不属本层代码问题。
*/
#define STB_IMAGE_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable : 4244 4456 4457 4996)
#include "external/stb/stb_image.h"
#pragma warning(pop)

namespace
{
	//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；第四百二十八轮）
	std::filesystem::path path_from_utf8(const std::string& text)
	{
		std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
		return std::filesystem::path(u8);
	}
}

namespace engine
{
	/*
	从文件创建纹理
	流程：读入文件字节 → stb 解码为 RGBA8（强制 4 通道）→ 走既有内存像素路径创建纹理 →
	释放解码缓冲。任何一步失败都不留下半初始化记录（句柄保持 0）。
	*/
	bool Texture_Context::texture_create_from_file(uint64_t& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：模块必须已构建
		if (!valid())
		{
			logger.error("纹理:: 从文件创建纹理时模块尚未构建");
			return false;
		}

		//以二进制方式打开并定位到末尾取文件长度（路径按 UTF-8 解释，支持中文目录）
		std::ifstream file(path_from_utf8(path), std::ios::binary | std::ios::ate);
		//若文件不存在或不可读
		if (!file)
		{
			logger.error("纹理:: 打不开纹理文件 {}", path);
			return false;
		}
		//文件字节数
		std::streamsize byte_count = file.tellg();
		//若文件为空
		if (byte_count <= 0)
		{
			logger.error("纹理:: 纹理文件为空 {}", path);
			return false;
		}
		//回到文件开头并整块读入
		file.seekg(0, std::ios::beg);
		vector<uint8_t> bytes(static_cast<size_t>(byte_count));
		//若读取失败
		if (!file.read(reinterpret_cast<char*>(bytes.data()), byte_count))
		{
			logger.error("纹理:: 读取纹理文件失败 {}", path);
			return false;
		}

		//用 stb 解码（强制 4 通道，与内存像素路径的 RGBA8 口径一致）
		int width = 0;
		int height = 0;
		int source_channel_count = 0;
		stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
			&width, &height, &source_channel_count, 4);
		//若解码失败
		if (decoded == nullptr)
		{
			logger.error("纹理:: 解码失败（{}）", stbi_failure_reason());
			return false;
		}
		//解码尺寸非法（防御性校验；stb 成功时通常已保证为正）
		if (width <= 0 || height <= 0)
		{
			logger.error("纹理:: 解码尺寸非法（{} × {}）", width, height);
			stbi_image_free(decoded);
			return false;
		}

		//复用内存像素路径创建纹理（纹理创建会把像素拷入设备资源）
		bool created = texture_create_from_memory(handle_out, decoded,
			static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		//释放解码缓冲（此后与 stb 无关）
		stbi_image_free(decoded);

		//若创建失败
		if (!created)
		{
			logger.error("纹理:: 解码成功但从像素创建纹理失败 {}", path);
			handle_out = 0;
			return false;
		}
		logger.info("纹理:: 已从文件创建纹理 {}（{} × {}，源通道数 {}）", path, width, height, source_channel_count);
		return true;
	}
}
