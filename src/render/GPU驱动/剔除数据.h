#pragma once
#include <cstdint>
/*
视锥剔除数据（Vulkan-free；第二百四十八轮）
从「视图 × 投影」矩阵提取 6 个归一化平面，并提供两种 CPU 参考判定：
球判定（GPU 同口径，保守）与 AABB 判定（更紧，仅作 `C ⊆ G` 的参照）。
*/

//游戏引擎命名空间
namespace engine
{
	/*
	视锥平面组
	planes[i] = (n.x, n.y, n.z, d)，判可见口径为 dot(n, c) + d + r >= -1e-4（r 为包围球半径，AABB 判定时取 0）。
	顺序固定：左 / 右 / 下 / 上 / 近 / 远。
	*/
	struct Frustum_Planes
	{
		float planes[6][4] = {};
	};

	//判定容差（世界单位；吸收浮点误差，保证保守）
	constexpr float FRUSTUM_CULL_EPSILON = 1e-4f;

	/*
	提取视锥平面
	view_proj：列主序 4×4（与相机一致）；逐平面归一化后写出。
	矩阵含非有限值或空指针时返回假。
	*/
	bool frustum_planes_extract(Frustum_Planes& out, const float view_proj[16]);

	//球判可见（六个平面全部满足；与 GPU 着色器同一公式）
	bool frustum_sphere_visible(const Frustum_Planes& planes, const float center[3], float radius);

	//AABB 判可见（正顶点法；比球更紧，用于验证 GPU 不多剔）
	bool frustum_aabb_visible(const Frustum_Planes& planes, const float aabb_min[3], const float aabb_max[3]);

	/*
	距离剔除（第二百五十轮）
	中心与相机距离**严格大于** max_distance 才剔除；max_distance ≤ 0 表示不限距离。
	边界口径：恰好等于阈值仍可见（与 GPU 着色器同一比较方向）。
	*/
	bool distance_cull(const float center[3], const float camera_position[3], float max_distance);

	/*
	背面（半球）剔除（第二百五十轮）
	实例中心落在相机后方（forward 与「中心 − 相机」夹角 > 90°）才剔除；forward 须为单位向量。
	边界口径：正交（点积恰为 0）仍可见。
	*/
	bool backface_cull(const float center[3], const float camera_position[3], const float forward[3]);
}
