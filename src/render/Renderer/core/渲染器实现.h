#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
各模块对外头
本头属本层「实现细节」：实现体持有各模块实例，故需其完整类型；
不进公共头，故可以合法暴露 Vulkan / SDL3 类型（层外仍看不到）。
*/
#include "Application/Renderer/src/render/Platform/平台上下文.h"
#include "Application/Renderer/src/render/Device/设备上下文.h"
#include "Application/Renderer/src/render/Memory/内存上下文.h"
#include "Application/Renderer/src/render/Swapchain/交换链.h"
#include "Application/Renderer/src/render/Frame/帧上下文.h"
//门面声明（Renderer::Impl 为其私有嵌套类）
#include "Application/Renderer/src/render/Renderer/渲染器.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器实现体
	持有五个模块的实例并按依赖顺序编排创建与销毁；本类只被 渲染器.cpp 使用。
	*/
	class Renderer::Impl
	{
	public:
		//配置（init 成功后生效）
		Renderer_Config config;
		//平台模块
		Platform_Context platform;
		//设备模块
		Device_Context device;
		//内存模块
		Memory_Context memory;
		//交换链模块
		Swapchain_Context swapchain;
		//帧模块
		Frame_Context frame;
		//是否已初始化
		bool initialized = false;
		//已呈现帧数
		uint64_t frame_count = 0;
	};
}
