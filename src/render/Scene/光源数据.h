#pragma once
#include <cstdint>
/*
光源数据（Vulkan-free；第二百六十五轮）
阶段 D（Cluster 光照）的数据底座：把主方向光 / 补光 / 逐盏点光与聚光统一为 `Light_Record`，
供 CPU 快照、SSBO 上传与（后续）计算着色器分簇使用。本头不依赖 Vulkan。

字段按 std140 / std430 兼容的 4 分量排布（共 80 字节）：
position = (x, y, z, radius)；color = (r, g, b, intensity)；
direction = (x, y, z, 0)；cone = (cos_inner, cos_outer, attenuation, specular)；
尾随 shininess / type / flags / 保留。
*/

//游戏引擎命名空间
namespace engine
{
	//光源类型（与着色器分支编号一致）
	enum class Light_Type : uint32_t
	{
		//主方向光 / 补光
		Directional = 0,
		//点光源
		Point = 1,
		//聚光灯
		Spot = 2,
	};

	//光源记录容量上限（阶段 D 的 SSBO 默认容量，见规划 §2.6）
	constexpr uint32_t LIGHT_RECORD_CAPACITY = 1024;
	//旧前向路径（无 Cluster）支持的光源上限（第六百九十九轮由 8 扩为 16）
	constexpr uint32_t LIGHT_LEGACY_MAX = 16;
	//单个光源记录的字节数
	constexpr uint32_t LIGHT_RECORD_BYTES = 80;

	/*
	光源记录
	pos_radius：世界位置 + 影响半径（方向光半径无意义，取 0）；
	color_intensity：rgb + 强度；
	direction：光传播方向（点光源为 0）；
cone：内 / 外锥余弦、衰减模式、逐盏高光缩放；
shininess：逐盏光泽度（0 = 跟随全局）；type / flags 供着色器分支与降级标记。
	*/
	struct Light_Record
	{
		//位置 ＋ 半径
		float pos_radius[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		//颜色 ＋ 强度
		float color_intensity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		//方向（点光源填 0）
		float direction[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		//锥参数：cos_inner / cos_outer / attenuation / specular
		float cone[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		//逐盏光泽度（0 = 跟随全局）
		float shininess = 0.0f;
		//类型（Light_Type）
		uint32_t type = 0;
		//标志位（bit0 = 该记录有效）
		uint32_t flags = 0;
		//保留（对齐 80 字节；第四百八十八轮起点光 / 聚光记录写入前向路径槽位）
		float reserved = 0.0f;
	};

	/*
	有效光源数（规划 §2.6）
	Cluster 关闭时旧前向路径只循环前 16 盏（超出部分须由调用方告警截断，不静默）；
	Cluster 开启时返回全量（上限由 LIGHT_RECORD_CAPACITY 约束）。
	*/
	constexpr uint32_t light_effective_count(uint32_t light_count, bool cluster_enabled)
	{
		//关闭 Cluster：截断到旧路径上限
		if (!cluster_enabled)
			return light_count < LIGHT_LEGACY_MAX ? light_count : LIGHT_LEGACY_MAX;
		//开启 Cluster：全量（容量由记录数组上限约束）
		return light_count < LIGHT_RECORD_CAPACITY ? light_count : LIGHT_RECORD_CAPACITY;
	}
}
