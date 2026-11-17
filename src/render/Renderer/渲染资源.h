#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	struct Render_Core_Context;
	class Renderer;
	struct Resource_State;
	class Ui_Renderer;
	class Font_Context;
	struct Frame_Render_Item;
	struct Frame_Render_Target;

	/*
	渲染器域：资源
	网格 / 纹理 / 材质 / 图形管线。
	*/
	class Render_Resource
	{
	public:
		Render_Resource(Renderer* owner, const Render_Core_Context& core_context);
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
		bool pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
			const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
			bool depth_write = true, bool cull_front = false, bool depth_test = true,
			bool depth_format_override = false, uint32_t depth_format = 0, uint32_t sample_count = 0,
			uint32_t color_format = 0);
		bool pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader,
			Blend_Mode blend_mode = Blend_Mode::Opaque, bool alpha_to_coverage = false);
		//卸载具名图形管线
		void pipeline_unload(uint64_t handle);
		//设置后续绘制所用图形管线
		void pipeline_bind_set(uint64_t handle);

		//=== 原渲染目标资源接口方法（签名原样照抄）===
		//创建离屏渲染目标
		bool render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height);
		//销毁离屏渲染目标（句柄无效则忽略）
		void render_target_destroy(uint64_t handle);
		//查询离屏渲染目标尺寸
		bool render_target_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const;
		//读回离屏渲染目标颜色
		bool render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//读回离屏渲染目标颜色子区域
		bool render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//读回离屏渲染目标深度
		bool render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//读回离屏渲染目标深度子区域
		bool render_target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//把离屏渲染目标颜色绑定为当前纹理
		void texture_bind_target(uint64_t handle);

		//=== 域生命周期与层内成员窄接口 ===
		//初始化（原 renderer_init_graphics 中纹理 / 默认资源部分）
		bool init(const Renderer_Config& effective);
		//关闭（原 shutdown 中网格 / 纹理 / 材质 / 管线相关部分）
		void shutdown(void);
		//按帧槽绑定 Cluster 图形描述符（原生缓冲以 uint64_t 承载）
		bool scene_cluster_bind(uint32_t slot, uint64_t light_buffer, uint64_t counts_buffer,
			uint64_t offsets_buffer, uint64_t list_buffer);
		//初始化、帧描述符与资源生命周期协作
		void frame_inputs_reserve(void);
		bool scene_shaders_load(const std::string& shader_dir, uint32_t& vertex_input_locations);
		bool scene_pipeline_infrastructure_build(uint32_t color_format, uint32_t depth_format,
			uint32_t frame_overlap, uint32_t sample_count, uint32_t max_push_constant_size);
		bool scene_pipeline_build(uint32_t vertex_input_locations);
		bool scene_buffer_bind(uint32_t slot, uint64_t scene_buffer);
		bool scene_shadow_bind(uint32_t slot, uint64_t sampler, uint64_t shadow_view);
		bool scene_spot_shadow_bind(uint32_t slot, uint64_t sampler, uint64_t shadow_view, uint32_t tile);
		bool post_stage_bind(uint32_t slot, uint32_t stage, uint64_t sampler, uint64_t source_view,
			uint64_t scene_view, uint64_t depth_view, uint64_t shadow_view);
		bool render_target_create_format(uint64_t& handle_out, uint32_t width, uint32_t height,
			uint32_t color_format, uint32_t depth_format, uint32_t sample_count = 0);
		bool render_target_native_get(uint64_t handle, Render_Target_Native_Info& info_out) const;
		void render_target_release(uint64_t handle);
		bool texture_native_get(uint64_t handle, uint64_t& view_out, uint64_t& sampler_out) const;
		uint64_t default_texture_handle(void) const;
		bool default_texture_native_get(uint64_t& view_out, uint64_t& sampler_out) const;
		uint64_t default_sky_texture_handle(void) const;
		uint64_t sky_pipeline_handle(void) const;
		bool sky_pipeline_resource_ensure(void);
		void pipeline_cache_set(const uint8_t* data, std::size_t size);
		bool pipeline_cache_get(std::vector<uint8_t>& bytes_out) const;
		void pipeline_shutdown(void);
		//按句柄查询原生图形管线
		uint64_t pipeline_native_get(uint64_t handle) const;
		//查询离屏目标是否已登记
		bool render_target_valid(uint64_t handle) const;
		//交换链格式或采样数变化后重建图形管线
		bool pipeline_rebuild(uint32_t color_format, uint32_t depth_format, uint32_t sample_count);
		//加载或销毁独立着色器模块（原生模块以 uint64_t 承载）
		bool shader_module_load(uint64_t& module_out, const std::string& shader_name);
		void shader_module_destroy(uint64_t module);
		//阴影管线登记与阴影目标组分辨率切换（失败保留旧目标并完整回滚绑定）
		bool shadow_pipeline_build(uint64_t& handle_out);
		bool shadow_targets_resize(uint64_t old_directional, const uint64_t old_spot_targets[8],
			uint32_t resolution, uint32_t old_resolution, uint32_t frame_overlap, uint32_t depth_format,
			uint64_t& new_directional, uint64_t new_spot_targets[8]);
		//资源模块初始化、统计与后处理查找表
		bool mesh_resource_build(void);
		void resource_counts_fill(Renderer_Resource_Counts& counts) const;
		bool pipeline_valid(uint64_t handle) const;
		uint32_t pipeline_sample_count(uint64_t handle) const;
		void post_lut_bind(uint64_t texture_handle, uint32_t frame_overlap);
		uint64_t post_lut_get(void) const;
		bool render_target_sample_count_set(uint32_t samples);
		void default_texture_rebind(uint32_t frame_overlap);
		//界面域资源装配与文字网格生命周期
		bool ui_render_resource_build(Ui_Renderer& ui_render, Font_Context& font,
			uint64_t solid_pipeline, uint64_t text_pipeline, uint32_t frame_overlap);
		bool font_resource_build(Font_Context& font);
		bool text_mesh_create(Mesh_Handle& mesh, const Mesh_Data& data);
		void text_mesh_destroy(Mesh_Handle mesh);
		//帧绘制项与原生资源值快照装配
		bool mesh_native_get(Mesh_Handle handle, uint64_t& buffer_out,
			uint64_t& index_offset_out, uint32_t& index_count_out) const;
		uint64_t pipeline_layout_native(void) const;
		uint64_t pipeline_scene_set_native(uint32_t slot, bool particle = false) const;
		uint64_t pipeline_sampler_set_native(uint32_t slot) const;
		uint64_t pipeline_post_set_native(uint32_t slot, uint32_t stage) const;
		uint64_t pipeline_bound_handle(void) const;
		bool particle_buffers_bind(uint32_t slot, uint64_t scene_buffer,
			uint64_t state_buffer, uint64_t index_buffer);
		bool render_target_frame_get(uint64_t handle, Frame_Render_Target& target_out) const;
		bool render_target_frame_full_get(uint64_t handle, Frame_Render_Target& target_out) const;
		uint64_t render_target_msaa_depth_image_native(uint64_t handle) const;
		void frame_descriptor_sets_fill(std::vector<Frame_Render_Item>& items, uint32_t slot,
			uint64_t instance_buffer, uint64_t shadow_target, bool spot_atlas,
			uint64_t spot_atlas_target, const uint64_t spot_targets[8]);
	private:
		//门面自身（跨域语义调用）
		Renderer* owner = nullptr;
		const Render_Core_Context* core = nullptr;
		//资源域状态
		std::unique_ptr<Resource_State> state_ptr;
	};
}
