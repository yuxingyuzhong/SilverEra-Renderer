#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

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

	//网格数据（创建入参）
	struct Mesh_Data
	{
		//顶点集合
		std::vector<Vertex> vertices;
		//索引集合
		std::vector<uint32_t> indices;
	};

	//网格句柄（0 表示无效）
	using Mesh_Handle = uint64_t;
}
