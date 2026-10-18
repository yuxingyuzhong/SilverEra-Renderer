#include "src/render/Mesh/局部命名空间使用.h"

namespace engine
{
	/*
	生成三角形
	单个等腰三角形，位于 XY 平面（z = 0），逆时针绕序（从 +Z 方向观察）。
	纹理坐标覆盖整块贴图。
	*/
	Mesh_Data Mesh_Context::triangle_generate(void)
	{
		Mesh_Data data;
		//顶点（左上 / 右上 / 下方中点）
		data.vertices = {
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } },
			{ { 0.0f, 0.5f, 0.0f }, { 0.5f, 1.0f } },
		};
		//索引（逆时针）
		data.indices = { 0, 1, 2 };
		return data;
	}

	/*
	生成轴对齐立方体
	边长 2*half_extent，24 顶点 / 36 索引；每面 4 顶点，顶点按「从外部看逆时针」排列，
	与管线 frontFace = COUNTER_CLOCKWISE / cullMode = BACK 自洽。
	*/
	Mesh_Data Mesh_Context::cube_generate(float half_extent)
	{
		//半边长
		const float h = half_extent;
		Mesh_Data data;
		//24 顶点：逐面给出 4 个角点（位置 + 纹理坐标）
		data.vertices = {
			// +X 面
			{ { +h, -h, -h }, { 0.0f, 0.0f } },
			{ { +h, +h, -h }, { 0.0f, 1.0f } },
			{ { +h, +h, +h }, { 1.0f, 1.0f } },
			{ { +h, -h, +h }, { 1.0f, 0.0f } },
			// -X 面
			{ { -h, -h, +h }, { 0.0f, 0.0f } },
			{ { -h, +h, +h }, { 0.0f, 1.0f } },
			{ { -h, +h, -h }, { 1.0f, 1.0f } },
			{ { -h, -h, -h }, { 1.0f, 0.0f } },
			// +Y 面
			{ { -h, +h, +h }, { 0.0f, 0.0f } },
			{ { +h, +h, +h }, { 0.0f, 1.0f } },
			{ { +h, +h, -h }, { 1.0f, 1.0f } },
			{ { -h, +h, -h }, { 1.0f, 0.0f } },
			// -Y 面
			{ { -h, -h, -h }, { 0.0f, 0.0f } },
			{ { +h, -h, -h }, { 0.0f, 1.0f } },
			{ { +h, -h, +h }, { 1.0f, 1.0f } },
			{ { -h, -h, +h }, { 1.0f, 0.0f } },
			// +Z 面
			{ { -h, -h, +h }, { 0.0f, 0.0f } },
			{ { +h, -h, +h }, { 0.0f, 1.0f } },
			{ { +h, +h, +h }, { 1.0f, 1.0f } },
			{ { -h, +h, +h }, { 1.0f, 0.0f } },
			// -Z 面
			{ { +h, -h, -h }, { 0.0f, 0.0f } },
			{ { -h, -h, -h }, { 0.0f, 1.0f } },
			{ { -h, +h, -h }, { 1.0f, 1.0f } },
			{ { +h, +h, -h }, { 1.0f, 0.0f } },
		};
		//36 索引：每面两个三角形（0,1,2 与 2,3,0），逐面偏移 4
		data.indices = {
			0, 1, 2, 2, 3, 0,
			4, 5, 6, 6, 7, 4,
			8, 9, 10, 10, 11, 8,
			12, 13, 14, 14, 15, 12,
			16, 17, 18, 18, 19, 16,
			20, 21, 22, 22, 23, 20,
		};
		return data;
	}
}
