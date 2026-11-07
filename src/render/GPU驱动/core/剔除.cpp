//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//局部命名空间使用
#include "Engine/EngineRenderer/src/render/GPU驱动/局部命名空间使用.h"

//游戏引擎命名空间
namespace engine
{
	/*
	提取视锥平面（第二百四十八轮）
	列主序矩阵按行取值：row(r) = (m[r], m[4+r], m[8+r], m[12+r])；
	Vulkan 裁剪空间 z ∈ [0, 1]，故近平面 = row3 + row2、远平面 = row3 − row2。
	*/
	bool frustum_planes_extract(Frustum_Planes& out, const float view_proj[16])
	{
		//入参为空
		if (view_proj == nullptr)
			return false;
		//矩阵含非有限值
		for (uint32_t index = 0; index < 16; ++index)
			if (!std::isfinite(view_proj[index]))
				return false;
		//按行取四分量
		float rows[4][4] = {};
		for (uint32_t row = 0; row < 4; ++row)
			for (uint32_t column = 0; column < 4; ++column)
				rows[row][column] = view_proj[column * 4 + row];
		//六平面（3 + 0 / 3 − 0 / 3 + 1 / 3 − 1 / 3 + 2 / 3 − 2）
		const float signs[6][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 1.0f, 1.0f },
			{ -1.0f, 1.0f }, { 1.0f, 2.0f }, { -1.0f, 2.0f } };
		for (uint32_t plane = 0; plane < 6; ++plane)
		{
			const uint32_t axis_row = static_cast<uint32_t>(signs[plane][1]);
			float values[4] = {};
			for (uint32_t column = 0; column < 4; ++column)
				values[column] = rows[3][column] + signs[plane][0] * rows[axis_row][column];
			//法线长度（归一化用）
			float length = std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
			//退化平面（长度为零）视为非法
			if (length <= 0.0f || !std::isfinite(length))
				return false;
			for (uint32_t column = 0; column < 4; ++column)
				out.planes[plane][column] = values[column] / length;
		}
		return true;
	}

	//球判可见（六个平面全部满足 dot(n, c) + d + r >= −ε）
	bool frustum_sphere_visible(const Frustum_Planes& planes, const float center[3], float radius)
	{
		//入参为空
		if (center == nullptr)
			return false;
		for (uint32_t plane = 0; plane < 6; ++plane)
		{
			const float* value = planes.planes[plane];
			const float distance = value[0] * center[0] + value[1] * center[1] + value[2] * center[2] + value[3];
			//完全在平面外侧
			if (distance + radius < -FRUSTUM_CULL_EPSILON)
				return false;
		}
		return true;
	}

	//AABB 判可见（正顶点法：每平面只取最靠内的角点）
	bool frustum_aabb_visible(const Frustum_Planes& planes, const float aabb_min[3], const float aabb_max[3])
	{
		//入参为空
		if (aabb_min == nullptr || aabb_max == nullptr)
			return false;
		for (uint32_t plane = 0; plane < 6; ++plane)
		{
			const float* value = planes.planes[plane];
			float point[3] = {};
			for (uint32_t axis = 0; axis < 3; ++axis)
				point[axis] = value[axis] >= 0.0f ? aabb_max[axis] : aabb_min[axis];
			const float distance = value[0] * point[0] + value[1] * point[1] + value[2] * point[2] + value[3];
			//该平面外侧的最内角点仍在外面 → 整个盒不可见
			if (distance < -FRUSTUM_CULL_EPSILON)
				return false;
		}
		return true;
	}

	//距离剔除（严格大于阈值才剔除；阈值 ≤ 0 不限）
	bool distance_cull(const float center[3], const float camera_position[3], float max_distance)
	{
		//不限距离或入参为空
		if (max_distance <= 0.0f || center == nullptr || camera_position == nullptr)
			return false;
		const float dx = center[0] - camera_position[0];
		const float dy = center[1] - camera_position[1];
		const float dz = center[2] - camera_position[2];
		const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		return distance > max_distance;
	}

	//背面剔除（点积严格小于 0 才剔除；正交仍可见）
	bool backface_cull(const float center[3], const float camera_position[3], const float forward[3])
	{
		//入参为空
		if (center == nullptr || camera_position == nullptr || forward == nullptr)
			return false;
		const float dx = center[0] - camera_position[0];
		const float dy = center[1] - camera_position[1];
		const float dz = center[2] - camera_position[2];
		const float dot_value = forward[0] * dx + forward[1] * dy + forward[2] * dz;
		return dot_value < 0.0f;
	}
}
