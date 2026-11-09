#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：网格
	*/
	class Renderer_Mesh : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		加载网格（顶点 / 索引缓冲上传至设备本地）
		失败返回假，句柄保持无效（0）。
		*/
		bool mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data);
		/*
		加载带子网格分段的网格（第四百一十六轮）
		与 mesh_load 相同，但额外保存 sections（每段 first_index / index_count / material）；
		段校验在网格模块完成（索引数为 3 的倍数、区间不越界），任一不合法整次拒绝并返回假。
		注意（第四百一十六轮修正）：分段不作为 Mesh_Data 字段，避免与 EngineFoundation 同名 Mesh_Data 的 ODR 冲突。
		*/
		bool mesh_load_sections(Mesh_Handle& handle_out, const Mesh_Data& data,
			const Mesh_Section* sections, uint32_t count);
		/*
		从 OBJ 文件加载网格（第四百二十二轮；资产加载方向第一阶段）
		解析 v / vt / vn / f（多边形按扇形三角化、支持负数索引）与 usemtl 分组；
		缺纹理坐标填 (0,0)，缺法线按相邻三角形面积加权自动生成；
		成功时按 usemtl 分组建立子网格段（段序号即材质槽），可配合 mesh_draw_sections 逐段绑材质绘制。
		路径按 UTF-8 解释；失败返回假且句柄为 0（文件不存在 / 超限 / 语法非法 / 无有效三角面）。
		*/
		bool mesh_load_obj(Mesh_Handle& handle_out, const std::string& path);
		/*
		加载程序化三角形（第一百四十二轮新增）
		单个正面朝 +Z 的等腰三角形（3 顶点 / 3 索引）；仅需已初始化，无参数校验。
		*/
		bool mesh_triangle_load(Mesh_Handle& handle_out);
		/*
		加载程序化立方体（第一百四十二轮新增）
		half_extent 为正（边长 = 2 × half_extent，中心在原点）；每面独立法线，24 顶点 / 36 索引；
		非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_cube_load(Mesh_Handle& handle_out, float half_extent);
		/*
		加载程序化球体（第六十六轮新增）
		radius 为正；slices 经度分段 3..128、stacks 纬度分段 2..64；
		非法参数整次拒绝（句柄置 0 并告警）。法线朝外，UV 覆盖整球。
		*/
		bool mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks);
		/*
		加载程序化正二十面体球（第一百八十九轮新增）
		radius 为正；subdivisions 细分次数 0..4（0 = 正二十面体 12 顶点 / 20 面，每级面数 ×4）；
		非法参数整次拒绝（句柄置 0 并告警）。顶点按球面归一化，法线朝外，UV 按球坐标展开；
		与 UV 球相比三角形更均匀（适合展示与物理近似）。
		*/
		bool mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions);
		/*
		加载程序化 XZ 平面细分网格（第一百九十三轮新增）
		width / depth 为正（世界单位，沿 X / Z、中心在原点）；segments_x / segments_z 分段数 1..512；
		顶点 (segments_x+1) × (segments_z+1)、索引 segments_x × segments_z × 6；
		法线恒 +Y、UV 覆盖 [0,1]；非法参数整次拒绝（句柄置 0 并告警）。适合作地形 / 水面底板。
		*/
		bool mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
			uint32_t segments_x, uint32_t segments_z);
		/*
		加载程序化圆柱（第六十六轮新增）
		radius / height 为正（沿 Y 轴、中心在原点）；segments 圆周分段 3..128；
		非法参数整次拒绝（句柄置 0 并告警）。侧面法线径向，上下端盖法线朝 ±Y。
		*/
		bool mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		/*
		加载程序化圆锥（第六十八轮新增）
		radius / height 为正（沿 Y 轴、中心在原点、锥尖朝 +Y）；segments 圆周分段 3..128；
		非法参数整次拒绝（句柄置 0 并告警）。侧面法线按锥面斜率倾斜，底面端盖法线朝 -Y。
		*/
		bool mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		/*
		加载程序化四棱锥（第二百三十二轮新增）
		base_half 底面半边长（> 0）、height 总高（> 0，沿 Y 轴、中心在原点，锥尖朝 +Y）；
		非法参数整次拒绝（句柄置 0 并告警）。侧面与底面为逐面法线（硬边），外向绕序。
		*/
		bool mesh_pyramid_load(Mesh_Handle& handle_out, float base_half, float height);
		/*
		加载程序化胶囊（第六十九轮新增）
		radius 为正、height 为中段圆柱高度（可为 0 → 退化为球）；总高 = height + 2 × radius；
		segments 圆周分段 3..128、rings 单侧半球纬圈 2..64；
		非法参数整次拒绝（句柄置 0 并告警）。法线朝外，接缝 UV，外向绕序。
		*/
		bool mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
			uint32_t segments, uint32_t rings);
		/*
		加载程序化平面（第七十一轮新增）
		width / height 为正（沿 X / Y，中心在原点，法线朝 +Z）；非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_plane_load(Mesh_Handle& handle_out, float width, float height);
		/*
		加载程序化圆盘（第八十三轮新增）
		radius 为正、segments 圆周分段 3..256；非法参数整次拒绝（句柄置 0 并告警）。
		圆盘位于 XY 平面、中心在原点、法线朝 +Z，外向（从 +Z 观察逆时针）绕序。
		*/
		bool mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments);
		/*
		加载细分平面（第九十三轮新增）
		width / height 为正、columns / rows 为网格分段 1..256；
		顶点 (columns+1)×(rows+1)、索引 columns×rows×6，XY 平面、法线 +Z；非法参数整次拒绝。
		*/
		bool mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
			uint32_t columns, uint32_t rows);
		/*
		查询网格顶点 / 索引数量（第九十三轮新增）
		成功写出（各出参可为空）并返回真；未初始化 / 句柄无效返回假且出参归零。
		*/
		bool mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const;
		/*
		加载程序化圆环（第六十七轮新增）
		major_radius / minor_radius 为正；双分段 3..128；非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
			uint32_t major_segments, uint32_t minor_segments);
		//卸载网格
		void mesh_unload(Mesh_Handle handle);
		/*
		查询网格包围盒（第七十三轮新增；网格局部空间的轴对齐包围盒）
		成功写出 min / max 并返回真；未初始化 / 句柄无效返回假（出参不变）。
		*/
		bool mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const;
		/*
		更新网格顶点区域（第一百五十九轮新增）
		first_vertex 起、count 个顶点以 vertices 覆盖（结构须与创建时一致）；索引区不变。
		更新前会等尽在途帧；局部包围盒只扩不缩（保守），确保剔除不会误剔。
		未初始化 / 句柄无效 / 数据为空 / 区间越界返回假且内容不变。
		*/
		bool mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
			const Vertex* vertices);
		/*
		更新网格索引区域（第一百六十轮新增）
		first_index 起、count 个索引以 indices 覆盖；每个索引须小于该网格顶点数，否则整次拒绝。
		更新前会等尽在途帧；顶点区与网格计数不变。
		*/
		bool mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
			const uint32_t* indices);
		/*
		绘制网格
		transform 为列主序 16 浮点模型矩阵，可为 nullptr 表示单位矩阵；
		color 为可选的 RGBA 颜色（长度 ≥ 4，分量 0..1）：非空时随绘制项推送常量下发（offset 64），
		仅对声明了该推送常量的片元（文字）有意义；为空时不推送常量（场景 / 后处理路径的既有行为）；
		本调用只登记一个待绘制项，实际绘制在下一帧 frame_render 时发生；
		同一帧内可多次调用以登记多个网格（各自独立的模型矩阵）。
		*/
		bool mesh_draw(Mesh_Handle handle, const float* transform, const float* color = nullptr);
		/*
		按索引区间绘制网格（第四百一十轮；物体多材质的分段绘制基础）
		first_index 与 index_count 描述该网格索引区内的一个区间（count 个 uint32 索引）；
		first_index == 0 且 index_count == 0 视为整段（等价于 mesh_draw，内部即以此转发）；
		区间越界（first_index ≥ 总索引数 或 first_index + index_count > 总索引数）整次拒绝并告警；
		其余语义（模型矩阵 / 颜色 / 剔除 / 合批 / 延迟登记）与 mesh_draw 一致。
		*/
		bool mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
			uint32_t first_index, uint32_t index_count);
		/*
		多材质分段绘制（第四百一十轮；一个物体一次请求内使用多个材质）
		把网格索引区按 count 个材质均分（每段索引数按 3 对齐，避免切断三角形；余数归最后一段），
		逐段先 material_bind_set(materials[i]) 再按区间登记绘制，结束后恢复 material_bind_set(0)；
		materials 为空 / count 为 0 / 句柄无效 / 段数多于三角形数返回假且不登记任何绘制项；
		任一段登记失败时返回假（已登记的段不回滚，与 mesh_draw 的既有语义一致）。
		`instances`（第四百六十九轮；方向七阶段 C）非空时按段套用材质实例覆盖（tint / 自发光），
		长度须与 materials 相同（由调用方保证；句柄无效时不套用）。
		*/
		bool mesh_draw_sections(Mesh_Handle handle, const float* transform,
			const Material_Handle* materials, uint32_t count,
			const Material_Instance_Data* instances = nullptr);

		/*
		实例化绘制网格（同一网格 × N 个模型矩阵，一次绘制调用提交）
		transforms 为 count 个「列主序 16 浮点模型矩阵」的连续存储（共 count × 16 个浮点），可为 nullptr 表整批单位矩阵；
		count 为 0 或句柄无效时返回假，且**不登记任何绘制项**（不会留下半截绘制）；
		本调用只登记一个待绘制项（其内部为一个实例化绘制调用），实际绘制在下一帧 frame_render 时发生。
		*/
		bool mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count);

	};
}
