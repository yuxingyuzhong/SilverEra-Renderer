#include "src/render/Mesh/局部命名空间使用.h"

namespace engine
{
	/*
	生成三角形
	单个等腰三角形，位于 XY 平面（z = 0），逆时针绕序（从 +Z 方向观察）。
	纹理坐标覆盖整块贴图；法线恒为 +Z（第二十九轮起参与方向光）。
	*/
	Mesh_Data Mesh_Context::triangle_generate(void)
	{
		Mesh_Data data;
		//顶点（左上 / 右上 / 下方中点），法线朝 +Z
		data.vertices = {
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.0f, 0.5f, 0.0f }, { 0.5f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		};
		//索引（逆时针）
		data.indices = { 0, 1, 2 };
		return data;
	}

	/*
	生成轴对齐立方体
	边长 2*half_extent，24 顶点 / 36 索引；每面 4 顶点，顶点按「从外部看逆时针」排列，
	与管线 frontFace = COUNTER_CLOCKWISE / cullMode = BACK 自洽。
	法线逐面写入（第二十九轮起参与方向光）：每面 4 顶点共用该面朝外法线，棱边不共享平滑。
	*/
	Mesh_Data Mesh_Context::cube_generate(float half_extent)
	{
		//半边长
		const float h = half_extent;
		Mesh_Data data;
		//24 顶点：逐面给出 4 个角点（位置 + 纹理坐标 + 该面朝外法线）
		data.vertices = {
			// +X 面（法线 +X）
			{ { +h, -h, -h }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { +h, +h, -h }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { +h, +h, +h }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { +h, -h, +h }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			// -X 面（法线 -X）
			{ { -h, -h, +h }, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -h, +h, +h }, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -h, +h, -h }, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
			{ { -h, -h, -h }, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
			// +Y 面（法线 +Y）
			{ { -h, +h, +h }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { +h, +h, +h }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { +h, +h, -h }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -h, +h, -h }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			// -Y 面（法线 -Y）
			{ { -h, -h, -h }, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { +h, -h, -h }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { +h, -h, +h }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
			{ { -h, -h, +h }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
			// +Z 面（法线 +Z）
			{ { -h, -h, +h }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { +h, -h, +h }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { +h, +h, +h }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { -h, +h, +h }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			// -Z 面（法线 -Z）
			{ { +h, -h, -h }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { -h, -h, -h }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { -h, +h, -h }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { +h, +h, -h }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
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

	/*
	生成 UV 球体（第六十六轮）
	slices 经度分段、stacks 纬度分段；顶点 (stacks+1) × (slices+1)（末列与首列重合以保留 UV 接缝）；
	法线 = 归一化位置（半径归一），索引每格两三角形、外向绕序。
	*/
	Mesh_Data Mesh_Context::sphere_generate(float radius, uint32_t slices, uint32_t stacks)
	{
		//分段数兜底
		if (slices < 3)
			slices = 3;
		if (stacks < 2)
			stacks = 2;
		Mesh_Data data;
		//逐纬度环生成顶点
		for (uint32_t stack = 0; stack <= stacks; ++stack)
		{
			//纬度参数（0 = 北极、1 = 南极）
			float v = static_cast<float>(stack) / static_cast<float>(stacks);
			float phi = 3.14159265f * v;
			//本环半径与高度
			float ring_radius = radius * std::sin(phi);
			float y = radius * std::cos(phi);
			//逐经度生成（含接缝列）
			for (uint32_t slice = 0; slice <= slices; ++slice)
			{
				float u = static_cast<float>(slice) / static_cast<float>(slices);
				float theta = 6.2831853f * u;
				float x = ring_radius * std::cos(theta);
				float z = ring_radius * std::sin(theta);
				data.vertices.push_back({ { x, y, z }, { u, v }, { x / radius, y / radius, z / radius } });
			}
		}
		//每格两三角形（外向绕序）
		for (uint32_t stack = 0; stack < stacks; ++stack)
		{
			for (uint32_t slice = 0; slice < slices; ++slice)
			{
				uint32_t a = stack * (slices + 1) + slice;
				uint32_t b = a + slices + 1;
				data.indices.push_back(a);
				data.indices.push_back(a + 1);
				data.indices.push_back(b);
				data.indices.push_back(a + 1);
				data.indices.push_back(b + 1);
				data.indices.push_back(b);
			}
		}
		return data;
	}

	/*
	生成带端盖圆柱（第六十六轮）
	沿 Y 轴、中心在原点；侧面两圈（底部 / 顶部，接缝复制列）法线径向；
	上下端盖各一中心顶点 ＋ 扇形（法线朝向 ±Y）。
	*/
	Mesh_Data Mesh_Context::cylinder_generate(float radius, float height, uint32_t segments)
	{
		//分段数兜底
		if (segments < 3)
			segments = 3;
		Mesh_Data data;
		//半高
		float half_height = height * 0.5f;
		//侧面：底部一圈 + 顶部一圈（末列与首列重合以保留 UV 接缝）
		for (uint32_t ring = 0; ring < 2; ++ring)
		{
			float y = (ring == 0) ? -half_height : half_height;
			float v = (ring == 0) ? 0.0f : 1.0f;
			for (uint32_t index = 0; index <= segments; ++index)
			{
				float u = static_cast<float>(index) / static_cast<float>(segments);
				float theta = 6.2831853f * u;
				float nx = std::cos(theta);
				float nz = std::sin(theta);
				data.vertices.push_back({ { radius * nx, y, radius * nz }, { u, v }, { nx, 0.0f, nz } });
			}
		}
		//侧面索引（外向绕序）
		const uint32_t stride = segments + 1;
		for (uint32_t index = 0; index < segments; ++index)
		{
			uint32_t a = index;
			uint32_t b = index + 1;
			uint32_t c = stride + index;
			uint32_t d = stride + index + 1;
			data.indices.push_back(a);
			data.indices.push_back(c);
			data.indices.push_back(b);
			data.indices.push_back(b);
			data.indices.push_back(c);
			data.indices.push_back(d);
		}
		//端盖（normal_y = +1 顶面、-1 底面）
		auto add_cap = [&](float y, float normal_y)
		{
			//中心顶点
			uint32_t center = static_cast<uint32_t>(data.vertices.size());
			data.vertices.push_back({ { 0.0f, y, 0.0f }, { 0.5f, 0.5f }, { 0.0f, normal_y, 0.0f } });
			//环顶点（含接缝列）
			uint32_t start = static_cast<uint32_t>(data.vertices.size());
			for (uint32_t index = 0; index <= segments; ++index)
			{
				float u = static_cast<float>(index) / static_cast<float>(segments);
				float theta = 6.2831853f * u;
				float nx = std::cos(theta);
				float nz = std::sin(theta);
				data.vertices.push_back({ { radius * nx, y, radius * nz },
					{ 0.5f + 0.5f * nx, 0.5f + 0.5f * nz }, { 0.0f, normal_y, 0.0f } });
			}
			//扇形索引（顶面逆序、底面顺序，保证法线与绕序自洽）
			for (uint32_t index = 0; index < segments; ++index)
			{
				data.indices.push_back(center);
				if (normal_y > 0.0f)
				{
					data.indices.push_back(start + index + 1);
					data.indices.push_back(start + index);
				}
				else
				{
					data.indices.push_back(start + index);
					data.indices.push_back(start + index + 1);
				}
			}
		};
		add_cap(half_height, 1.0f);
		add_cap(-half_height, -1.0f);
		return data;
	}
}
