#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
/*
引擎唯一网格数据类型（2026-11-04 自本层下移至 EngineCore）
Vertex / Mesh_Section / Mesh_Data 现由 EngineCore 的 `tools/Mesh_Loader/网格加载器.h` 提供，
本层不再另立同名定义（此前两边同名不同定义，靠「不改布局 + 不混包含」规避 ODR 冲突）。
*/
#include "Engine/EngineCore/src/tools/Mesh_Loader/网格加载器.h"

//游戏引擎命名空间
namespace engine
{
	//网格句柄（0 表示无效）
	using Mesh_Handle = uint64_t;
}
