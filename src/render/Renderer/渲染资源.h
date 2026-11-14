#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;
	struct Resource_State;

	/*
	渲染器域：资源
	网格 / 纹理 / 材质 / 图形管线。
	*/
	class Render_Resource
	{
	public:
		explicit Render_Resource(Renderer* owner);
		~Render_Resource(void);
		//不可拷贝、不可移动
		Render_Resource(const Render_Resource&) = delete;
		Render_Resource& operator=(const Render_Resource&) = delete;

		//=== 原网格接口方法（签名原样照抄）===
		//加载网格（顶点 / 索引缓冲上传至设备本地）
		bool mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data);
		//加载带子网格分段的网格（第四百一十六轮）
		bool mesh_load_sections(Mesh_Handle& handle_out, const Mesh_Data& data,
			const Mesh_Section* sections, uint32_t count);
		//从 OBJ 文件加载网格（第四百二十二轮）
		bool mesh_load_obj(Mesh_Handle& handle_out, const std::string& path);
		//加载程序化三角形（第一百四十二轮）
		bool mesh_triangle_load(Mesh_Handle& handle_out);
		//加载程序化立方体（第一百四十二轮）
		bool mesh_cube_load(Mesh_Handle& handle_out, float half_extent);
		//加载程序化球体（第六十六轮）
		bool mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks);
		//加载程序化正二十面体球（第一百八十九轮）
		bool mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions);
		//加载程序化 XZ 平面细分网格（第一百九十三轮）
		bool mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
			uint32_t segments_x, uint32_t segments_z);
		//加载程序化圆柱（第六十六轮）
		bool mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		//加载程序化圆锥（第六十八轮）
		bool mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		//加载程序化四棱锥（第二百三十二轮）
		bool mesh_pyramid_load(Mesh_Handle& handle_out, float base_half, float height);
		//加载程序化胶囊（第六十九轮）
		bool mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
			uint32_t segments, uint32_t rings);
		//加载程序化平面（第七十一轮）
		bool mesh_plane_load(Mesh_Handle& handle_out, float width, float height);
		//加载程序化圆盘（第八十三轮）
		bool mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments);
		//加载细分平面（第九十三轮）
		bool mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
			uint32_t columns, uint32_t rows);
		//查询网格顶点 / 索引数量（第九十三轮）
		bool mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const;
		//加载程序化圆环（第六十七轮）
		bool mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
			uint32_t major_segments, uint32_t minor_segments);
		//卸载网格
		void mesh_unload(Mesh_Handle handle);
		//查询网格包围盒（第七十三轮）
		bool mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const;
		//更新网格顶点区域（第一百五十九轮）
		bool mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
			const Vertex* vertices);
		//更新网格索引区域（第一百六十轮）
		bool mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
			const uint32_t* indices);
		//绘制网格
		bool mesh_draw(Mesh_Handle handle, const float* transform, const float* color = nullptr);
		//按索引区间绘制网格（第四百一十轮）
		bool mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
			uint32_t first_index, uint32_t index_count);
		//多材质分段绘制（第四百一十轮）
		bool mesh_draw_sections(Mesh_Handle handle, const float* transform,
			const Material_Handle* materials, uint32_t count,
			const Material_Instance_Data* instances = nullptr);
		//实例化绘制网格
		bool mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count);

		//=== 原纹理接口方法（签名原样照抄）===
		//纹理加载（RGBA8 紧排像素）
		bool texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height);
		//从文件加载纹理（PNG / JPG / BMP 等经 stb 解码）
		bool texture_load_file(uint64_t& handle_out, const std::string& path);
		//程序生成棋盘格纹理
		bool texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size);
		//程序生成恒等 3D LUT（第一百四十四轮）
		bool texture_load_lut_identity(uint64_t& handle_out, uint32_t edge);
		//从内存像素创建立方体贴图（第一百二十五轮）
		bool texture_cube_load(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height);
		//天空盒 / 简易 IBL 开关（第一百二十五轮）
		void sky_enable_set(bool enabled);
		//当前是否启用天空盒
		bool sky_enabled(void) const;
		//设置天空盒强度
		void sky_params_set(float ambient, float specular);
		//读取天空盒强度（出参可为空）
		void sky_params_get(float* ambient_out, float* specular_out) const;
		//绑定天空盒立方体贴图（0 = 内置纯白立方体）
		void sky_texture_set(uint64_t texture_handle);
		//当前天空盒立方体贴图句柄
		uint64_t sky_texture(void) const;
		//卸载纹理（句柄无效则忽略）
		void texture_unload(uint64_t handle);
		//读回纹理第 0 级像素（第二十七轮）
		bool texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//读回纹理第 0 级的指定子区域（第二十八轮）
		bool texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//更新纹理第 0 级的子区域（第一百五十一轮）
		bool texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		//更新纹理指定级的子区域（第一百五十八轮）
		bool texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		//读回纹理指定级的子区域（第一百六十六轮）
		bool texture_readback_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//读回纹理指定级整幅像素（第一百五十八轮）
		bool texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//查询纹理 mip 级数（第一百六十二轮）
		bool texture_mip_levels(uint64_t handle, uint32_t* mip_levels_out) const;
		//查询纹理指定级尺寸（第一百六十一轮）
		bool texture_size_level(uint64_t handle, uint32_t level, uint32_t* width_out, uint32_t* height_out) const;
		//查询纹理尺寸（第九十轮）
		bool texture_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const;
		//设置当前绘制所用纹理（0 或无效句柄回落默认白纹理）
		void texture_bind_set(uint64_t texture_handle);
		//设置纹理采样器状态
		void texture_sampler_state_set(const Texture_Sampler_State& state);
		//读取当前采样器状态（只读诊断）
		Texture_Sampler_State texture_sampler_state_get(void) const;

		//=== 原材质接口方法（签名原样照抄）===
		//登记材质（图形管线 ＋ 纹理 ＋ 混合档）
		bool material_load(Material_Handle& handle_out, const Material_Data& data);
		//从 MTL 材质库创建材质（第四百二十三轮）
		bool material_load_mtl(std::vector<Material_Handle>& handles_out, const std::string& path,
			std::vector<std::string>* names_out = nullptr);
		//校验材质实例数据（第四百四十八轮）
		bool material_instance_validate(const Material_Instance_Data& data) const;
		//卸载材质（句柄无效则忽略）
		void material_unload(Material_Handle handle);
		//设置后续绘制所用材质
		void material_bind_set(Material_Handle handle);
		//设置逐物体 PBR 材质（第六十八轮）
		bool material_pbr_set(float roughness, float metallic);
		//清除逐物体 PBR 材质（第六十八轮）
		void material_pbr_clear(void);
		//当前是否启用逐物体 PBR 材质覆盖
		bool material_pbr_active(void) const;
		//当前逐物体材质粗糙度缩放
		float material_pbr_roughness(void) const;
		//当前逐物体材质金属度
		float material_pbr_metallic(void) const;
		//设置逐物体清漆层（第二百三十九轮）
		bool material_clear_coat_set(float strength, float roughness);
		//清除逐物体清漆层（第二百三十九轮）
		void material_clear_coat_clear(void);
		//当前是否启用逐物体清漆层
		bool material_clear_coat_active(void) const;
		//当前逐物体清漆强度
		float material_clear_coat_strength(void) const;
		//当前逐物体清漆粗糙度
		float material_clear_coat_roughness(void) const;
		//设置逐物体反照率色调（第六十九轮）
		bool material_tint_set(float red, float green, float blue, float alpha = 1.0f);
		//清除逐物体反照率色调（第六十九轮）
		void material_tint_clear(void);
		//当前是否启用逐物体反照率色调
		bool material_tint_active(void) const;
		//读取当前逐物体色调
		void material_tint_get(float out[4]) const;
		//设置逐物体自发光强度（第七十轮）
		bool material_emissive_set(float emissive);
		//清除逐物体自发光
		void material_emissive_clear(void);
		//当前逐物体自发光强度
		float material_emissive(void) const;

		//=== 原管线接口方法（签名原样照抄）===
		//设置后续绘制所用混合档
		void blend_mode_set(Blend_Mode mode);
		//登记具名图形管线
		bool pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader,
			Blend_Mode blend_mode = Blend_Mode::Opaque, bool alpha_to_coverage = false);
		//卸载具名图形管线
		void pipeline_unload(uint64_t handle);
		//设置后续绘制所用图形管线
		void pipeline_bind_set(uint64_t handle);

		//=== 迁移期内部入口（阶段 8 收口时降为私有）===
		//初始化（原 renderer_init_graphics 中纹理 / 默认资源部分）
		bool init(const Renderer_Config& effective);
		//关闭（原 shutdown 中网格 / 纹理 / 材质 / 管线相关部分）
		void shutdown(void);
		//迁移期：状态直取（供其余域过渡）
		Resource_State& state(void);
		const Resource_State& state(void) const;

	private:
		//门面自身（迁移期用于调用尚未迁出的门面方法与 impl）
		Renderer* owner = nullptr;
		//资源域状态
		std::unique_ptr<Resource_State> state_ptr;
	};
}
