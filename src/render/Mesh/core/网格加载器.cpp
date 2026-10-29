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
		//按顶点 / 索引数量预留（第八十五轮：减少生成期重分配）
		data.vertices.reserve(static_cast<std::size_t>(stacks + 1) * (slices + 1));
		data.indices.reserve(static_cast<std::size_t>(stacks) * slices * 6);
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
	生成正二十面体球（第一百八十九轮）
	基础为 12 顶点 / 20 面正二十面体；每次细分把每个三角面拆成 4 个（中点缓存复用，避免裂缝），
	最后把所有顶点归一化到半径 radius、法线取归一化位置、UV 按球坐标展开。
	绕序沿用正二十面体标准外向逆时针表，故背面剔除行为与其它程序化图元一致。
	*/
	Mesh_Data Mesh_Context::icosphere_generate(float radius, uint32_t subdivisions)
	{
		//半径兜底与细分上限
		if (!(radius > 0.0f))
			radius = 1.0f;
		if (subdivisions > 4)
			subdivisions = 4;
		//基础正二十面体顶点（未归一化）
		const float golden = (1.0f + std::sqrt(5.0f)) * 0.5f;
		std::vector<std::array<float, 3>> points = {
			{ -1.0f, golden, 0.0f }, { 1.0f, golden, 0.0f }, { -1.0f, -golden, 0.0f }, { 1.0f, -golden, 0.0f },
			{ 0.0f, -1.0f, golden }, { 0.0f, 1.0f, golden }, { 0.0f, -1.0f, -golden }, { 0.0f, 1.0f, -golden },
			{ golden, 0.0f, -1.0f }, { golden, 0.0f, 1.0f }, { -golden, 0.0f, -1.0f }, { -golden, 0.0f, 1.0f },
		};
		//基础 20 个三角面（外向逆时针）
		std::vector<std::array<uint32_t, 3>> faces = {
			{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
			{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
			{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
			{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
		};
		//逐次细分（每次面数 ×4，中点按顶点对缓存复用）
		for (uint32_t level = 0; level < subdivisions; ++level)
		{
			//中点索引缓存（键 = 低位顶点索引 × 基数 ＋ 高位顶点索引）
			std::unordered_map<uint64_t, uint32_t> mid_cache;
			const uint64_t cache_base = static_cast<uint64_t>(points.size()) + 1;
			std::vector<std::array<uint32_t, 3>> next_faces;
			next_faces.reserve(faces.size() * 4);
			//取两点中点的索引（不存在则新建并归一化）
			auto midpoint_of = [&points, &mid_cache, cache_base](uint32_t a, uint32_t b) -> uint32_t
			{
				//键按大小顺序存放，保证 a→b 与 b→a 命中同一条目
				const uint64_t low = a < b ? a : b;
				const uint64_t high = a < b ? b : a;
				const uint64_t key = low * cache_base + high;
				auto it = mid_cache.find(key);
				//命中直接返回
				if (it != mid_cache.end())
					return it->second;
				//新建中点并归一化到单位球
				std::array<float, 3> mid = {
					(points[a][0] + points[b][0]) * 0.5f,
					(points[a][1] + points[b][1]) * 0.5f,
					(points[a][2] + points[b][2]) * 0.5f,
				};
				const float length = std::sqrt(mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2]);
				if (length > 0.0f)
					for (std::size_t axis = 0; axis < 3; ++axis)
						mid[axis] /= length;
				points.push_back(mid);
				const uint32_t index = static_cast<uint32_t>(points.size() - 1);
				mid_cache.emplace(key, index);
				return index;
			};
			//逐面拆四
			for (const std::array<uint32_t, 3>& face : faces)
			{
				const uint32_t ab = midpoint_of(face[0], face[1]);
				const uint32_t bc = midpoint_of(face[1], face[2]);
				const uint32_t ca = midpoint_of(face[2], face[0]);
				next_faces.push_back({ face[0], ab, ca });
				next_faces.push_back({ face[1], bc, ab });
				next_faces.push_back({ face[2], ca, bc });
				next_faces.push_back({ ab, bc, ca });
			}
			faces.swap(next_faces);
		}
		//组装网格数据（顶点归一化到 radius，UV 按球坐标展开）
		Mesh_Data data;
		data.vertices.reserve(points.size());
		for (const std::array<float, 3>& point : points)
		{
			//单位方向
			const float length = std::sqrt(point[0] * point[0] + point[1] * point[1] + point[2] * point[2]);
			const float nx = length > 0.0f ? point[0] / length : 0.0f;
			const float ny = length > 0.0f ? point[1] / length : 1.0f;
			const float nz = length > 0.0f ? point[2] / length : 0.0f;
			//球面 UV（经度 u、纬度 v；纬度钳到 [-1,1] 防浮点误差越界）
			const float clamped_y = ny < -1.0f ? -1.0f : (ny > 1.0f ? 1.0f : ny);
			const float u = std::atan2(nz, nx) / 6.2831853f + 0.5f;
			const float v = 0.5f - std::asin(clamped_y) / 3.14159265f;
			data.vertices.push_back({ { nx * radius, ny * radius, nz * radius }, { u, v }, { nx, ny, nz } });
		}
		data.indices.reserve(faces.size() * 3);
		for (const std::array<uint32_t, 3>& face : faces)
		{
			data.indices.push_back(face[0]);
			data.indices.push_back(face[1]);
			data.indices.push_back(face[2]);
		}
		return data;
	}

	/*
	生成 XZ 平面细分网格（第一百九十三轮）
	以原点为中心、Y = 0 的水平网格；法线恒 +Y、UV 在 [0,1] 上线性分布；
	正面绕序为「从 +Y 看逆时针」，与其它程序化图元一致。
	*/
	Mesh_Data Mesh_Context::ground_grid_generate(float width, float depth, uint32_t segments_x, uint32_t segments_z)
	{
		//参数兜底（非法值按 1 处理；门面已先行校验）
		if (!(width > 0.0f))
			width = 1.0f;
		if (!(depth > 0.0f))
			depth = 1.0f;
		if (segments_x < 1)
			segments_x = 1;
		if (segments_z < 1)
			segments_z = 1;
		//每行顶点数
		const uint32_t columns = segments_x + 1;
		Mesh_Data data;
		data.vertices.reserve(static_cast<std::size_t>(columns) * (segments_z + 1));
		//逐行（Z）逐列（X）铺顶点
		for (uint32_t row = 0; row <= segments_z; ++row)
		{
			const float v = static_cast<float>(row) / static_cast<float>(segments_z);
			const float z = (v - 0.5f) * depth;
			for (uint32_t column = 0; column <= segments_x; ++column)
			{
				const float u = static_cast<float>(column) / static_cast<float>(segments_x);
				const float x = (u - 0.5f) * width;
				data.vertices.push_back({ { x, 0.0f, z }, { u, v }, { 0.0f, 1.0f, 0.0f } });
			}
		}
		//逐格生成两个三角（从 +Y 看逆时针）
		data.indices.reserve(static_cast<std::size_t>(segments_x) * segments_z * 6);
		for (uint32_t row = 0; row < segments_z; ++row)
		{
			for (uint32_t column = 0; column < segments_x; ++column)
			{
				//本格左上角顶点与下一行同列顶点
				const uint32_t base = row * columns + column;
				const uint32_t next_row = base + columns;
				data.indices.push_back(base);
				data.indices.push_back(next_row);
				data.indices.push_back(next_row + 1);
				data.indices.push_back(base);
				data.indices.push_back(next_row + 1);
				data.indices.push_back(base + 1);
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
		//按顶点 / 索引数量预留（第八十五轮：减少生成期重分配）
		data.vertices.reserve(static_cast<std::size_t>(segments + 1) * 2 + (segments + 2) * 2);
		data.indices.reserve(static_cast<std::size_t>(segments) * 12);
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

	/*
	生成平面（第七十一轮）
	沿 X / Y 展开、中心在原点、法线朝 +Z；四个顶点 ＋ 两个三角形（与场景四边形同绕序）。
	*/
	Mesh_Data Mesh_Context::plane_generate(float width, float height)
	{
		Mesh_Data data;
		//半宽 / 半高
		float half_width = width * 0.5f;
		float half_height = height * 0.5f;
		const float normal[3] = { 0.0f, 0.0f, 1.0f };
		data.vertices = {
			{ { -half_width, -half_height, 0.0f }, { 0.0f, 1.0f }, { normal[0], normal[1], normal[2] } },
			{ { half_width, -half_height, 0.0f }, { 1.0f, 1.0f }, { normal[0], normal[1], normal[2] } },
			{ { half_width, half_height, 0.0f }, { 1.0f, 0.0f }, { normal[0], normal[1], normal[2] } },
			{ { -half_width, half_height, 0.0f }, { 0.0f, 0.0f }, { normal[0], normal[1], normal[2] } },
		};
		data.indices = { 0, 1, 2, 0, 2, 3 };
		return data;
	}

	/*
	生成圆盘（第八十三轮）
	XY 平面上的实心圆，中心在原点、法线朝 +Z；中心顶点 ＋ (segments+1) 个圆周顶点（末点重合以保留 UV 接缝）；
	三角扇索引，从 +Z 方向观察为逆时针。
	*/
	Mesh_Data Mesh_Context::disc_generate(float radius, uint32_t segments)
	{
		//分段数兜底
		if (segments < 3)
			segments = 3;
		Mesh_Data data;
		//圆周顶点数（含接缝复制点）
		data.vertices.reserve(static_cast<std::size_t>(segments) + 2);
		//中心顶点（UV 在圆心）
		data.vertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } });
		//圆周顶点（末点与首点重合）
		for (uint32_t index = 0; index <= segments; ++index)
		{
			//本顶点极角
			float angle = 6.28318530718f * static_cast<float>(index) / static_cast<float>(segments);
			float cosine = std::cos(angle);
			float sine = std::sin(angle);
			data.vertices.push_back({
				{ radius * cosine, radius * sine, 0.0f },
				{ 0.5f + 0.5f * cosine, 0.5f - 0.5f * sine },
				{ 0.0f, 0.0f, 1.0f } });
		}
		//三角扇（中心 → 相邻两圆周点）
		data.indices.reserve(static_cast<std::size_t>(segments) * 3);
		for (uint32_t index = 0; index < segments; ++index)
		{
			data.indices.push_back(0);
			data.indices.push_back(1 + index);
			data.indices.push_back(2 + index);
		}
		return data;
	}

	/*
	生成细分平面（第九十三轮）
	XY 平面上的规则网格：顶点 (columns+1)×(rows+1)，索引 columns×rows×6，法线 +Z，UV 覆盖整面。
	*/
	Mesh_Data Mesh_Context::grid_generate(float width, float height, uint32_t columns, uint32_t rows)
	{
		//分段数兜底
		if (columns < 1)
			columns = 1;
		if (rows < 1)
			rows = 1;
		Mesh_Data data;
		//按顶点 / 索引数量预留
		data.vertices.reserve(static_cast<std::size_t>(columns + 1) * (rows + 1));
		data.indices.reserve(static_cast<std::size_t>(columns) * rows * 6);
		//半宽 / 半高
		float half_width = width * 0.5f;
		float half_height = height * 0.5f;
		//逐行逐列生成顶点（v 自上而下）
		for (uint32_t row = 0; row <= rows; ++row)
		{
			float v = static_cast<float>(row) / static_cast<float>(rows);
			float y = half_height - height * v;
			for (uint32_t column = 0; column <= columns; ++column)
			{
				float u = static_cast<float>(column) / static_cast<float>(columns);
				float x = -half_width + width * u;
				data.vertices.push_back({ { x, y, 0.0f }, { u, v }, { 0.0f, 0.0f, 1.0f } });
			}
		}
		//逐格两三角形（外向绕序，从 +Z 观察逆时针）
		for (uint32_t row = 0; row < rows; ++row)
		{
			for (uint32_t column = 0; column < columns; ++column)
			{
				uint32_t top_left = row * (columns + 1) + column;
				uint32_t top_right = top_left + 1;
				uint32_t bottom_left = top_left + columns + 1;
				uint32_t bottom_right = bottom_left + 1;
				data.indices.push_back(top_left);
				data.indices.push_back(bottom_left);
				data.indices.push_back(bottom_right);
				data.indices.push_back(top_left);
				data.indices.push_back(bottom_right);
				data.indices.push_back(top_right);
			}
		}
		return data;
	}

	/*
	生成胶囊（第六十九轮）
	中段圆柱 ＋ 上下半球；总高 = height + 2 × radius，沿 Y 轴、中心在原点；
	环表自北向南排列（上半球 → 下赤道 → 下半球），法线朝外、接缝 UV、外向绕序与球体一致。
	*/
	Mesh_Data Mesh_Context::capsule_generate(float radius, float height, uint32_t segments, uint32_t rings)
	{
		//分段数兜底
		if (segments < 3)
			segments = 3;
		if (rings < 2)
			rings = 2;
		Mesh_Data data;
		//圆柱段半高
		float half_height = height * 0.5f;
		//单个环：高度、圆周半径、法线径向系数、法线纵向分量
		struct Ring
		{
			float y = 0.0f;
			float ring_radius = 0.0f;
			float radial = 0.0f;
			float vertical = 0.0f;
		};
		//环表（自北向南）
		std::vector<Ring> ring_list;
		ring_list.reserve(2 * rings + 2);
		//按顶点 / 索引数量预留（第八十五轮：减少生成期重分配）
		data.vertices.reserve(static_cast<std::size_t>(2 * rings + 2) * (segments + 1));
		data.indices.reserve(static_cast<std::size_t>(2 * rings + 1) * segments * 6);
		//上半球（北极 → 赤道）
		for (uint32_t index = 0; index <= rings; ++index)
		{
			float factor = static_cast<float>(index) / static_cast<float>(rings);
			float phi = 1.5707963f * factor;
			ring_list.push_back({ half_height + radius * std::cos(phi), radius * std::sin(phi),
				std::sin(phi), std::cos(phi) });
		}
		//圆柱底端（下赤道，法线纯径向）
		ring_list.push_back({ -half_height, radius, 1.0f, 0.0f });
		//下半球（赤道 → 南极）
		for (uint32_t index = 1; index <= rings; ++index)
		{
			float factor = static_cast<float>(index) / static_cast<float>(rings);
			float phi = 1.5707963f * factor;
			ring_list.push_back({ -half_height - radius * std::sin(phi), radius * std::cos(phi),
				std::cos(phi), -std::sin(phi) });
		}
		//逐环生成顶点（含接缝列）
		const uint32_t ring_count = static_cast<uint32_t>(ring_list.size());
		for (uint32_t ring_index = 0; ring_index < ring_count; ++ring_index)
		{
			const Ring& ring = ring_list[ring_index];
			float v = static_cast<float>(ring_index) / static_cast<float>(ring_count - 1);
			for (uint32_t slice = 0; slice <= segments; ++slice)
			{
				float u = static_cast<float>(slice) / static_cast<float>(segments);
				float theta = 6.2831853f * u;
				float nx = std::cos(theta);
				float nz = std::sin(theta);
				data.vertices.push_back({ { ring.ring_radius * nx, ring.y, ring.ring_radius * nz }, { u, v },
					{ ring.radial * nx, ring.vertical, ring.radial * nz } });
			}
		}
		//每格两三角形（外向绕序，与球体一致）
		for (uint32_t ring_index = 0; ring_index + 1 < ring_count; ++ring_index)
		{
			for (uint32_t slice = 0; slice < segments; ++slice)
			{
				uint32_t a = ring_index * (segments + 1) + slice;
				uint32_t b = a + segments + 1;
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
	生成圆锥（第六十八轮）
	底面半径 radius、总高 height，沿 Y 轴、中心在原点、锥尖朝 +Y；
	侧面两圈（底面 / 锥尖，接缝复制列）法线按锥面斜率倾斜，底面端盖法线朝 -Y。
	*/
	Mesh_Data Mesh_Context::cone_generate(float radius, float height, uint32_t segments)
	{
		//分段数兜底
		if (segments < 3)
			segments = 3;
		Mesh_Data data;
		//按顶点 / 索引数量预留（第八十五轮：减少生成期重分配）
		data.vertices.reserve(static_cast<std::size_t>(segments + 1) * 2 + (segments + 2));
		data.indices.reserve(static_cast<std::size_t>(segments) * 9);
		//半高
		float half_height = height * 0.5f;
		/*
		锥面法线（侧面）
		曲面参数化 p(t, θ) = ((1−t)·r·cosθ, −h/2 + t·h, (1−t)·r·sinθ)，
		切向分别沿「高度」与「圆周」，叉积即法线 ∝ (h·cosθ, r, h·sinθ)；
		故上分量为底面半径、径向上分量为总高。
		*/
		float normal_radial = height;
		float normal_up = radius;
		float normal_length = std::sqrt(normal_radial * normal_radial + normal_up * normal_up);
		//侧面：底面一圈 + 锥尖一圈（末列与首列重合以保留 UV 接缝）
		for (uint32_t ring = 0; ring < 2; ++ring)
		{
			float y = (ring == 0) ? -half_height : half_height;
			float v = (ring == 0) ? 0.0f : 1.0f;
			//锥尖圈的圆周半径为 0
			float ring_radius = (ring == 0) ? radius : 0.0f;
			for (uint32_t index = 0; index <= segments; ++index)
			{
				float u = static_cast<float>(index) / static_cast<float>(segments);
				float theta = 6.2831853f * u;
				float nx = std::cos(theta);
				float nz = std::sin(theta);
				data.vertices.push_back({ { ring_radius * nx, y, ring_radius * nz }, { u, v },
					{ nx * normal_radial / normal_length, normal_up / normal_length, nz * normal_radial / normal_length } });
			}
		}
		/*
		侧面索引（外向绕序）
		锥尖一圈各列位置重合，故每段只有一枚三角形（底面 i → 锥尖 i → 底面 i+1），
		与圆柱侧面首个三角形同序，保证背面剔除下可见。
		*/
		const uint32_t stride = segments + 1;
		for (uint32_t index = 0; index < segments; ++index)
		{
			data.indices.push_back(index);
			data.indices.push_back(stride + index);
			data.indices.push_back(index + 1);
		}
		//底面端盖（法线朝 -Y；中心顶点 ＋ 扇形）
		uint32_t center = static_cast<uint32_t>(data.vertices.size());
		data.vertices.push_back({ { 0.0f, -half_height, 0.0f }, { 0.5f, 0.5f }, { 0.0f, -1.0f, 0.0f } });
		uint32_t start = static_cast<uint32_t>(data.vertices.size());
		for (uint32_t index = 0; index <= segments; ++index)
		{
			float u = static_cast<float>(index) / static_cast<float>(segments);
			float theta = 6.2831853f * u;
			float nx = std::cos(theta);
			float nz = std::sin(theta);
			data.vertices.push_back({ { radius * nx, -half_height, radius * nz },
				{ 0.5f + 0.5f * nx, 0.5f + 0.5f * nz }, { 0.0f, -1.0f, 0.0f } });
		}
		//扇形索引（底面朝向 -Y，顺序排列）
		for (uint32_t index = 0; index < segments; ++index)
		{
			data.indices.push_back(center);
			data.indices.push_back(start + index);
			data.indices.push_back(start + index + 1);
		}
		return data;
	}

	/*
	生成四棱锥（第二百三十二轮）
	底面为 XZ 平面上的正方形（半边长 base_half，位于 y = −height/2），锥尖在 (0, +height/2, 0)；
	四个侧面与底面各自独立顶点（法线取面法线，硬边）；UV：侧面底边 0/1、锥尖 0.5，底面膜面 0..1。
	*/
	Mesh_Data Mesh_Context::pyramid_generate(float base_half, float height)
	{
		Mesh_Data data;
		//按顶点 / 索引数量预留（18 顶点 / 18 索引：侧面 4 × 3 ＋ 底面 4 ＋ 2 三角形）
		data.vertices.reserve(16);
		data.indices.reserve(18);
		//半高
		float half_height = height * 0.5f;
		//底面四角（自上方看逆时针，便于底面法线朝 -Y）
		const float base_corners[4][3] = {
			{ -base_half, -half_height, -base_half },
			{ base_half, -half_height, -base_half },
			{ base_half, -half_height, base_half },
			{ -base_half, -half_height, base_half },
		};
		//锥尖
		const float apex[3] = { 0.0f, half_height, 0.0f };
		//逐侧面的面法线（叉积归一）
		auto face_normal = [](const float a[3], const float b[3], const float c[3], float out[3])
		{
			//两条边向量
			float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
			float ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
			//叉积
			out[0] = ab[1] * ac[2] - ab[2] * ac[1];
			out[1] = ab[2] * ac[0] - ab[0] * ac[2];
			out[2] = ab[0] * ac[1] - ab[1] * ac[0];
			//归一（退化面按 +Y 兜底）
			float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
			if (length < 1e-6f)
			{
				out[0] = 0.0f;
				out[1] = 1.0f;
				out[2] = 0.0f;
				return;
			}
			out[0] /= length;
			out[1] /= length;
			out[2] /= length;
		};
		//四个侧面：底边后角 → 前角 → 锥尖（外向逆时针绕序）
		for (uint32_t face = 0; face < 4; ++face)
		{
			const float* a = base_corners[face];
			const float* b = base_corners[(face + 1) % 4];
			float normal[3] = { 0.0f, 1.0f, 0.0f };
			face_normal(b, a, apex, normal);
			uint32_t first = static_cast<uint32_t>(data.vertices.size());
			data.vertices.push_back({ { b[0], b[1], b[2] }, { 1.0f, 0.0f }, { normal[0], normal[1], normal[2] } });
			data.vertices.push_back({ { a[0], a[1], a[2] }, { 0.0f, 0.0f }, { normal[0], normal[1], normal[2] } });
			data.vertices.push_back({ { apex[0], apex[1], apex[2] }, { 0.5f, 1.0f }, { normal[0], normal[1], normal[2] } });
			//单个三角形（外向）
			data.indices.push_back(first);
			data.indices.push_back(first + 1);
			data.indices.push_back(first + 2);
		}
		//底面：四角独立顶点（法线 -Y），两个三角形
		uint32_t base_first = static_cast<uint32_t>(data.vertices.size());
		for (uint32_t corner = 0; corner < 4; ++corner)
		{
			const float* p = base_corners[corner];
			//UV 按角点方位映射（-x/-z → (0,0) … +x/+z → (1,1)）
			float u = (p[0] > 0.0f) ? 1.0f : 0.0f;
			float v = (p[2] > 0.0f) ? 1.0f : 0.0f;
			data.vertices.push_back({ { p[0], p[1], p[2] }, { u, v }, { 0.0f, -1.0f, 0.0f } });
		}
		data.indices.push_back(base_first);
		data.indices.push_back(base_first + 1);
		data.indices.push_back(base_first + 2);
		data.indices.push_back(base_first);
		data.indices.push_back(base_first + 2);
		data.indices.push_back(base_first + 3);
		return data;
	}

	/*
	生成圆环（第六十七轮）
	major_radius 环半径、minor_radius 管半径；双分段 >= 3 兜底；
	顶点 (major+1) × (minor+1)（接缝复制），法线朝外，UV = (u, v) 归一化参数。
	*/
	Mesh_Data Mesh_Context::torus_generate(float major_radius, float minor_radius,
		uint32_t major_segments, uint32_t minor_segments)
	{
		//分段数兜底
		if (major_segments < 3)
			major_segments = 3;
		if (minor_segments < 3)
			minor_segments = 3;
		Mesh_Data data;
		//按顶点 / 索引数量预留（第八十五轮：减少生成期重分配）
		data.vertices.reserve(static_cast<std::size_t>(major_segments + 1) * (minor_segments + 1));
		data.indices.reserve(static_cast<std::size_t>(major_segments) * minor_segments * 6);
		//逐环绕角（u）与管周角（v）生成顶点
		for (uint32_t major = 0; major <= major_segments; ++major)
		{
			float u = static_cast<float>(major) / static_cast<float>(major_segments);
			float u_angle = 6.2831853f * u;
			float cos_u = std::cos(u_angle);
			float sin_u = std::sin(u_angle);
			for (uint32_t minor = 0; minor <= minor_segments; ++minor)
			{
				float v = static_cast<float>(minor) / static_cast<float>(minor_segments);
				float v_angle = 6.2831853f * v;
				float cos_v = std::cos(v_angle);
				float sin_v = std::sin(v_angle);
				//管截面中心到环心的半径
				float ring_radius = major_radius + minor_radius * cos_v;
				data.vertices.push_back({
					{ ring_radius * cos_u, minor_radius * sin_v, ring_radius * sin_u },
					{ u, v },
					{ cos_v * cos_u, sin_v, cos_v * sin_u },
				});
			}
		}
		//每格两三角形（外向绕序）
		const uint32_t stride = minor_segments + 1;
		for (uint32_t major = 0; major < major_segments; ++major)
		{
			for (uint32_t minor = 0; minor < minor_segments; ++minor)
			{
				uint32_t a = major * stride + minor;
				uint32_t b = a + stride;
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
}
