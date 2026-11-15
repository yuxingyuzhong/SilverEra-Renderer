#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;
	struct Scene_State;

	/*
	渲染器域：场景
	相机 / 场景装配 / 光源 / 雾 / 风格 / 阴影开关与参数。
	*/
	class Render_Scene
	{
	public:
		explicit Render_Scene(Renderer* owner);
		~Render_Scene(void);
		//不可拷贝、不可移动
		Render_Scene(const Render_Scene&) = delete;
		Render_Scene& operator=(const Render_Scene&) = delete;

		//=== 原场景域接口方法（签名原样照抄）===
		//相机：设置位置
		void camera_position_set(float x, float y, float z);
		//相机：设置注视点
		void camera_target_set(float x, float y, float z);
		//相机：设置视场角（弧度）
		void camera_fov_set(float radians);
		//相机：设置近远平面
		void camera_near_far_set(float near_plane, float far_plane);
		//切换正交 / 透视投影
		void camera_ortho_set(bool enabled, float ortho_height);
		//设置相机上方向
		void camera_up_set(float x, float y, float z);
		//设置宽高比覆盖（0 = 跟随窗口）
		void camera_aspect_set(float aspect);
		//查询当前生效宽高比（覆盖优先；未初始化返回 0）
		float camera_aspect_get(void) const;
		//查询视场角（弧度；未初始化返回假且出参不变）
		bool camera_fov_get(float* radians_out) const;
		//查询指定视深处可见的垂直世界高度
		bool camera_world_height_at(float view_depth, float* height_out) const;
		//查询投影模式与正交高度（未初始化返回假且出参不变）
		bool camera_ortho_get(bool* enabled_out, float* height_out) const;
		//世界点 → 屏幕坐标
		bool camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const;
		//屏幕坐标 → 世界点
		bool camera_unproject(float u, float v, float depth, float world_out[3]) const;
		//世界空间 AABB 是否与视锥相交
		bool camera_aabb_visible(const float min[3], const float max[3]) const;
		//导出相机「视图 × 投影」矩阵
		bool camera_view_proj(float out[16]) const;

		//应用场景描述（相机与清屏色）
		void scene_apply(const Scene_Data& scene);
		//应用场景描述（相机 / 光照 / 雾三组入参）
		void scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog);
		//设置清屏色（RGBA）
		void clear_color_set(const float color[4]);
		//设置方向光与环境光
		void light_set(const float direction[3], const float color[3], float intensity, float ambient);
		//设置点光源
		void light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient);
		//追加一个点光源
		bool light_add_point(const float position[3], const float color[3], float radius, float intensity,
			float ambient, uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		//追加一个聚光灯
		bool light_add_spot(const float position[3], const float direction[3], const float color[3],
			float radius, float intensity, float inner_deg, float outer_deg, float ambient,
			uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		//清空全部点光源并恢复方向光默认强度
		void lights_clear(void);
		//已登记点光源数量（含聚光灯；未初始化返回 0）
		uint32_t point_light_count(void) const;
		//读取第 index 盏点光源的状态快照
		bool point_light_state_get(uint32_t index, Point_Light_State& state_out) const;
		//光源记录快照
		uint32_t light_records_get(Light_Record* out, uint32_t capacity) const;
		//当前光源记录总数
		uint32_t light_record_count(void) const;
		//设置光源选择模式与预算
		void light_select_set(uint32_t mode, uint32_t budget);
		//设置镜面高光参数
		void specular_set(float intensity, float shininess);
		//设置距离衰减模式
		bool light_attenuation_set(uint32_t mode);
		//读取当前距离衰减模式
		uint32_t light_attenuation_get(void) const;
		//当前聚光灯数量
		uint32_t spot_light_count(void) const;

		//=== 原光照域接口方法（签名原样照抄）===
		//设置距离雾
		void fog_set(const float color[3], float density, float start, float end);
		//设置指数平方雾
		void fog_exp2_set(const float color[3], float density, float start);
		//读取当前雾模式
		uint32_t fog_mode_get(void) const;
		//设置雾的高度衰减
		void fog_height_set(float falloff);
		//关闭距离雾
		void fog_clear(void);
		//设置环境光颜色
		void ambient_color_set(const float color[3]);
		//设置半球环境光
		void ambient_ground_set(const float ground_color[3], float hemisphere);
		//设置方向光的逐光高光缩放
		void directional_specular_set(float scale);
		//方向光阴影开关
		void shadow_enable_set(bool enabled);
		//阴影是否启用
		bool shadow_enabled(void) const;
		//设置阴影深度偏置
		void shadow_bias_set(float bias);
		//当前阴影深度偏置
		float shadow_bias(void) const;
		//设置阴影软度
		void shadow_softness_set(float softness);
		//当前阴影软度
		float shadow_softness(void) const;
		//设置阴影强度
		void shadow_strength_set(float strength);
		//当前阴影强度
		float shadow_strength(void) const;
		//聚光灯阴影质量参数
		void spot_shadow_params_set(float bias, float softness, float strength);
		//当前聚光灯阴影参数
		void spot_shadow_params_get(float* bias_out, float* softness_out, float* strength_out) const;
		//聚光灯阴影序号
		void spot_shadow_index_set(int index);
		//当前聚光灯阴影序号
		int spot_shadow_index(void) const;
		//多盏聚光灯阴影开关
		void spot_shadow_all_set(bool enabled);
		//当前是否多盏聚光灯阴影
		bool spot_shadow_all(void) const;
		//逐盏聚光灯阴影开关
		bool spot_shadow_light_set(uint32_t index, bool enabled);
		//聚光阴影图集开关，返回实际生效状态
		bool spot_shadow_atlas_set(bool enabled);
		//当前聚光阴影图集是否实际生效
		bool spot_shadow_atlas(void) const;
		//PCSS 软阴影开关与预算
		void shadow_pcss_set(bool enabled, float search, float max_radius);
		//逐盏聚光灯阴影偏置缩放
		bool spot_shadow_bias_scale_set(uint32_t index, float scale);
		//逐盏聚光灯阴影质量缩放
		bool spot_shadow_quality_set(uint32_t index, float softness_scale, float strength_scale);
		//逐盏聚光灯阴影优先级
		bool spot_shadow_priority_set(uint32_t index, float priority);
		//阴影分辨率预算
		void shadow_budget_set(uint64_t bytes);
		//曝光补偿
		bool exposure_set(float ev);
		//当前曝光补偿（EV）
		float exposure_get(void) const;
		//阴影纹素尺寸上限
		void shadow_texel_max_set(float texel);
		//当前阴影纹素尺寸上限
		float shadow_texel_max(void) const;
		//级联切换带自动淡化
		void shadow_cascade_blend_auto_set(bool enabled);
		//当前是否开启级联切换带自动淡化
		bool shadow_cascade_blend_auto(void) const;
		//阴影批量提交
		void shadow_batch_submit_set(bool enabled);
		//当前是否开启阴影批量提交
		bool shadow_batch_submit(void) const;
		//聚光灯阴影偏置自校准
		void spot_shadow_bias_auto_set(bool enabled);
		//当前是否开启偏置自校准
		bool spot_shadow_bias_auto(void) const;
		//最近一帧逐瓦片有效偏置
		void spot_shadow_bias_tiles_get(float out[8]) const;
		//阴影图分辨率
		void shadow_resolution_set(uint32_t resolution);
		//当前阴影图分辨率
		uint32_t shadow_resolution(void) const;
		//阴影光源正交视锥范围
		void shadow_range_set(float half_extent, float near_plane, float far_plane);
		//当前阴影正交范围
		void shadow_range_get(float* half_extent_out, float* near_out, float* far_out) const;
		//级联阴影配置
		void shadow_cascade_set(uint32_t count, float far_scale, float split_distance);
		//当前级联配置
		void shadow_cascade_get(uint32_t* count_out, float* far_scale_out, float* split_distance_out) const;
		//级联接缝淡化
		void shadow_cascade_blend_set(float blend);
		//当前级联接缝淡化比例
		float shadow_cascade_blend(void) const;
		//累计实际执行的阴影 pass 次数
		uint64_t shadow_pass_count(void) const;

		//=== 原风格域接口方法（签名原样照抄）===
		//PBR-lite 镜面开关
		void pbr_enable_set(bool enabled);
		//PBR-lite 是否启用
		bool pbr_enabled(void) const;
		//PBR-lite 粗糙度缩放
		void pbr_roughness_set(float scale);
		//当前 PBR-lite 粗糙度缩放
		float pbr_roughness(void) const;
		//PBR 金属度
		void pbr_metallic_set(float metallic);
		//当前 PBR 金属度
		float pbr_metallic(void) const;
		//PBR 掠射高光能量修正
		void pbr_energy_set(bool enabled);
		//PBR 掠射高光能量修正是否开启
		bool pbr_energy(void) const;
		//PBR 粗糙度绝对口径
		void pbr_roughness_absolute_set(bool enabled);
		//PBR 粗糙度绝对口径是否开启
		bool pbr_roughness_absolute(void) const;
		//二次元风格化：光照色阶量化与边缘光
		void toon_set(uint32_t steps, float rim);
		//当前色阶数
		uint32_t toon_steps(void) const;
		//当前边缘光强度
		float toon_rim(void) const;
		//设置二次元暗部色调
		void toon_shadow_tint_set(float r, float g, float b);
		//读取暗部色调
		void toon_shadow_tint(float out[3]) const;
		//设置卡通高光阈值
		void toon_specular_set(float threshold);
		//读取卡通高光阈值
		float toon_specular(void) const;
		//设置反向外扩描边
		void outline_set(float width, const float color[3]);
		//关闭反向外扩描边
		void outline_clear(void);
		//当前外扩宽度
		float outline_width(void) const;
		//读取当前描边色
		void outline_color(float out[3]) const;

		//=== 迁移期内部入口（阶段 8 收口时降为私有）===
		//关闭（复位场景域状态；幂等）
		void shutdown(void);
		//迁移期：状态直取（供帧模块过渡；阶段 8 改为语义方法）
		Scene_State& state(void);
		const Scene_State& state(void) const;

	private:
		//门面自身（迁移期用于调用尚未迁出的门面方法与 impl）
		Renderer* owner = nullptr;
		//场景域状态
		std::unique_ptr<Scene_State> state_ptr;
	};
}
