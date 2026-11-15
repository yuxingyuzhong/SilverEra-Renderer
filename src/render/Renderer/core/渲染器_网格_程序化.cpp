#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	namespace
	{
		//列主序 4×4 矩阵相乘（out = a × b；out 可与 a / b 不同）
		void matrix4_multiply(const float a[16], const float b[16], float out[16])
		{
			for (std::size_t column = 0; column < 4; ++column)
				for (std::size_t row = 0; row < 4; ++row)
				{
					//累加「a 的第 row 行」与「b 的第 column 列」的点积
					float sum = 0.0f;
					for (std::size_t k = 0; k < 4; ++k)
						sum += a[k * 4 + row] * b[column * 4 + k];
					out[column * 4 + row] = sum;
				}
		}

		/*
		视锥剔除（保守口径；第二十二轮新增）
		把局部包围盒 8 个角经「model × view_proj」变换到裁剪空间，逐个测 6 个裁剪面；
		**仅当 8 个角同时落在同一个面外**时判为「完全不可见」（返回真）；
		任何相交 / 跨越近平面一律保留（不做背面与遮挡剔除）。
		*/
		bool frustum_cull_aabb(const float view_proj[16], const float model[16], const float bounds_min[3], const float bounds_max[3])
		{
			//组合矩阵（列主序）
			float mvp[16] = {};
			matrix4_multiply(view_proj, model, mvp);
			//六个裁剪面的「全部角点在外」标志（左 / 右 / 下 / 上 / 近 / 远）
			bool outside[6] = { true, true, true, true, true, true };
			for (std::size_t corner = 0; corner < 8; ++corner)
			{
				//角点局部坐标（按位取上下界）
				float x = (corner & 1) ? bounds_max[0] : bounds_min[0];
				float y = (corner & 2) ? bounds_max[1] : bounds_min[1];
				float z = (corner & 4) ? bounds_max[2] : bounds_min[2];
				//变换到裁剪空间
				float clip_x = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
				float clip_y = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
				float clip_z = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
				float clip_w = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
				//逐面判「该角是否在外」
				if (!(clip_x > clip_w))
					outside[0] = false;
				if (!(clip_x < -clip_w))
					outside[1] = false;
				if (!(clip_y > clip_w))
					outside[2] = false;
				if (!(clip_y < -clip_w))
					outside[3] = false;
				if (!(clip_z < 0.0f))
					outside[4] = false;
				if (!(clip_z > clip_w))
					outside[5] = false;
			}
			//任一面「全部角点在外」即视为完全不可见
			for (std::size_t plane = 0; plane < 6; ++plane)
				if (outside[plane])
					return true;
			return false;
		}

		//单位矩阵（列主序）
		const float IDENTITY_MATRIX4[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};

		//白色（RGBA；第六十九轮：未设置逐物体色调时的默认推送值，乘到反照率上画面不变）
		const float WHITE_COLOR_RGBA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	//按程序化球体加载网格（第六十六轮新增）

	bool Render_Resource::mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks)
	{
		//前置：已初始化、半径为正（非数亦拒）、分段数在区间内
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f)
			|| slices < 3 || slices > 128 || stacks < 2 || stacks > 64)
		{
			logger.warn("渲染器:: 球体参数非法（半径 {}、经度 {}、纬度 {}）", radius, slices, stacks);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::sphere_generate(radius, slices, stacks));
	}

	bool Render_Resource::mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions)
	{
		//前置：已初始化、半径为正（非数亦拒）、细分次数在 0..4
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f) || subdivisions > 4)
		{
			logger.warn("渲染器:: 正二十面体球参数非法（半径 {}、细分 {}）", radius, subdivisions);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::icosphere_generate(radius, subdivisions));
	}

	bool Render_Resource::mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
		uint32_t segments_x, uint32_t segments_z)
	{
		//顶点总数（分段乘积上限约 100 万，防止误传超大分段耗尽内存）
		const uint64_t vertex_count = (static_cast<uint64_t>(segments_x) + 1)
			* (static_cast<uint64_t>(segments_z) + 1);
		//前置：已初始化、宽深为正（非数亦拒）、分段在 1..512 且顶点总数受限
		if (owner == nullptr || !owner->device().valid() || !(width > 0.0f) || !(depth > 0.0f)
			|| segments_x < 1 || segments_x > 512 || segments_z < 1 || segments_z > 512
			|| vertex_count > (1ull << 20))
		{
			logger.warn("渲染器:: 细分平面参数非法（宽 {}、深 {}、分段 {}×{}）",
				width, depth, segments_x, segments_z);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::ground_grid_generate(width, depth, segments_x, segments_z));
	}

	bool Render_Resource::mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆柱参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cylinder_generate(radius, height, segments));
	}

	bool Render_Resource::mesh_plane_load(Mesh_Handle& handle_out, float width, float height)
	{
		//前置：已初始化、宽高为正（非数亦拒）
		if (owner == nullptr || !owner->device().valid() || !(width > 0.0f) || !(height > 0.0f))
		{
			logger.warn("渲染器:: 平面参数非法（宽 {}、高 {}）", width, height);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::plane_generate(width, height));
	}

	bool Render_Resource::mesh_triangle_load(Mesh_Handle& handle_out)
	{
		//前置：已初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.warn("渲染器:: 三角形加载失败：尚未初始化");
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::triangle_generate());
	}

	bool Render_Resource::mesh_cube_load(Mesh_Handle& handle_out, float half_extent)
	{
		//前置：已初始化、半边长正（非数亦拒）
		if (owner == nullptr || !owner->device().valid() || !(half_extent > 0.0f))
		{
			logger.warn("渲染器:: 立方体参数非法（半边长 {}）", half_extent);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cube_generate(half_extent));
	}

	bool Render_Resource::mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments)
	{
		//前置：已初始化、半径为正（非数亦拒）、分段在区间内
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f)
			|| segments < 3 || segments > 256)
		{
			logger.warn("渲染器:: 圆盘参数非法（半径 {}、分段 {}）", radius, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::disc_generate(radius, segments));
	}

	bool Render_Resource::mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
		uint32_t columns, uint32_t rows)
	{
		//前置：已初始化、宽高为正（非数亦拒）、分段在区间内
		if (owner == nullptr || !owner->device().valid() || !(width > 0.0f) || !(height > 0.0f)
			|| columns < 1 || columns > 256 || rows < 1 || rows > 256)
		{
			logger.warn("渲染器:: 细分平面参数非法（宽 {}、高 {}、列 {}、行 {}）",
				width, height, columns, rows);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::grid_generate(width, height, columns, rows));
	}

	bool Render_Resource::mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
		uint32_t segments, uint32_t rings)
	{
		//前置：已初始化、半径为正、中段高度非负（非数亦拒）、分段与纬圈在区间内
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f) || !(height >= 0.0f)
			|| segments < 3 || segments > 128 || rings < 2 || rings > 64)
		{
			logger.warn("渲染器:: 胶囊参数非法（半径 {}、高度 {}、分段 {}、纬圈 {}）",
				radius, height, segments, rings);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::capsule_generate(radius, height, segments, rings));
	}

	bool Render_Resource::mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (owner == nullptr || !owner->device().valid() || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆锥参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cone_generate(radius, height, segments));
	}

	bool Render_Resource::mesh_pyramid_load(Mesh_Handle& handle_out, float base_half, float height)
	{
		//前置：已初始化、底面半边长与高度为正（非数亦拒）
		if (owner == nullptr || !owner->device().valid() || !(base_half > 0.0f) || !(height > 0.0f))
		{
			logger.warn("渲染器:: 四棱锥参数非法（底面半边长 {}、高度 {}）", base_half, height);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::pyramid_generate(base_half, height));
	}

	bool Render_Resource::mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
		uint32_t major_segments, uint32_t minor_segments)
	{
		//前置：已初始化、双半径为正（非数亦拒）、双分段在区间内
		if (owner == nullptr || !owner->device().valid() || !(major_radius > 0.0f) || !(minor_radius > 0.0f)
			|| major_segments < 3 || major_segments > 128 || minor_segments < 3 || minor_segments > 128)
		{
			logger.warn("渲染器:: 圆环参数非法（环半径 {}、管半径 {}、环分段 {}、管分段 {}）",
				major_radius, minor_radius, major_segments, minor_segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::torus_generate(major_radius, minor_radius,
			major_segments, minor_segments));
	}
}
