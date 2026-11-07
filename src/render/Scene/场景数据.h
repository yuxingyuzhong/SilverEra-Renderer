#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
场景语义类型（2026-11-07 下沉至 EngineSystem）
Camera_Data / Light_Data / Fog_Data / Point_Light_State / Scene_Data 现由系统层
`Engine/EngineSystem/src/scene/Level/场景数据.h` 提供 —— 场景描述是系统层产出的中立数据契约，
渲染层单向消费它。本头保留为转发头，使渲染层内部的既有包含路径（渲染器.h、局部命名空间使用.h）
与上层调用方无需改动，且「类型唯一定义」仍只有系统层一处。
*/
#include "Engine/EngineSystem/src/scene/Level/场景数据.h"
