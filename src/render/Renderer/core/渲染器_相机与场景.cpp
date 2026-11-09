#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
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

#include "渲染器内部工具.h"

namespace engine
{
	//相机：设置位置
	void Renderer_Scene::camera_position_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.position_set(x, y, z);
	}

	//相机：设置注视点
	void Renderer_Scene::camera_target_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.target_set(x, y, z);
	}

	//相机：设置视场角（弧度）
	void Renderer_Scene::camera_fov_set(float radians)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.fov_set(radians);
	}

	//相机：设置近远平面
	void Renderer_Scene::camera_near_far_set(float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.near_far_set(near_plane, far_plane);
	}

	//切换正交 / 透视投影（第一百六十四轮）
	void Renderer_Scene::camera_ortho_set(bool enabled, float ortho_height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.ortho_set(enabled, ortho_height);
	}

	//设置相机上方向（第一百八十六轮）
	void Renderer_Scene::camera_up_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//全零向量会使视图矩阵退化，整次忽略
		if (x == 0.0f && y == 0.0f && z == 0.0f)
		{
			logger.warn("渲染器:: 相机上方向全零，整次忽略");
			return;
		}
		//非有限分量按忽略处理
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
			return;
		impl->camera.up_set(x, y, z);
	}

	//设置宽高比覆盖（第一百八十六轮）
	void Renderer_Scene::camera_aspect_set(float aspect)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.aspect_override_set(aspect);
	}

	//查询当前生效宽高比（第一百八十六轮）
	float Renderer_Scene::camera_aspect_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->camera.aspect_effective_get();
	}

	//查询视场角（第一百六十四轮）
	bool Renderer_Scene::camera_fov_get(float* radians_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.fov_get(radians_out);
		return true;
	}

	//查询投影模式与正交高度（第一百六十四轮）
	bool Renderer_Scene::camera_ortho_get(bool* enabled_out, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.ortho_get(enabled_out, height_out);
		return true;
	}

	//查询指定视深处可见的垂直世界高度（第一百六十五轮）
	bool Renderer_Scene::camera_world_height_at(float view_depth, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.world_height_at_get(view_depth, height_out);
		return true;
	}

	//世界点 → 屏幕坐标（第一百四十九轮）
	bool Renderer_Scene::camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.project_to_screen(world, u_out, v_out, depth_out);
	}

	//屏幕坐标 → 世界点（第一百四十九轮）
	bool Renderer_Scene::camera_unproject(float u, float v, float depth, float world_out[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.unproject_from_screen(u, v, depth, world_out);
	}

	//世界空间 AABB 是否与视锥相交（第一百六十轮）
	bool Renderer_Scene::camera_aabb_visible(const float min[3], const float max[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交相机模块（六面保守判定）
		return impl->camera.aabb_visible(min, max);
	}

	//导出相机「视图 × 投影」矩阵（第一百六十一轮）
	bool Renderer_Scene::camera_view_proj(float out[16]) const
	{
		//未初始化或出参为空则拒绝
		if (impl == nullptr || !impl->initialized || out == nullptr)
			return false;
		//写出列主序矩阵
		impl->camera.view_proj_get(out);
		return true;
	}

	//应用场景描述（三组入参；第四十七轮）
	void Renderer_Scene::scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
		相机参数校验（处置 R-FACADE-10）
		视场角须落在 (0, π)、近平面须为正、远平面须大于近平面；任一非法即整次拒绝，
		保持既有相机与清屏色不变。用「取反比较」写法使 NaN 同样被判为非法。
		*/
		bool camera_valid = (camera.fov > 0.0f) && (camera.fov < 3.14159265f)
			&& (camera.near_plane > 0.0f) && (camera.far_plane > camera.near_plane)
			//正交模式额外要求垂直可见高度为正（否则投影矩阵退化）
			&& (!camera.ortho || camera.ortho_height > 0.0f);
		//若相机参数非法
		if (!camera_valid)
		{
			logger.warn("渲染器:: 场景相机非法（视场角 {}、近平面 {}、远平面 {}），整次拒绝",
				camera.fov, camera.near_plane, camera.far_plane);
			return;
		}
		//相机位置 / 注视点
		impl->camera.position_set(camera.position[0], camera.position[1], camera.position[2]);
		impl->camera.target_set(camera.target[0], camera.target[1], camera.target[2]);
		//视场角与近远平面
		impl->camera.fov_set(camera.fov);
		impl->camera.near_far_set(camera.near_plane, camera.far_plane);
		//投影模式（正交 / 透视；高度合法性已在上方整体校验）
		impl->camera.ortho_set(camera.ortho, camera.ortho_height);
		//上方向与宽高比覆盖（第一百八十六轮；非法值由各自入口归一）
		impl->camera.up_set(camera.up[0], camera.up[1], camera.up[2]);
		impl->camera.aspect_override_set(camera.aspect);
		//清屏色（统一走 clear_color_set：逐分量钳制到 [0,1]）
		self().clear_color_set(camera.clear_color);
		//光照（统一走 light_set：非法参数由后者整次拒绝）
		self().light_set(light.direction, light.color, light.intensity, light.ambient);
		/*
		光源选择模式与预算（第六百四十五轮；A1）
		预算 = 0 跟随路径上限（旧前向 32 盏 / 簇路径全量）；否则取用户预算与路径上限的较小值。
		候选集合 = 点光（先）＋ 聚光（后）的声明序列表，各组最多 32 条（与 Light_Data 数组容量一致）。
		*/
		self().light_select_set(static_cast<uint32_t>(light.light_select_mode), light.light_budget);
		const uint32_t path_limit = impl->cluster_enabled ? Impl::POINT_LIGHT_MAX : Impl::POINT_LIGHT_UBO_MAX;
		const uint32_t light_budget = (impl->light_budget > 0u && impl->light_budget < path_limit)
			? impl->light_budget : path_limit;
		//候选总数（点光在前、聚光在后；与登记顺序一致；第七百零一轮各组上限 16 → 32）
		const uint32_t point_total = light.point_count < 32u ? light.point_count : 32u;
		const uint32_t spot_total = light.spot_count < 32u ? light.spot_count : 32u;
		const uint32_t candidate_total = point_total + spot_total;
		/*
		入选标志（true = 本次登记该候选）
		声明顺序模式且候选未超预算时全为真，与引入前逐条等价；重要性选择时才逐候选重算。
		*/
		bool keep[64] = {};
		for (uint32_t candidate = 0; candidate < candidate_total; ++candidate)
			keep[candidate] = true;
		//若重要性选择且候选数超过预算
		if (impl->light_select_mode == 1 && candidate_total > light_budget)
		{
			//相机位置（评分距离基准）
			float camera_position[3] = {};
			impl->camera.position_get(camera_position);
			//逐候选评分
			float scores[64] = {};
			for (uint32_t candidate = 0; candidate < candidate_total; ++candidate)
			{
				//候选所属光源组与组内序号
				const bool is_spot = candidate >= point_total;
				const uint32_t index = is_spot ? candidate - point_total : candidate;
				const float* position = is_spot ? light.spot_position[index] : light.point_position[index];
				const float intensity = is_spot ? light.spot_intensity[index] : light.point_intensity[index];
				const float radius = is_spot ? light.spot_radius[index] : light.point_radius[index];
				//相机相对光源的位置分量（距离与锥角判定的公共基准）
				float offset[3] = {
					camera_position[0] - position[0],
					camera_position[1] - position[1],
					camera_position[2] - position[2],
				};
				float distance = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
				//距离权重 = 半径 /(半径 ＋ 距离)（越近越高；非法半径按 0 分）
				float proximity = 0.0f;
				if (radius > 0.0f && std::isfinite(radius) && std::isfinite(distance))
					proximity = radius / (radius + distance);
				//锥角可见性（点光恒 1；聚光按相机方向落在锥内的程度）
				float cone = 1.0f;
				if (is_spot)
				{
					cone = 0.0f;
					const float* direction = light.spot_direction[index];
					float direction_length_squared = direction[0] * direction[0]
						+ direction[1] * direction[1] + direction[2] * direction[2];
					if (direction_length_squared > 1e-12f && distance > 0.0f && std::isfinite(direction_length_squared))
					{
						//相机方向与光传播方向的夹角余弦
						float cos_angle = (direction[0] * offset[0] + direction[1] * offset[1] + direction[2] * offset[2])
							/ (std::sqrt(direction_length_squared) * distance);
						//内 / 外锥余弦（度 → 弧度）
						constexpr float degrees_to_radians = 0.017453292519943295f;
						float inner_cos = std::cos(light.spot_inner_deg[index] * degrees_to_radians);
						float outer_cos = std::cos(light.spot_outer_deg[index] * degrees_to_radians);
						//锥内为 1、锥外为 0、两者之间线性过渡
						if (inner_cos > outer_cos)
						{
							float visibility = (cos_angle - outer_cos) / (inner_cos - outer_cos);
							cone = visibility < 0.0f ? 0.0f : (visibility > 1.0f ? 1.0f : visibility);
						}
					}
				}
				//评分 = 强度 × 距离权重 × 锥角可见性（非法强度按 0 分）
				float score = (intensity > 0.0f && std::isfinite(intensity))
					? intensity * proximity * cone : 0.0f;
				scores[candidate] = std::isfinite(score) ? score : 0.0f;
			}
			//先全部置为未入选，再逐个取最高分（同分按声明序号较小者，保证同分同序）
			for (uint32_t candidate = 0; candidate < candidate_total; ++candidate)
				keep[candidate] = false;
			for (uint32_t pick = 0; pick < light_budget; ++pick)
			{
				uint32_t best = candidate_total;
				for (uint32_t candidate = 0; candidate < candidate_total; ++candidate)
					if (!keep[candidate] && (best == candidate_total || scores[candidate] > scores[best]))
						best = candidate;
				//无剩余候选则停止
				if (best == candidate_total)
					break;
				keep[best] = true;
			}
		}
		/*
		逐条登记（按声明顺序；点光段在前、聚光段在后）
		已登记数达到预算即停止（与旧实现「超出上限不再登记」等价）；
		单条非法只跳过该条并告警，未入选候选直接跳过。
		*/
		uint32_t light_registered = 0;
		for (uint32_t candidate = 0; candidate < candidate_total; ++candidate)
		{
			//已满预算则不再登记
			if (light_registered >= light_budget)
				break;
			//未入选的候选跳过
			if (!keep[candidate])
				continue;
			//按候选所属光源组登记
			if (candidate < point_total)
			{
				self().light_add_point(light.point_position[candidate], light.point_color[candidate],
					light.point_radius[candidate], light.point_intensity[candidate], light.ambient,
					light.point_attenuation[candidate], light.point_specular[candidate], light.point_shininess[candidate]);
			}
			else
			{
				const uint32_t index = candidate - point_total;
				self().light_add_spot(light.spot_position[index], light.spot_direction[index],
					light.spot_color[index], light.spot_radius[index], light.spot_intensity[index],
					light.spot_inner_deg[index], light.spot_outer_deg[index], light.ambient,
					light.spot_attenuation[index], light.spot_specular[index], light.spot_shininess[index]);
			}
			//统计实际登记成功的光源数（失败项不计入）
			if (impl->point_light_count > light_registered)
				light_registered = impl->point_light_count;
		}
		/*
		本轮淘汰数与截断标志（第六百四十五轮；A4）
		淘汰数 = 候选总数 − 实际登记数；旧前向路径下确实有光源未登记时才置截断标志。
		*/
		impl->light_dropped = candidate_total > impl->point_light_count
			? candidate_total - impl->point_light_count : 0;
		impl->light_truncated = !impl->cluster_enabled && impl->light_dropped > 0;
		self().specular_set(light.specular_intensity, light.specular_shininess);
		//距离衰减模式（非法值由 light_attenuation_set 拒绝并保持当前模式）
		self().light_attenuation_set(light.attenuation_mode);
		//距离雾（按模式分派；非法参数由 fog_set / fog_exp2_set 整次忽略）
		if (fog.mode == 1)
			self().fog_exp2_set(fog.color, fog.density, fog.start);
		else
			self().fog_set(fog.color, fog.density, fog.start, fog.end);
		//雾高度衰减（非法值由 fog_height_set 整次忽略）
		self().fog_height_set(fog.height_falloff);
		//环境光颜色（非法分量由 ambient_color_set 整次忽略）
		self().ambient_color_set(light.ambient_color);
		//半球环境光（地面颜色 ＋ 混合强度；非法颜色由 ambient_ground_set 整次忽略）
		self().ambient_ground_set(light.ambient_ground, light.ambient_hemisphere);
		//补光（第二盏方向光；强度 0 = 关闭；第五十轮含逐光高光缩放）
		self().light_fill_set(light.fill_direction, light.fill_color, light.fill_intensity, light.fill_specular);
		//方向光逐光高光缩放（第五十轮；非法值由 setter 忽略）
		self().directional_specular_set(light.directional_specular);
		//方向光阴影开关与偏置（第五十二轮）
		self().shadow_enable_set(light.shadow_enabled != 0);
		self().shadow_bias_set(light.shadow_bias);
		//阴影软度与强度（第五十三轮；非法值由 setter 整次忽略）
		self().shadow_softness_set(light.shadow_softness);
		self().shadow_strength_set(light.shadow_strength);
		//阴影图分辨率（第五十四轮；非法值由 setter 整次忽略）
		self().shadow_resolution_set(static_cast<uint32_t>(light.shadow_resolution));
		//级联接缝淡化比例（第六百五十一轮；非法值由 setter 整次忽略）
		self().shadow_cascade_blend_set(light.shadow_cascade_blend);
		/*
		级联阴影配置（第六百五十四轮）
		仅在数值变化时才调用 setter：该 setter 会把阴影图标记为待重绘，
		默认值与初始值相同 → 不触发，保持既有「静止场景复用」时序与画面逐字节不变。
		*/
		if (light.shadow_cascade_count != static_cast<int>(impl->cascade_count)
			|| light.shadow_cascade_far_scale != impl->cascade_far_scale
			|| light.shadow_cascade_split != impl->cascade_split_distance)
		{
			//级联数以无符号位传入门面（非法值由 setter 整次忽略）
			self().shadow_cascade_set(light.shadow_cascade_count < 0 ? 0u : static_cast<uint32_t>(light.shadow_cascade_count),
				light.shadow_cascade_far_scale, light.shadow_cascade_split);
		}
		/*
		PCSS 软阴影（第六百五十七轮）
		仅影响着色器采样，不重建阴影图；非法值由 setter 整次忽略。
		*/
		self().shadow_pcss_set(light.shadow_pcss != 0, light.shadow_pcss_search, light.shadow_pcss_max);
		//逐盏聚光灯阴影偏置缩放（第六百五十八轮；按前向槽位，默认 1）
		for (std::size_t index = 0; index < 8; ++index)
			self().spot_shadow_bias_scale_set(static_cast<uint32_t>(index), light.spot_shadow_bias_scale[index]);
		//逐盏聚光灯阴影质量缩放（第七百零八轮；按前向槽位，默认 1）
		for (std::size_t index = 0; index < 8; ++index)
			self().spot_shadow_quality_set(static_cast<uint32_t>(index), light.spot_shadow_softness_scale[index],
				light.spot_shadow_strength_scale[index]);
		//逐盏聚光灯阴影优先级（第七百零九轮；按光源槽位 0..31，默认 0）
		for (std::size_t index = 0; index < 32; ++index)
			self().spot_shadow_priority_set(static_cast<uint32_t>(index), light.spot_shadow_priority[index]);
		/*
		阴影分辨率预算（第六百五十九轮）
		预算 > 0 时按 RGBA8 折算可用边长（向下取 256 倍数、钳 256..4096），与请求分辨率取较小值；
		预算 0（默认）不干预，与既有逐字节一致。
		*/
		self().shadow_budget_set(light.shadow_budget_bytes);
		//曝光补偿（第六百六十轮；EV，0 = 关闭，非有限值由 setter 忽略）
		self().exposure_set(light.exposure);
			/*
			级联纹素钳制与自动淡化（第六百六十一轮）
			纹素上限 0 与自动淡化关闭均为默认，行为与引入前逐字节一致；非法值由 setter 整次忽略。
			*/
			self().shadow_texel_max_set(light.shadow_texel_max);
			self().shadow_cascade_blend_auto_set(light.shadow_cascade_blend_auto != 0);
			//阴影批量提交（第六百六十九轮；默认关，开启后一次提交全部阴影段）
			self().shadow_batch_submit_set(light.shadow_batch_submit != 0);
		if (light.shadow_budget_bytes > 0)
		{
			//参与预算的阴影图单位数（方向光级联 ＋ 聚光瓦片，至少 1）
			uint32_t units = 0;
			if (light.shadow_enabled != 0)
				units += light.shadow_cascade_count > 0
					? (light.shadow_cascade_count < 3 ? static_cast<uint32_t>(light.shadow_cascade_count) : 3u) : 1u;
			if (light.spot_shadow_enabled != 0)
				units += light.spot_shadow_all != 0
					? (light.spot_count == 0 ? 1u : (light.spot_count > 8 ? 8u : light.spot_count)) : 1u;
			if (units == 0)
				units = 1;
			//每单位可用像素数（RGBA8 = 4 字节/像素）
			uint64_t pixels_per_unit = static_cast<uint64_t>(light.shadow_budget_bytes) / (static_cast<uint64_t>(units) * 4u);
			//整数平方根（逐位逼近）
			uint32_t side = 0;
			for (uint32_t bit = 4096u; bit > 0; bit >>= 1)
			{
				uint64_t trial = static_cast<uint64_t>(side) + bit;
				if (trial * trial <= pixels_per_unit)
					side += bit;
			}
			//向下取 256 倍数并钳到 [256, 4096]
			side = side / 256u * 256u;
			if (side < 256u)
				side = 256u;
			if (side > 4096u)
				side = 4096u;
			//门面仅接受 512 / 1024 / 2048：取不超过计算值的最大档（不足 512 时取最低档）
			uint32_t budget_side = 512u;
			if (side >= 2048u)
				budget_side = 2048u;
			else if (side >= 1024u)
				budget_side = 1024u;
			//与请求分辨率取较小值
			if (budget_side < light.shadow_resolution)
				self().shadow_resolution_set(budget_side);
		}
		//聚光灯阴影开关（第四百七十四轮；第四百八十四轮起不再强制开启方向光阴影）
		impl->spot_shadow_enabled = light.spot_shadow_enabled != 0;
		if (impl->spot_shadow_enabled)
		{
			//借用 shadow_enable_set 的管线懒登记，登记后恢复方向光阴影开关原值
			const bool directional_enabled = impl->shadow_enabled;
			self().shadow_enable_set(true);
			impl->shadow_enabled = directional_enabled;
		}
		//聚光灯阴影独立质量参数（第四百七十六轮；非法值由 setter 整次忽略）
		self().spot_shadow_params_set(light.spot_shadow_bias, light.spot_shadow_softness, light.spot_shadow_strength);
		//聚光灯阴影序号（第四百七十九轮；非法值由 setter 整次忽略）
		self().spot_shadow_index_set(light.spot_shadow_index);
		/*
		聚光灯阴影偏置自校准（第五百二十九轮）
		仅在开关值变化时调用 setter：该 setter 会把阴影图标记为待重绘，
		默认（0 与初始 false 相同）不触发，保持既有重绘时序与画面逐字节不变。
		*/
		if (impl->spot_shadow_bias_auto != (light.spot_shadow_bias_auto != 0))
			self().spot_shadow_bias_auto_set(light.spot_shadow_bias_auto != 0);
		//多盏聚光灯阴影（第五百零七轮；前 4 盏各自投影，关闭时沿用单盏语义）
		impl->spot_shadow_all = light.spot_shadow_all != 0;
		//逐盏聚光灯阴影开关（第六百四十八轮；按前向槽位 0..7，默认全开）
		for (std::size_t index = 0; index < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++index)
			impl->spot_shadow_light_enabled[index] = light.spot_shadow_on[index] != 0;
		/*
		聚光阴影图集（第六百七十轮；仅请求值变化时切换）
		默认 0 不触发，保持既有绑定与阴影重绘时序，画面与引入前逐字节一致。
		*/
		if (impl->spot_shadow_atlas_requested != (light.spot_shadow_atlas != 0))
			self().spot_shadow_atlas_set(light.spot_shadow_atlas != 0);
		/*
		多盏瓦片按需建立（第五百零七轮；第五百一十一轮抽为共用方法；第五百一十三轮扩为 8 瓦片；
		第六百四十四轮改为按「聚光灯所在槽位的最大下标」建立，超出 8 槽时回落按数量建立）
		槽位可一一对应时瓦片号 = 光源槽位，故需建到最靠后的聚光灯槽位；否则按聚光灯数量建立（与旧口径一致）。
		创建失败仅告警，该瓦片按「无聚光阴影」处理，不影响其余瓦片。
		*/
		if (impl->spot_shadow_all)
		{
			/*
			所需瓦片数（第六百四十四轮）
			槽位可一一对应时建到「最靠后的聚光灯槽位 + 1」；总光源超过 8 槽而回落压缩口径时建到「聚光灯数量」；
			两者都至少建 1 张占位。
			*/
			std::size_t tile_limit = 0;
			bool slot_mapping = true;
			std::size_t spot_total = 0;
			for (std::size_t slot = 0; slot < impl->point_light_count; ++slot)
			{
				//非聚光灯跳过
				if (!impl->point_lights[slot].is_spot)
					continue;
				++spot_total;
				if (slot >= Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)
					slot_mapping = false;
				else
					tile_limit = slot + 1;
			}
			//回落压缩口径：瓦片数 = 聚光灯数量（上限 8）
			if (!slot_mapping)
				tile_limit = spot_total < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES ? spot_total : Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES;
			spot_shadow_tiles_ensure_impl(impl.get(), tile_limit > 0 ? tile_limit : 1u);
		}
		//PBR-lite 镜面（第六十四轮；非法缩放由 setter 整次忽略）
		self().pbr_enable_set(light.pbr_enabled != 0);
		self().pbr_roughness_set(light.pbr_roughness);
		//PBR 金属度（第六十五轮；非法值由 setter 整次忽略）
		self().pbr_metallic_set(light.pbr_metallic);
		//PBR 掠射高光能量修正（第六百七十二轮；默认关闭）
		impl->pbr_energy = light.pbr_energy != 0;
		//PBR 粗糙度绝对口径（第七百轮；默认关闭）
		impl->pbr_roughness_absolute = light.pbr_roughness_absolute != 0;
		//二次元风格化（第一百七十四轮；非法参数由 setter 归一）
		self().toon_set(light.toon_steps, light.toon_rim);
		//二次元暗部色调（第一百七十八轮）
		self().toon_shadow_tint_set(light.toon_shadow_tint[0], light.toon_shadow_tint[1], light.toon_shadow_tint[2]);
		//二次元卡通高光（第一百七十九轮；非法值由 setter 归一）
		self().toon_specular_set(light.toon_specular);
		//反向外扩描边（第二百二十八轮；非法值由 setter 归一）
		self().outline_set(light.outline_width, light.outline_color);
		//若点光源数量超过上限则告警（声明顺序模式；重要性选择模式已主动择优，不再告警）
		if (impl->light_select_mode == 0 && light.point_count > Impl::POINT_LIGHT_UBO_MAX)
			logger.warn("渲染器:: 场景点光源数量 {} 超过上限 {}，仅登记前 {} 盏",
				light.point_count, Impl::POINT_LIGHT_UBO_MAX, Impl::POINT_LIGHT_UBO_MAX);
		//若聚光灯数量超过上限则告警（点光源与聚光灯共用 32 盏上限）
		if (impl->light_select_mode == 0 && light.spot_count > Impl::POINT_LIGHT_UBO_MAX)
			logger.warn("渲染器:: 场景聚光灯数量 {} 超过上限 {}，仅登记前 {} 盏（与点光源共用上限）",
				light.spot_count, Impl::POINT_LIGHT_UBO_MAX, Impl::POINT_LIGHT_UBO_MAX);
	}

	//应用场景描述（兼容扁平聚合；内部拆成三组后转发）
	void Renderer_Scene::scene_apply(const Scene_Data& scene)
	{
		//相机组
		Camera_Data camera{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			camera.position[index] = scene.position[index];
			camera.target[index] = scene.target[index];
		}
		camera.fov = scene.fov;
		camera.near_plane = scene.near_plane;
		camera.far_plane = scene.far_plane;
		camera.ortho = scene.ortho;
		camera.ortho_height = scene.ortho_height;
		//上方向与宽高比覆盖（第一百八十六轮）
		for (std::size_t index = 0; index < 3; ++index)
			camera.up[index] = scene.up[index];
		camera.aspect = scene.aspect;
		for (std::size_t index = 0; index < 4; ++index)
			camera.clear_color[index] = scene.clear_color[index];
		//光照组
		Light_Data light{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.direction[index] = scene.light_direction[index];
			light.color[index] = scene.light_color[index];
			light.ambient_color[index] = scene.ambient_color[index];
			light.ambient_ground[index] = scene.ambient_ground[index];
			light.fill_direction[index] = scene.fill_direction[index];
			light.fill_color[index] = scene.fill_color[index];
		}
		light.ambient_hemisphere = scene.ambient_hemisphere;
		light.fill_intensity = scene.fill_intensity;
		light.directional_specular = scene.directional_specular;
		light.fill_specular = scene.fill_specular;
		light.shadow_enabled = scene.shadow_enabled;
		light.shadow_bias = scene.shadow_bias;
		light.shadow_softness = scene.shadow_softness;
		light.shadow_strength = scene.shadow_strength;
		light.shadow_resolution = scene.shadow_resolution;
		//级联接缝淡化比例（第六百五十一轮）
		light.shadow_cascade_blend = scene.shadow_cascade_blend;
		//级联阴影配置（第六百五十四轮）
		light.shadow_cascade_count = scene.shadow_cascade_count;
		light.shadow_cascade_far_scale = scene.shadow_cascade_far_scale;
		light.shadow_cascade_split = scene.shadow_cascade_split;
		//PCSS 软阴影（第六百五十七轮）
		light.shadow_pcss = scene.shadow_pcss;
		light.shadow_pcss_search = scene.shadow_pcss_search;
		light.shadow_pcss_max = scene.shadow_pcss_max;
		//逐盏聚光灯阴影偏置缩放（第六百五十八轮）
		for (std::size_t index = 0; index < 8; ++index)
			light.spot_shadow_bias_scale[index] = scene.spot_shadow_bias_scale[index];
		//逐盏聚光灯阴影质量缩放（第七百零八轮）
		for (std::size_t index = 0; index < 8; ++index)
		{
			light.spot_shadow_softness_scale[index] = scene.spot_shadow_softness_scale[index];
			light.spot_shadow_strength_scale[index] = scene.spot_shadow_strength_scale[index];
		}
		//逐盏聚光灯阴影优先级（第七百零九轮）
		for (std::size_t index = 0; index < 32; ++index)
			light.spot_shadow_priority[index] = scene.spot_shadow_priority[index];
		//阴影分辨率预算（第六百五十九轮）
		light.shadow_budget_bytes = scene.shadow_budget_bytes;
		//曝光补偿（第六百六十轮）
		light.exposure = scene.exposure;
		//级联纹素钳制与自动淡化（第六百六十一轮）
		light.shadow_texel_max = scene.shadow_texel_max;
		light.shadow_cascade_blend_auto = scene.shadow_cascade_blend_auto;
		//阴影批量提交（第六百六十九轮）
		light.shadow_batch_submit = scene.shadow_batch_submit;
		//PBR-lite 镜面（第六十四轮）
		light.pbr_enabled = scene.pbr_enabled;
		light.pbr_roughness = scene.pbr_roughness;
		//二次元风格化（第一百七十四轮）
		light.toon_steps = scene.toon_steps;
		light.toon_rim = scene.toon_rim;
		//二次元暗部色调（第一百七十八轮）
		for (std::size_t index = 0; index < 3; ++index)
			light.toon_shadow_tint[index] = scene.toon_shadow_tint[index];
		//二次元卡通高光（第一百七十九轮）
		light.toon_specular = scene.toon_specular;
		//二次元反向外扩描边（第二百二十八轮）
		light.outline_width = scene.outline_width;
		for (std::size_t index = 0; index < 3; ++index)
			light.outline_color[index] = scene.outline_color[index];
		//PBR 金属度（第六十五轮）
		//PBR 金属度（第六十五轮）
		light.pbr_metallic = scene.pbr_metallic;
		//PBR 掠射高光能量修正（第六百七十二轮）
		light.pbr_energy = scene.pbr_energy;
		//PBR 粗糙度绝对口径（第七百轮）
		light.pbr_roughness_absolute = scene.pbr_roughness_absolute;
		light.intensity = scene.light_intensity;
		light.ambient = scene.light_ambient;
		light.point_count = scene.light_count;
		for (uint32_t index = 0; index < Impl::POINT_LIGHT_UBO_MAX; ++index)
		{
			for (std::size_t each = 0; each < 3; ++each)
			{
				light.point_position[index][each] = scene.point_light_position[index][each];
				light.point_color[index][each] = scene.point_light_color[index][each];
				light.spot_position[index][each] = scene.spot_light_position[index][each];
				light.spot_direction[index][each] = scene.spot_light_direction[index][each];
				light.spot_color[index][each] = scene.spot_light_color[index][each];
			}
			light.point_radius[index] = scene.point_light_radius[index];
			light.point_intensity[index] = scene.point_light_intensity[index];
			light.point_attenuation[index] = scene.point_light_attenuation[index];
			light.point_specular[index] = scene.point_light_specular[index];
			light.point_shininess[index] = scene.point_light_shininess[index];
			light.spot_radius[index] = scene.spot_light_radius[index];
			light.spot_intensity[index] = scene.spot_light_intensity[index];
			light.spot_inner_deg[index] = scene.spot_light_inner_deg[index];
			light.spot_outer_deg[index] = scene.spot_light_outer_deg[index];
			light.spot_attenuation[index] = scene.spot_light_attenuation[index];
			light.spot_specular[index] = scene.spot_light_specular[index];
			light.spot_shininess[index] = scene.spot_light_shininess[index];
		}
		light.specular_intensity = scene.specular_intensity;
		light.specular_shininess = scene.specular_shininess;
		light.spot_count = scene.spot_light_count;
		light.attenuation_mode = scene.attenuation_mode;
		//光源选择模式与预算（第六百四十五轮）
		light.light_select_mode = scene.light_select_mode;
		light.light_budget = scene.light_budget;
		//逐盏聚光灯阴影开关（第六百四十八轮）
		for (std::size_t index = 0; index < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++index)
			light.spot_shadow_on[index] = scene.spot_shadow_on[index];
		//聚光阴影图集（第六百七十轮）
		light.spot_shadow_atlas = scene.spot_shadow_atlas;
		//雾组
		Fog_Data fog{};
		for (std::size_t index = 0; index < 3; ++index)
			fog.color[index] = scene.fog_color[index];
		fog.density = scene.fog_density;
		fog.start = scene.fog_start;
		fog.end = scene.fog_end;
		fog.mode = scene.fog_mode;
		fog.height_falloff = scene.fog_height_falloff;
		//转发三组入参
		self().scene_apply(camera, light, fog);
	}

	//设置方向光与环境光（第二十九轮新增）
	void Renderer_Scene::light_set(const float direction[3], const float color[3], float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验
		方向长度须非零（否则无法归一化）、颜色各分量须落在 [0,1]、强度须非负且有限、环境光须落在 [0,1]；
		用「取反比较」写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 光照参数非法（方向 ({}, {}, {})、强度 {}、环境光 {}），整次拒绝",
				direction[0], direction[1], direction[2], intensity, ambient);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->light_direction[index] = direction[index] * inverse_length;
			impl->light_color[index] = color[index];
		}
		impl->light_intensity = intensity;
		impl->light_ambient = ambient;
		//切回「仅方向光」（清空点光源；第三十二轮）
		impl->point_light_count = 0;
	}

	//设置点光源（第三十一轮新增）
	void Renderer_Scene::light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return;
		/*
		参数校验（与 light_set 同口径，另加「半径须为正且有限」）
		用取反比较写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、环境光 {}），整次拒绝",
				position[0], position[1], position[2], radius, intensity, ambient);
			return;
		}
		//写入点光源参数（位置即「光位置」，由着色器按模式选择语义）
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->point_lights[0].position[index] = position[index];
			impl->point_lights[0].color[index] = color[index];
			//方向光颜色保持既有值
		}
		impl->point_lights[0].radius = radius;
		impl->point_lights[0].intensity = intensity;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		impl->point_lights[0].is_spot = false;
		impl->point_light_count = 1;
		impl->light_ambient = ambient;
		//关闭方向光（第三十二轮：单点光源入口等价于「仅一个点光源」）
		impl->light_intensity = 0.0f;
	}

	//追加一个点光源（第三十二轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer_Scene::light_add_point(const float position[3], const float color[3], float radius, float intensity,
		float ambient, uint32_t attenuation, float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return false;
		/*
		参数校验（与 light_point_set 同口径）＋ 容量校验
		任一非法或已满即返回假并告警，既有光源保持不变。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 追加点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}），拒绝",
				position[0], position[1], position[2], radius, intensity);
			return false;
		}
		//登记上限：Cluster 开启为全量容量，关闭为旧前向路径 8 盏（第二百八十轮）
		const uint32_t light_limit = impl->cluster_enabled ? Impl::POINT_LIGHT_MAX : Impl::POINT_LIGHT_UBO_MAX;
		//若已达上限
		if (impl->point_light_count >= light_limit)
		{
			//关闭 Cluster 时的截断须在统计中可见（不静默）
			if (!impl->cluster_enabled)
				impl->light_truncated = true;
			logger.warn("渲染器:: 点光源数量已达上限（{}），拒绝追加", light_limit);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		light.is_spot = false;
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//追加一个聚光灯（第三十四轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer_Scene::light_add_spot(const float position[3], const float direction[3], const float color[3],
		float radius, float intensity, float inner_deg, float outer_deg, float ambient, uint32_t attenuation,
		float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || direction == nullptr || color == nullptr)
			return false;
		/*
		参数校验（点光源同口径 ＋ 方向非退化 ＋ 锥角关系）
		方向长度须非零；锥角须满足 0 < 内角 < 外角 ≤ 90；
		用取反比较写法使 NaN 同样被判为非法，任一非法即拒绝并保持既有光源。
		*/
		float direction_length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (direction_length_squared > 1e-12f) && (radius > 0.0f) && (radius <= 1e6f)
			&& (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			&& (inner_deg > 0.0f) && (inner_deg < outer_deg) && (outer_deg <= 90.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 聚光灯参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、内角 {}、外角 {}），拒绝",
				position[0], position[1], position[2], radius, intensity, inner_deg, outer_deg);
			return false;
		}
		//登记上限：Cluster 开启为全量容量，关闭为旧前向路径 8 盏（第二百八十轮）
		const uint32_t light_limit = impl->cluster_enabled ? Impl::POINT_LIGHT_MAX : Impl::POINT_LIGHT_UBO_MAX;
		//若已达上限
		if (impl->point_light_count >= light_limit)
		{
			//关闭 Cluster 时的截断须在统计中可见（不静默）
			if (!impl->cluster_enabled)
				impl->light_truncated = true;
			logger.warn("渲染器:: 光源数量已达上限（{}），拒绝追加聚光灯", light_limit);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		float inverse_length = 1.0f / std::sqrt(direction_length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
			//方向归一化（内部恒存单位向量）
			light.spot_direction[index] = direction[index] * inverse_length;
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		light.is_spot = true;
		//锥角转余弦（度 → 弧度；内角更小故其余弦更大）
		constexpr float degrees_to_radians = 0.017453292519943295f;
		light.spot_inner_cos = std::cos(inner_deg * degrees_to_radians);
		light.spot_outer_cos = std::cos(outer_deg * degrees_to_radians);
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//清空全部点光源（第三十二轮新增）
	void Renderer_Scene::lights_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//清空计数并恢复方向光默认强度（方向 / 颜色 / 环境光保持既有值）
		impl->point_light_count = 0;
		//清空截断标志（第二百八十轮）
		impl->light_truncated = false;
		//清空淘汰数（第六百四十五轮）
		impl->light_dropped = 0;
		impl->light_intensity = 1.0f;
	}

	//设置光源选择模式与预算（第六百四十五轮）
	void Renderer_Scene::light_select_set(uint32_t mode, uint32_t budget)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//模式仅接受 0 / 1（其它取值按 0，保持旧口径）
		impl->light_select_mode = mode == 1u ? 1u : 0u;
		//预算钳到容量上限（0 = 跟随路径上限）
		impl->light_budget = budget > Impl::POINT_LIGHT_MAX ? Impl::POINT_LIGHT_MAX : budget;
	}

	//已登记点光源数量（第九十二轮）
	uint32_t Renderer_Scene::point_light_count(void) const
	{
		//未初始化视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->point_light_count;
	}

	//读取第 index 盏点光源的状态快照（第九十二轮）
	bool Renderer_Scene::point_light_state_get(uint32_t index, Point_Light_State& state_out) const
	{
		//未初始化或越界返回假（出参保持不变）
		if (impl == nullptr || !impl->initialized || index >= impl->point_light_count
			|| index >= Impl::POINT_LIGHT_MAX)
			return false;
		//内部记录 → 公开快照
		const Impl::Point_Light& light = impl->point_lights[index];
		state_out = Point_Light_State{};
		for (std::size_t each = 0; each < 3; ++each)
		{
			state_out.position[each] = light.position[each];
			state_out.color[each] = light.color[each];
			state_out.spot_direction[each] = light.spot_direction[each];
		}
		state_out.radius = light.radius;
		state_out.intensity = light.intensity;
		state_out.attenuation = light.attenuation;
		state_out.specular_scale = light.specular_scale;
		state_out.shininess = light.shininess;
		state_out.is_spot = light.is_spot;
		state_out.spot_inner_cos = light.spot_inner_cos;
		state_out.spot_outer_cos = light.spot_outer_cos;
		return true;
	}

	//当前光源记录总数（主光 ＋ 补光 ＋ 逐盏；第二百六十五轮）
	uint32_t Renderer_Scene::light_record_count(void) const
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return 2u + impl->point_light_count;
	}

	/*
	光源记录快照（第二百六十五轮）
	索引 0 = 主方向光、1 = 补光（强度 0 表示关闭），其后按登记顺序写点光 / 聚光。
	*/
	uint32_t Renderer_Scene::light_records_get(Light_Record* out, uint32_t capacity) const
	{
		//未初始化 / 出参缺失
		if (impl == nullptr || !impl->initialized || out == nullptr || capacity == 0)
			return 0;
		uint32_t written = 0;
		//写入一条记录（容量不足则只计数不写）
		auto push = [&](const Light_Record& record)
		{
			//容量内则写出
			if (written < capacity)
				out[written] = record;
			++written;
		};
		//主方向光
		{
			Light_Record record;
			record.color_intensity[0] = impl->light_color[0];
			record.color_intensity[1] = impl->light_color[1];
			record.color_intensity[2] = impl->light_color[2];
			record.color_intensity[3] = impl->light_intensity;
			record.direction[0] = impl->light_direction[0];
			record.direction[1] = impl->light_direction[1];
			record.direction[2] = impl->light_direction[2];
			record.cone[3] = impl->directional_specular;
			record.type = static_cast<uint32_t>(Light_Type::Directional);
			record.flags = 1u;
			push(record);
		}
		//补光（强度 0 = 关闭，仍占一条以保持索引稳定）
		{
			Light_Record record;
			record.color_intensity[0] = impl->fill_color[0];
			record.color_intensity[1] = impl->fill_color[1];
			record.color_intensity[2] = impl->fill_color[2];
			record.color_intensity[3] = impl->fill_intensity;
			record.direction[0] = impl->fill_direction[0];
			record.direction[1] = impl->fill_direction[1];
			record.direction[2] = impl->fill_direction[2];
			record.cone[3] = impl->fill_specular;
			record.type = static_cast<uint32_t>(Light_Type::Directional);
			record.flags = impl->fill_intensity > 0.0f ? 1u : 0u;
			push(record);
		}
		//逐盏点光 / 聚光
		for (uint32_t index = 0; index < impl->point_light_count && index < Impl::POINT_LIGHT_MAX; ++index)
		{
			const Impl::Point_Light& light = impl->point_lights[index];
			Light_Record record;
			record.pos_radius[0] = light.position[0];
			record.pos_radius[1] = light.position[1];
			record.pos_radius[2] = light.position[2];
			record.pos_radius[3] = light.radius;
			record.color_intensity[0] = light.color[0];
			record.color_intensity[1] = light.color[1];
			record.color_intensity[2] = light.color[2];
			record.color_intensity[3] = light.intensity;
			//聚光方向（点光为 0）
			if (light.is_spot)
			{
				record.direction[0] = light.spot_direction[0];
				record.direction[1] = light.spot_direction[1];
				record.direction[2] = light.spot_direction[2];
			}
			record.cone[0] = light.spot_inner_cos;
			record.cone[1] = light.spot_outer_cos;
			record.cone[2] = static_cast<float>(light.attenuation);
			record.cone[3] = light.specular_scale;
			record.shininess = light.shininess;
			record.type = static_cast<uint32_t>(light.is_spot ? Light_Type::Spot : Light_Type::Point);
			record.flags = 1u;
			//前向路径槽位（第四百八十八轮；供 Cluster 路径按同一序号定位聚光阴影）
			record.reserved = static_cast<float>(index);
			push(record);
		}
		return written < capacity ? written : capacity;
	}

	//设置镜面高光参数（第三十一轮新增）
	void Renderer_Scene::specular_set(float intensity, float shininess)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验（取反比较使 NaN 判非法）
		if (!(intensity >= 0.0f && intensity <= 1e6f) || !(shininess >= 1.0f && shininess <= 1e6f))
		{
			logger.warn("渲染器:: 高光参数非法（强度 {}、光泽度 {}），整次拒绝", intensity, shininess);
			return;
		}
		impl->specular_intensity = intensity;
		impl->specular_shininess = shininess;
	}

	//设置距离衰减模式（第三十六轮新增）
	bool Renderer_Scene::light_attenuation_set(uint32_t mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//仅接受线性（0）与平方反比（1）
		if (mode > 1)
		{
			logger.warn("渲染器:: 距离衰减模式非法（{}），仅接受 0（线性）/ 1（平方反比）", mode);
			return false;
		}
		impl->attenuation_mode = mode;
		return true;
	}

	//读取当前距离衰减模式（第三十六轮新增）
	uint32_t Renderer_Scene::light_attenuation_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->attenuation_mode;
	}

}
