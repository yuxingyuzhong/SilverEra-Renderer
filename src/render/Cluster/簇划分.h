#pragma once
#include <cstdint>
/*
分簇数据（Vulkan-free；第二百六十七轮）
阶段 D 的 CPU 参考：把裁剪空间（NDC）按网格切成 `Cluster_Grid` 个簇，
并给出「光源记录 → 逐簇计数」的保守覆盖实现，供第二百六十八轮的 GPU 计数对数。
本头不依赖 Vulkan，也不依赖相机模块。
*/
#include "Application/Renderer/src/render/Scene/光源数据.h"

//游戏引擎命名空间
namespace engine
{
	//默认分簇网格（x / y / z；合计 3456 簇，与规划 §2.10 一致）
	constexpr uint32_t CLUSTER_GRID_X_DEFAULT = 16;
	constexpr uint32_t CLUSTER_GRID_Y_DEFAULT = 9;
	constexpr uint32_t CLUSTER_GRID_Z_DEFAULT = 24;
	//单轴网格上限与总簇数上限
	constexpr uint32_t CLUSTER_GRID_AXIS_MAX = 64;
	constexpr uint32_t CLUSTER_TOTAL_MAX = 4096;

	/*
	分簇网格
	x / y 为屏幕（NDC）方向的分段数，z 为视图深度（近远平面间线性）的分段数。
	簇线性下标为 (z * y + y) * x + x，即 x 变化最快。
	*/
	struct Cluster_Grid
	{
		uint32_t x = CLUSTER_GRID_X_DEFAULT;
		uint32_t y = CLUSTER_GRID_Y_DEFAULT;
		uint32_t z = CLUSTER_GRID_Z_DEFAULT;
	};

	//网格总簇数（任一轴为 0 时返回 0）
	constexpr uint32_t cluster_grid_clusters(Cluster_Grid grid)
	{
		if (grid.x == 0 || grid.y == 0 || grid.z == 0)
			return 0;
		return grid.x * grid.y * grid.z;
	}

	/*
	网格参数钳制
	各轴钳到 1..64；总簇数超过 4096 时逐步收缩 z 轴（至少保留 1 层）。
	*/
	constexpr Cluster_Grid cluster_grid_clamp(Cluster_Grid grid)
	{
		//逐轴钳制
		Cluster_Grid out;
		out.x = grid.x < 1 ? 1 : (grid.x > CLUSTER_GRID_AXIS_MAX ? CLUSTER_GRID_AXIS_MAX : grid.x);
		out.y = grid.y < 1 ? 1 : (grid.y > CLUSTER_GRID_AXIS_MAX ? CLUSTER_GRID_AXIS_MAX : grid.y);
		out.z = grid.z < 1 ? 1 : (grid.z > CLUSTER_GRID_AXIS_MAX ? CLUSTER_GRID_AXIS_MAX : grid.z);
		//总簇数超限时收缩 z 轴
		while (out.x * out.y * out.z > CLUSTER_TOTAL_MAX && out.z > 1)
			--out.z;
		return out;
	}

	/*
	分簇相机输入（CPU 参考）
	view_proj 为列主序「视图 × 投影」；camera_position 为世界位置；
	fov 为垂直视场角（弧度）；aspect 为宽高比；near / far 为视图空间深度范围。
	*/
	struct Cluster_Input
	{
		//视图 × 投影（列主序 16 浮点）
		float view_proj[16] = {};
		//相机世界位置
		float camera_position[3] = { 0.0f, 0.0f, 0.0f };
		//垂直视场角（弧度）
		float fov = 1.0f;
		//宽高比
		float aspect = 16.0f / 9.0f;
		//近平面（视图空间深度）
		float near_plane = 0.1f;
		//远平面（视图空间深度）
		float far_plane = 100.0f;
	};

	/*
	逐簇计数（CPU 参考；第二百六十七轮）
	counts_out 长度须 ≥ 总簇数，按线性下标写入；每项为覆盖该簇的光源数量。
	口径：只统计 `flags` 的 bit0 为真的记录；方向光照亮全部簇；
	点光 / 聚光按其包围球的屏幕矩形与视图深度区间**保守**覆盖（宁多不少）。
	返回写入的簇数；容量不足、网格非法或光源为空返回 0（不写任何项）。
	*/
	uint32_t cluster_counts_reference(uint32_t* counts_out, uint32_t capacity, Cluster_Grid grid,
		const Light_Record* lights, uint32_t light_count, const Cluster_Input& input);

	//块内扫描的本地工作组大小（第二百六十九轮；与 GPU 着色器 local_size 一致）
	constexpr uint32_t CLUSTER_SCAN_BLOCK = 256;

	/*
	块内前缀和（CPU 参考；第二百六十九轮）
	按 `CLUSTER_SCAN_BLOCK` 分块做**包含式**前缀和：prefix_out[i] = 该块内 counts[块首..i] 之和；
	同时写出每块总和（block_totals_out）。返回块数；入参非法或 block_capacity 不足返回 0。
	*/
	uint32_t cluster_prefix_block_scan(uint32_t* prefix_out, uint32_t* block_totals_out, uint32_t block_capacity,
		const uint32_t* counts, uint32_t count);

	/*
	全量前缀和（CPU 参考；第二百七十轮）
	对 counts 做**包含式**全量前缀和（等价于「块内前缀 ＋ 块间偏移」两步的最终结果）。
	返回总元素数；入参非法返回 0。
	*/
	uint32_t cluster_prefix_scan(uint32_t* prefix_out, const uint32_t* counts, uint32_t count);

	/*
	簇列表排他偏移（第二百七十一轮）
	offsets[c] = counts[0..c-1] 之和（即该簇在光源列表中的写入起点）。
	返回总条目数（counts 全量之和）；入参非法返回 0。
	*/
	uint32_t cluster_list_offsets(const uint32_t* counts, uint32_t cluster_total, uint32_t* offsets_out);

	/*
	簇光源列表（CPU 参考；第二百七十一轮）
	offsets_io 为每簇写入游标（调用方传入排他偏移，函数内推进到该簇末尾）；
	按光源顺序把覆盖该簇的光源索引写入 list_out。返回写入的总条目数；
	容量不足或非法入参返回 0（不写任何项）。
	*/
	uint32_t cluster_list_reference(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_io,
		const uint32_t* counts, uint32_t cluster_total, Cluster_Grid grid,
		const Light_Record* lights, uint32_t light_count, const Cluster_Input& input);
}
