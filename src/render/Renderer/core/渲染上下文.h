#pragma once
//公共语义类型
#include "Engine/EngineRenderer/src/render/Renderer/渲染器类型.h"
//核心模块完整类型（仅层内使用）
#include "Engine/EngineRenderer/src/render/Platform/平台上下文.h"
#include "Engine/EngineRenderer/src/render/Device/设备上下文.h"
#include "Engine/EngineRenderer/src/render/Memory/内存上下文.h"
#include "Engine/EngineRenderer/src/render/Swapchain/交换链.h"
#include "Engine/EngineRenderer/src/render/Frame/帧上下文.h"

//游戏引擎命名空间
namespace engine
{
	//核心模块窄上下文（由设备域独占持有，其余域经公开接口协作）
	struct Render_Core_Context
	{
		Platform_Context& platform;
		Device_Context& device;
		Memory_Context& memory;
		Swapchain_Context& swapchain;
		Frame_Context& frame;
		const Renderer_Config& config;
	};
}
