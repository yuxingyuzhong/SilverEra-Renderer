#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
实例包围体（Vulkan-free；第二百四十七轮）
方向一规划 §2.3 的落地实现：把「网格局部 AABB ＋ 模型矩阵」换算成世界空间包围球与世界空间 AABB。
关键口径：局部 AABB 经非轴对齐变换后不再是 AABB，**不得**直接把局部 min/max 乘矩阵；
必须先变换 8 个角点再重求 min/max（结果比原盒子大，属正确的保守包围）。
*/

//游戏引擎命名空间
namespace engine
{
	/*
	世界空间包围体
	center / radius：包围球（视锥、距离、Hi-Z 遮挡前的粗判都用它）；
	aabb_min / aabb_max：世界轴对齐盒（Hi-Z 需要矩形投影，故必须另存）。
	两项均已按保守口径扩张，可直接用于剔除而不必再自行加容差。
	*/
	struct World_Bounds
	{
		//包围球中心（世界坐标）
		float center[3] = { 0.0f, 0.0f, 0.0f };
		//包围球半径（含保守扩张）
		float radius = 0.0f;
		//世界 AABB 下界（含每轴外扩）
		float aabb_min[3] = { 0.0f, 0.0f, 0.0f };
		//世界 AABB 上界（含每轴外扩）
		float aabb_max[3] = { 0.0f, 0.0f, 0.0f };
	};

	//世界 AABB 每轴外扩量（世界单位）
	constexpr float WORLD_BOUNDS_AABB_EXPAND = 0.002f;
	//包围球半径倍率（保守扩张）
	constexpr float WORLD_BOUNDS_SPHERE_SCALE = 1.001f;
	//包围球半径附加量（世界单位）
	constexpr float WORLD_BOUNDS_SPHERE_EXPAND = 0.002f;

	/*
	构造世界包围体
	local_min / local_max：网格局部 AABB（各 3 分量，须 min ≤ max）；
	model：列主序 4×4 模型矩阵（与实例缓冲一致，平移在 12..14）；
	返回假表示入参非法（空指针、局部下界大于上界、矩阵非有限值）。
	*/
	bool world_bounds_build(World_Bounds& out, const float local_min[3], const float local_max[3], const float model[16]);
}
