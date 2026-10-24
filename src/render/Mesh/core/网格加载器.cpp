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
