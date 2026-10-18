#include "src/render/Shader/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//获取引擎环境（锚定可执行文件目录，处理中文路径）
#include "Engine/EngineCore/src/tools/Engine_Env/引擎环境.h"

namespace engine
{
	namespace
	{
		//文件系统路径 → UTF-8 字符串（避免系统本地编码造成中文路径乱码）
		string path_to_utf8(const std::filesystem::path& path)
		{
			std::u8string u8 = path.u8string();
			return string(reinterpret_cast<const char*>(u8.data()), u8.size());
		}
	}

	//析构
	Shader_Context::~Shader_Context(void)
	{
		//复位内部状态
		destroy();
	}

	//构建（记录逻辑设备）
	bool Shader_Context::build(VkDevice device)
	{
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("着色器:: 构建时逻辑设备为空");
			return false;
		}

		//记录逻辑设备
		this->device = device;
		return true;
	}

	//销毁（幂等）
	void Shader_Context::destroy(void)
	{
		//复位记录
		device = VK_NULL_HANDLE;
		directory.clear();
	}

	//是否已构建完成
	bool Shader_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE;
	}

	//设置着色器目录
	void Shader_Context::directory_set(const string& directory)
	{
		this->directory = directory;
	}

	//着色器目录解析
	string Shader_Context::directory_resolve(void) const
	{
		//若已显式指定目录则直接采用
		if (!directory.empty())
			return directory;
		//否则锚定可执行文件目录的 shaders 子目录（§6.5：不得使用相对 CWD 的路径）
		return path_to_utf8(Engine_Env::exe_dir_get() / "shaders");
	}
}
