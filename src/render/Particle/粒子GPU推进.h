#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//渲染器门面（层内 GPU 粒子推进入口在渲染器公共头中声明）
#include "Engine/EngineRenderer/src/render/Renderer/渲染器.h"

/*
GPU 粒子推进（渲染层；2026-11-07 自 EngineSystem 归位）
粒子常驻推进的**模拟语义**（发射循环、环形写指针、活跃位图、死亡结算、紧凑索引）与计算原语
（compute_pipeline_load / compute_buffer_bind / compute_dispatch / compute_buffer_read 等）
同属渲染层，故整体归本层。入口在 `engine::particle_gpu` 命名空间，
声明见渲染器类型公共头，实现经六域公共接口协作。
*/
