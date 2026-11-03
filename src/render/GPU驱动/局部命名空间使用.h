#pragma once
/*
局部命名空间使用（GPU驱动模块）
本头只被本模块 .cpp 包含：把实现文件里用到的引擎类型一次性引入，之后的代码不再写命名空间前缀。
*/
#include "Engine/EngineSystem/src/core/Renderer/src/render/GPU驱动/包围体.h"
#include "Engine/EngineSystem/src/core/Renderer/src/render/GPU驱动/剔除数据.h"

//游戏引擎命名空间
namespace engine
{
}

//实现文件内不加命名空间前缀
using engine::World_Bounds;
using engine::WORLD_BOUNDS_AABB_EXPAND;
using engine::WORLD_BOUNDS_SPHERE_SCALE;
using engine::WORLD_BOUNDS_SPHERE_EXPAND;
using engine::Frustum_Planes;
using engine::FRUSTUM_CULL_EPSILON;
