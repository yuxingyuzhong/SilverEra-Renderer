#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>

//游戏引擎命名空间
namespace engine
{
	/*
	着色器上下文
	把 .spv 字节码加载为 VkShaderModule；模块生命周期由调用方显式管理（创建与销毁同属本类）。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	*/
	class Shader_Context
	{
	public:
		//构造（不加载任何着色器）
		Shader_Context(void) = default;
		//析构（内部调用 destroy）
		~Shader_Context(void);
		Shader_Context(const Shader_Context&) = delete;
		Shader_Context& operator=(const Shader_Context&) = delete;

		//构建（记录逻辑设备；不加载任何着色器）
		bool build(VkDevice device);
		//销毁（幂等；仅复位状态，着色器模块须由调用方先行销毁）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		//设置着色器目录（空则用「exe 目录 / shaders」）
		void directory_set(const std::string& directory);
		//读取着色器文件字节（file_name 如 "场景.vert.spv"）
		bool file_bytes_read(std::vector<char>& bytes_out, const std::string& file_name) const;
		//加载着色器模块（内部经 file_bytes_read；失败返回假）
		bool module_load(VkShaderModule& module_out, const std::string& file_name) const;
		//销毁着色器模块
		void module_destroy(VkShaderModule module) const;
	private:
		//着色器目录解析（空 → Engine_Env 的 exe 目录 / shaders；实现见 core/着色器.cpp）
		std::string directory_resolve(void) const;
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//着色器目录（空表未指定）
		std::string directory;
	};
}
