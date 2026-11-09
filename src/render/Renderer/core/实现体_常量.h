#pragma once
//管线描述集布局（提供 Pipeline_Context::POST_SET_PER_SLOT）
#include "Engine/EngineRenderer/src/render/Pipeline/管线.h"

//游戏引擎命名空间
namespace engine
{
	//后处理链级数上限（实现体各子结构共用）
	inline constexpr uint32_t POST_CHAIN_MAX_STAGES = 8;
	//呈现目标上限
	inline constexpr uint32_t PRESENT_TARGET_MAX = Pipeline_Context::POST_SET_PER_SLOT;
}