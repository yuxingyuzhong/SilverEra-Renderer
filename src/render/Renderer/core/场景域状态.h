#pragma once
//依赖：渲染器内部状态.h 已提供模块类型与常量

//游戏引擎命名空间
namespace engine
{
	//场景域状态（相机、场景装配、光源、雾、风格与阴影用户配置）
	struct Scene_State
	{
		//点光源记录（普通点光与聚光共用槽位）
		struct Point_Light
		{
			//位置（世界坐标）
			float position[3] = { 0.0f, 0.0f, 0.0f };
			//线性衰减半径（> 0）
			float radius = 5.0f;
			//颜色（RGB，0..1）
			float color[3] = { 1.0f, 1.0f, 1.0f };
			//强度（≥ 0）
			float intensity = 1.0f;
			//逐光源衰减模式（0 = 跟随全局、1 = 线性、2 = 平方反比）
			uint32_t attenuation = 0;
			//逐盏高光缩放（1 = 跟随全局，0 = 不产生高光）
			float specular_scale = 1.0f;
			//逐盏高光光泽度（0 = 跟随全局）
			float shininess = 0.0f;
			//是否聚光灯
			bool is_spot = false;
			//光传播方向（单位向量；仅聚光灯有效）
			float spot_direction[3] = { 0.0f, 0.0f, -1.0f };
			//内锥角余弦
			float spot_inner_cos = 1.0f;
			//外锥角余弦
			float spot_outer_cos = 0.0f;
		};

		//旧前向路径支持的光源上限
		static constexpr uint32_t POINT_LIGHT_UBO_MAX = LIGHT_LEGACY_MAX;
		//光源登记上限（Cluster 开启时生效）
		static constexpr uint32_t POINT_LIGHT_MAX = LIGHT_RECORD_CAPACITY;

		//相机（生成每帧视图 × 投影矩阵）
		Camera camera;
		//清屏色（RGBA）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };

		//主方向光与环境光强度
		float light_direction[3] = { 0.0f, 0.0f, -1.0f };
		float light_color[3] = { 1.0f, 1.0f, 1.0f };
		float light_intensity = 1.0f;
		float light_ambient = 0.0f;
		//补光（第二盏方向光；强度 0 = 关闭）
		float fill_direction[3] = { 0.0f, 0.0f, -1.0f };
		float fill_color[3] = { 1.0f, 1.0f, 1.0f };
		float fill_intensity = 0.0f;
		//点光源数组与当前数量
		Point_Light point_lights[POINT_LIGHT_MAX];
		uint32_t point_light_count = 0;
		//全局距离衰减模式
		uint32_t attenuation_mode = 0;
		//光源选择配置与诊断
		uint32_t light_select_mode = 0;
		uint32_t light_budget = 0;
		uint32_t light_dropped = 0;
		bool light_truncated = false;
		//镜面高光与逐光缩放
		float specular_intensity = 0.0f;
		float specular_shininess = 16.0f;
		float directional_specular = 1.0f;
		float fill_specular = 1.0f;

		//环境光颜色与半球环境光
		float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
		float ambient_ground[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
		//环境光照（方向九；IBL 常量近似；默认关闭）
		float environment_color[3] = { 0.0f, 0.0f, 0.0f };
		float environment_intensity = 0.0f;
		//距离雾与高度衰减
		float fog_color[3] = { 0.05f, 0.05f, 0.10f };
		float fog_density = 0.0f;
		float fog_start = 1.0f;
		float fog_end = 10.0f;
		uint32_t fog_mode = 0;
		float fog_height_falloff = 0.0f;
		//曝光补偿 EV
		float exposure = 0.0f;

		//PBR-lite 配置
		bool pbr_enabled = false;
		float pbr_roughness = 1.0f;
		float pbr_metallic = 0.0f;
		bool pbr_energy = false;
		bool pbr_roughness_absolute = false;
		//多重散射能量补偿（方向九第 716 轮；默认关闭）
		bool pbr_multiscatter = false;
		//镜面抗锯齿（方向九第 717 轮；默认关闭）
		bool pbr_specular_aa = false;
		//二次元风格化配置
		uint32_t toon_steps = 0;
		float toon_rim = 0.0f;
		float toon_shadow_tint[3] = { 1.0f, 1.0f, 1.0f };
		float toon_specular = 0.0f;
		//反向外扩描边用户参数（管线与逐帧推送块由设备域持有）
		float outline_width = 0.0f;
		float outline_color[3] = { 0.0f, 0.0f, 0.0f };

		//方向光阴影开关与质量参数
		bool shadow_enabled = false;
		uint32_t shadow_size = 1024;
		float shadow_bias = 0.003f;
		float shadow_softness = 0.0f;
		float shadow_strength = 1.0f;
		float shadow_half_extent = 12.0f;
		float shadow_near = 1.0f;
		float shadow_far = 60.0f;
		//级联阴影用户配置
		uint32_t cascade_count = 1;
		float cascade_far_scale = 3.0f;
		float cascade_split_distance = 12.0f;
		float cascade_blend = 0.0f;
		bool cascade_blend_auto = false;
		//PCSS、分辨率预算、纹素上限与批量提交配置
		bool shadow_pcss_enabled = false;
		float shadow_pcss_search = 24.0f;
		float shadow_pcss_max = 8.0f;
		uint64_t shadow_budget_bytes = 0;
		float shadow_texel_max = 0.0f;
		bool shadow_batch_submit = false;

		//聚光灯阴影开关与质量参数
		bool spot_shadow_enabled = false;
		float spot_shadow_bias = 0.010f;
		float spot_shadow_softness = 0.0f;
		float spot_shadow_strength = 1.0f;
		int spot_shadow_index = 0;
		bool spot_shadow_all = false;
		bool spot_shadow_light_enabled[8] = { true, true, true, true, true, true, true, true };
		bool spot_shadow_bias_auto = false;
		float spot_shadow_bias_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		float spot_shadow_softness_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		float spot_shadow_strength_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		float spot_shadow_priority[32] = {};
		//图集请求配置（实际目标与生效状态由设备域持有）
		bool spot_shadow_atlas_requested = false;
	};
}
