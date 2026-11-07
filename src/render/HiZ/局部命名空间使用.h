#pragma once
/*
局部命名空间使用（HiZ 模块）
本头只被本模块 .cpp 包含：把实现文件里用到的引擎类型一次性引入，之后的代码不再写命名空间前缀。
*/
#include "Engine/EngineRenderer/src/render/HiZ/HiZ.h"

// ---------- 基本类型别名 ----------
using std::vector;

//游戏引擎命名空间
namespace engine
{
}

//实现文件内不加命名空间前缀
using engine::HiZ_Context;
using engine::HIZ_LEVEL_MAX;
using engine::HIZ_PYRAMID_FORMAT;
