#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	//顶点（位置 vec3 + 纹理坐标 vec2；步长 20 字节，与着色器顶点输入一致）
	struct Vertex
	{
		//位置（三维）
		float position[3];
		//纹理坐标（二维）
		float uv[2];
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
