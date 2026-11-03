#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	/*
	顶点（位置 vec3 + 纹理坐标 vec2 + 法线 vec3；步长 32 字节，与着色器顶点输入一致）
	法线为第二十九轮新增：缺省为零向量（旧代码只写前两项，聚合初始化会补零），
	着色器据此判定「未提供法线」并按朝向相机处理，保证既有画面回退不变。
	*/
	struct Vertex
	{
		//位置（三维）
		float position[3];
		//纹理坐标（二维）
		float uv[2];
		//法线（三维；零向量 = 未提供）
		float normal[3] = { 0.0f, 0.0f, 0.0f };
	};

	/*
	网格分段（第四百一十四轮；物体多材质的子网格级描述）
	first_index 为段在网格索引区内的起始索引（相对索引区起点，uint32 元素计）；
	index_count 为该段索引数（须为 3 的倍数，即完整三角形）；
	material 为材质槽（由调用方解释；0 = 首个材质，越界时绘制入口回落 0）。
	段与段之间允许重叠或留空（不做强制分区校验），由调用方保证语义合理。
	*/
	struct Mesh_Section
	{
		//段起始索引（相对网格索引区起点）
		uint32_t first_index = 0;
		//段索引数（3 的倍数）
		uint32_t index_count = 0;
		//材质槽
		uint32_t material = 0;
	};

	//网格数据（创建入参）
	struct Mesh_Data
	{
		//顶点集合
		std::vector<Vertex> vertices;
		//索引集合
		std::vector<uint32_t> indices;
		/*
		注意（第四百一十六轮修正）：分段描述不再作为 `Mesh_Data` 的字段——
		本层与 EngineCore 都有 `engine::Mesh_Data`（同名不同定义），字段增删会改变结构体大小，
		在同时链接两层的 Test 里触发 ODR 冲突（曾导致 `Mesh_Loader_Test` 崩溃 / 卡住）。
		分段改由门面 `mesh_load_sections(handle, data, sections, count)` 单独传入。
		*/
	};

	//网格句柄（0 表示无效）
	using Mesh_Handle = uint64_t;
}
