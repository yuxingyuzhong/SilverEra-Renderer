#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	namespace
	{
		//UTF-8 字符串 → 文件系统路径（处理中文路径）
		std::filesystem::path path_from_utf8(const string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}
	}

	//读取着色器文件字节
	bool Shader_Context::file_bytes_read(vector<char>& bytes_out, const string& file_name) const
	{
		//清空输出
		bytes_out.clear();
		//拼接完整路径（目录已处理为 UTF-8，文件名为 UTF-8）
		std::filesystem::path full_path = path_from_utf8(directory_resolve()) / path_from_utf8(file_name);
		//以二进制方式打开并定位到文件尾以取大小
		std::ifstream stream(full_path, std::ios::ate | std::ios::binary);
		//若打开失败
		if (!stream.is_open())
		{
			logger.error("着色器:: 无法打开着色器文件 {}", file_name);
			return false;
		}

		//文件字节数
		std::streamsize size = stream.tellg();
		//若文件为空或读取位置无效
		if (size <= 0)
		{
			logger.error("着色器:: 着色器文件为空 {}", file_name);
			return false;
		}

		//读入字节
		bytes_out.resize(static_cast<std::size_t>(size));
		stream.seekg(0);
		stream.read(bytes_out.data(), size);
		//若实际读取字节数不足（短读或流已处于失败态）
		if (stream.gcount() != size)
		{
			logger.error("着色器:: 着色器文件读取不完整 {}", file_name);
			bytes_out.clear();
			stream.close();
			return false;
		}
		stream.close();
		return true;
	}

	//加载着色器模块
	bool Shader_Context::module_load(VkShaderModule& module_out, const string& file_name) const
	{
		//默认无有效模块
		module_out = VK_NULL_HANDLE;
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("着色器:: 尚未构建，无法加载着色器模块 {}", file_name);
			return false;
		}

		//读取 SPIR-V 字节码
		vector<char> bytes;
		if (!file_bytes_read(bytes, file_name))
			return false;

		//若字节长度非 4 字节倍数（SPIR-V 以 32 位字为单位）
		if (bytes.size() % 4 != 0)
		{
			logger.error("着色器:: 字节码长度非 4 字节倍数 {}", file_name);
			return false;
		}
		//取首字作为魔数判据
		uint32_t magic = 0;
		memcpy(&magic, bytes.data(), sizeof(uint32_t));
		//若魔数不匹配（非 SPIR-V 字节码）
		if (magic != 0x07230203u)
		{
			logger.error("着色器:: 魔数不匹配，非 SPIR-V 字节码（魔数 {}）{}", magic, file_name);
			return false;
		}

		//着色器模块创建信息（pCode 须 4 字节对齐；vector 数据天然满足）
		VkShaderModuleCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = bytes.size();
		create_info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
		//若模块创建失败
		if (!vk_result_check(vkCreateShaderModule(device, &create_info, nullptr, &module_out), "vkCreateShaderModule"))
		{
			module_out = VK_NULL_HANDLE;
			return false;
		}

		logger.info("着色器:: 着色器模块已加载（{}）", file_name);
		return true;
	}

	//销毁着色器模块
	void Shader_Context::module_destroy(VkShaderModule module) const
	{
		//句柄或逻辑设备不全即视为无需销毁（销毁幂等）
		if (module == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
			return;

		//归还着色器模块
		vkDestroyShaderModule(device, module, nullptr);
	}
}
