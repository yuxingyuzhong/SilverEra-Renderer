#include "src/render/Platform/局部命名空间使用.h"
//获取日志系统
#include "Engine/EngineCore/src/tools/Logging/日志系统运行包.h"

namespace engine
{
	//组装实例所需扩展清单
	bool Platform_Context::instance_extensions_get(vector<const char*>& extensions_out) const
	{
		//清空输出（使调用可重复）
		extensions_out.clear();

		/*
		查询 SDL 要求的实例扩展
		SDL3 该接口不再带窗口参数、也没有 names 出参，而是直接返回扩展名数组；
		失败时返回空指针并用 SDL_GetError 给出原因（K14）。
		*/
		Uint32 count = 0;
		//一次调用同时取得扩展名数组与数量
		const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
		//若查询失败
		if (names == nullptr)
		{
			logger.error("平台上下文:: 查询实例扩展失败：{}", SDL_GetError());
			return false;
		}
		//若扩展数量为 0（异常：至少应含 VK_KHR_surface）
		if (count == 0)
		{
			logger.warn("平台上下文:: SDL 未返回任何实例扩展");
			return true;
		}

		//填充输出（保序拷贝 SDL 返回的扩展名）
		extensions_out.reserve(count);
		for (Uint32 i = 0; i < count; ++i)
			extensions_out.push_back(names[i]);

		return true;
	}

	//创建 VkSurfaceKHR
	bool Platform_Context::surface_build(VkInstance instance)
	{
		//若窗口尚未创建（无窗口无法建表面）
		if (window == nullptr)
		{
			logger.error("平台上下文:: 表面创建失败，窗口尚未创建");
			return false;
		}
		//若表面已存在
		if (surface != VK_NULL_HANDLE)
		{
			logger.warn("平台上下文:: 表面已存在，跳过重复创建");
			return true;
		}

		//记录实例句柄（销毁表面需要它）
		this->instance = instance;

		//创建表面（SDL3 保留 allocator 参数，传空用系统默认分配器；K15）
		if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
		{
			logger.error("平台上下文:: 表面创建失败：{}", SDL_GetError());
			//创建失败时清空实例记录，避免后续误销毁
			this->instance = VK_NULL_HANDLE;
			return false;
		}

		logger.info("平台上下文:: Vulkan 表面已创建");
		return true;
	}
}
