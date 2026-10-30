//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//局部命名空间使用
#include "Application/Renderer/src/render/GPU驱动/局部命名空间使用.h"

//游戏引擎命名空间
namespace engine
{
	/*
	构造世界包围体（第二百四十七轮）
	球：变换局部中心，半径 = 局部半对角长 × 三列最大列长 × 保守倍率 ＋ 附加量；
	盒：逐角点变换（8 次）后重求 min/max，再逐轴外扩。
	*/
	bool world_bounds_build(World_Bounds& out, const float local_min[3], const float local_max[3], const float model[16])
	{
		//入参为空
		if (local_min == nullptr || local_max == nullptr || model == nullptr)
			return false;
		//局部盒下界大于上界
		for (uint32_t axis = 0; axis < 3; ++axis)
			if (local_min[axis] > local_max[axis])
				return false;
		//矩阵含非有限值（非数 / 无穷）
		for (uint32_t index = 0; index < 16; ++index)
			if (!std::isfinite(model[index]))
				return false;
		//局部中心与半对角长
		float center_local[3] = {};
		float half_extent[3] = {};
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			center_local[axis] = (local_min[axis] + local_max[axis]) * 0.5f;
			half_extent[axis] = (local_max[axis] - local_min[axis]) * 0.5f;
		}
		float half_diagonal = std::sqrt(half_extent[0] * half_extent[0] + half_extent[1] * half_extent[1]
			+ half_extent[2] * half_extent[2]);
		//三列最大列长（各轴缩放的最大倍数）
		float column_max = 0.0f;
		for (uint32_t column = 0; column < 3; ++column)
		{
			const float* base = model + column * 4;
			float length = std::sqrt(base[0] * base[0] + base[1] * base[1] + base[2] * base[2]);
			column_max = (std::max)(column_max, length);
		}
		//变换局部中心
		for (uint32_t axis = 0; axis < 3; ++axis)
			out.center[axis] = model[axis] * center_local[0] + model[4 + axis] * center_local[1]
				+ model[8 + axis] * center_local[2] + model[12 + axis];
		//保守球半径
		out.radius = half_diagonal * column_max * WORLD_BOUNDS_SPHERE_SCALE + WORLD_BOUNDS_SPHERE_EXPAND;
		//逐角点变换并重求 AABB
		for (uint32_t corner = 0; corner < 8; ++corner)
		{
			float point[3] = {
				(corner & 1u) != 0 ? local_max[0] : local_min[0],
				(corner & 2u) != 0 ? local_max[1] : local_min[1],
				(corner & 4u) != 0 ? local_max[2] : local_min[2],
			};
			float world[3] = {};
			for (uint32_t axis = 0; axis < 3; ++axis)
				world[axis] = model[axis] * point[0] + model[4 + axis] * point[1]
					+ model[8 + axis] * point[2] + model[12 + axis];
			//首角点初始化上下界
			if (corner == 0)
			{
				for (uint32_t axis = 0; axis < 3; ++axis)
				{
					out.aabb_min[axis] = world[axis];
					out.aabb_max[axis] = world[axis];
				}
				continue;
			}
			//逐轴扩张上下界
			for (uint32_t axis = 0; axis < 3; ++axis)
			{
				out.aabb_min[axis] = (std::min)(out.aabb_min[axis], world[axis]);
				out.aabb_max[axis] = (std::max)(out.aabb_max[axis], world[axis]);
			}
		}
		//逐轴外扩
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			out.aabb_min[axis] -= WORLD_BOUNDS_AABB_EXPAND;
			out.aabb_max[axis] += WORLD_BOUNDS_AABB_EXPAND;
		}
		return true;
	}
}
