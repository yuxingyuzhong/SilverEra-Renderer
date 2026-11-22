#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//骨架资产 / 姿态容器 / 中立求值（复用系统层骨骼系统）
#include "Engine/EngineSystem/src/world/skeleton/Skeleton_Asset/骨骼资产.h"
#include "Engine/EngineSystem/src/world/skeleton/Skeleton_Pose/骨骼姿态.h"

/*
渲染动画 · 骨架与姿态适配（方向十 · 阶段 B 第 8 轮；收敛轮 874）
骨架资产、姿态容器、世界矩阵与蒙皮矩阵求值一律复用 EngineSystem 骨骼系统：
  `Skeleton_Asset` / `Skeleton_Pose` / `skeleton_pose_world_update` / `skeleton_pose_skin_build`。
渲染侧只保留动画片段、采样、播放器与 GPU 蒙皮消费，不再自建骨架与姿态实现。
姿态世界矩阵位于演员根局部空间（根变换由调用方承担），与系统层口径一致。
*/
namespace engine
{
	//骨骼（系统层定义）
	using Animation_Bone = Skeleton_Bone;
	//骨架资产（系统层定义；加载与校验同上）
	using Animation_Skeleton = Skeleton_Asset;
	//姿态（系统层定义；世界矩阵经 skeleton_pose_world_update 求值）
	using Animation_Pose = Skeleton_Pose;
}
