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

#include "渲染器内部工具.h"

namespace engine
{
	void Render_Scene::spot_shadow_atlas_request_clear(void)
	{
		//仅由设备域 initialized 关闭流程调用
		(*state_ptr).spot_shadow_atlas_requested = false;
	}

	bool Render_Scene::spot_shadow_atlas_requested(void) const
	{
		return state_ptr != nullptr && state_ptr->spot_shadow_atlas_requested;
	}

	//设置距离雾（第三十八轮新增）

	void Render_Scene::spot_shadow_params_set(float bias, float softness, float strength)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//三项范围校验（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f) || !(softness >= 0.0f && softness <= 1.0f)
			|| !(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 聚光灯阴影参数非法（{} / {} / {}），整次忽略", bias, softness, strength);
			return;
		}
		(*state_ptr).spot_shadow_bias = bias;
		(*state_ptr).spot_shadow_softness = softness;
		(*state_ptr).spot_shadow_strength = strength;
	}

	void Render_Scene::spot_shadow_params_get(float* bias_out, float* softness_out, float* strength_out) const
	{
		//未初始化按默认值输出
		const bool ready = owner != nullptr && owner->device().valid();
		//偏置
		if (bias_out != nullptr)
			*bias_out = ready ? (*state_ptr).spot_shadow_bias : 0.010f;
		//软度
		if (softness_out != nullptr)
			*softness_out = ready ? (*state_ptr).spot_shadow_softness : 0.0f;
		//强度
		if (strength_out != nullptr)
			*strength_out = ready ? (*state_ptr).spot_shadow_strength : 1.0f;
	}

	void Render_Scene::spot_shadow_index_set(int index)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//序号须落在 0..7（取反比较使 NaN 判非法；int 入参无 NaN，仍保留统一写法）
		if (!(index >= 0 && index < static_cast<int>(Device_State::SPOT_SHADOW_TILES)))
		{
			logger.warn("渲染器:: 聚光灯阴影序号非法（{}），整次忽略", index);
			return;
		}
		(*state_ptr).spot_shadow_index = index;
	}

	int Render_Scene::spot_shadow_index(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->device().valid() ? (*state_ptr).spot_shadow_index : 0;
	}

	void Render_Scene::spot_shadow_all_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//与原值一致则无需处理
		if ((*state_ptr).spot_shadow_all == enabled)
			return;
		(*state_ptr).spot_shadow_all = enabled;
		//开启：按槽位 / 压缩口径补齐瓦片（第六百四十四轮；无聚光时至少建瓦片 1 占位）
		if (enabled)
		{
			//所需瓦片数（与帧内口径一致）
			std::size_t tile_limit = 0;
			bool slot_mapping = true;
			std::size_t spot_total = 0;
			for (std::size_t slot = 0; slot < (*state_ptr).point_light_count; ++slot)
			{
				if (!(*state_ptr).point_lights[slot].is_spot)
					continue;
				++spot_total;
				if (slot >= Device_State::SPOT_SHADOW_TILES)
					slot_mapping = false;
				else
					tile_limit = slot + 1;
			}
			if (!slot_mapping)
				tile_limit = spot_total < Device_State::SPOT_SHADOW_TILES ? spot_total : Device_State::SPOT_SHADOW_TILES;
			owner->device().spot_shadow_tiles_ensure( tile_limit > 0 ? tile_limit : 1u);
		}
		//矩阵与槽位映射随下一帧重算，旧阴影图不再可信
		owner->device().shadow_invalidate();
		logger.info("渲染器:: 多盏聚光灯阴影已{}", enabled ? "开启" : "关闭");
	}

	bool Render_Scene::spot_shadow_atlas_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//与请求一致则无需处理
		if ((*state_ptr).spot_shadow_atlas_requested == enabled)
			return owner->device().spot_shadow_atlas_active();
		(*state_ptr).spot_shadow_atlas_requested = enabled;
		//重建图集并重绑
		owner->device().spot_shadow_atlas_apply();
		logger.info("渲染器:: 聚光阴影图集已{}", owner->device().spot_shadow_atlas_active() ? "开启" : "关闭");
		return owner->device().spot_shadow_atlas_active();
	}

	bool Render_Scene::spot_shadow_atlas(void) const
	{
		//未初始化返回假
		return owner != nullptr && owner->device().valid() ? owner->device().spot_shadow_atlas_active() : false;
	}

	bool Render_Scene::spot_shadow_light_set(uint32_t index, bool enabled)
	{
		//若尚未初始化或槽位越界
		if (owner == nullptr || !owner->device().valid() || index >= Device_State::SPOT_SHADOW_TILES)
			return false;
		//与原值一致则无需重绘
		if ((*state_ptr).spot_shadow_light_enabled[index] == enabled)
			return true;
		(*state_ptr).spot_shadow_light_enabled[index] = enabled;
		//旧阴影图不再可信（该槽位可能不再投影或重新投影）
		owner->device().shadow_invalidate();
		return true;
	}

	bool Render_Scene::spot_shadow_bias_scale_set(uint32_t index, float scale)
	{
		//若尚未初始化或槽位越界
		if (owner == nullptr || !owner->device().valid() || index >= 8)
			return false;
		//非有限值忽略（保持原值）
		if (!std::isfinite(scale))
			return false;
		(*state_ptr).spot_shadow_bias_scale[index] = scale < 0.1f ? 0.1f : (scale > 10.0f ? 10.0f : scale);
		return true;
	}

	bool Render_Scene::spot_shadow_quality_set(uint32_t index, float softness_scale, float strength_scale)
	{
		//若尚未初始化或槽位越界
		if (owner == nullptr || !owner->device().valid() || index >= 8)
			return false;
		//非有限值整次忽略（保持原值）
		if (!std::isfinite(softness_scale) || !std::isfinite(strength_scale))
			return false;
		(*state_ptr).spot_shadow_softness_scale[index] =
			softness_scale < 0.25f ? 0.25f : (softness_scale > 4.0f ? 4.0f : softness_scale);
		(*state_ptr).spot_shadow_strength_scale[index] =
			strength_scale < 0.0f ? 0.0f : (strength_scale > 1.0f ? 1.0f : strength_scale);
		return true;
	}

	bool Render_Scene::spot_shadow_priority_set(uint32_t index, float priority)
	{
		//若尚未初始化或槽位越界
		if (owner == nullptr || !owner->device().valid() || index >= 32)
			return false;
		//非有限值忽略（保持原值）
		if (!std::isfinite(priority))
			return false;
		(*state_ptr).spot_shadow_priority[index] = priority;
		return true;
	}

	bool Render_Scene::spot_shadow_all(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->device().valid() ? (*state_ptr).spot_shadow_all : false;
	}

	void Render_Scene::spot_shadow_bias_auto_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).spot_shadow_bias_auto = enabled;
		//逐瓦片偏置随下一帧重算，旧阴影图不再可信
		owner->device().shadow_invalidate();
		logger.info("渲染器:: 聚光灯阴影偏置自校准已{}", enabled ? "开启" : "关闭");
	}

	bool Render_Scene::spot_shadow_bias_auto(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->device().valid() ? (*state_ptr).spot_shadow_bias_auto : false;
	}

	void Render_Scene::spot_shadow_bias_tiles_get(float out[8]) const
	{
		//输出数组必须有效
		if (out == nullptr)
			return;
		//未初始化写默认偏置
		if (owner == nullptr || !owner->device().valid())
		{
			for (std::size_t index = 0; index < 8; ++index)
				out[index] = 0.010f;
			return;
		}
		owner->device().shadow_bias_tiles_get(out);
	}

	void Render_Scene::directional_shadow_frame_build(Scene_Shadow_Frame& state) const
	{
		/*
		⑤a 光源空间矩阵（第五十二轮）
		无论阴影是否开启都先算好并写入 UBO，使开关切换可即时生效。
		以方向光传播方向构造正交视锥（覆盖以原点为中心、半径 12 的场景）；
		投影与相机同口径做 Y 翻转；方向与世界上方平行时改用 X 轴作上向量，避免 lookAt 退化。
		*/
		{
			glm::vec3 light_dir(state_ptr->light_direction[0], state_ptr->light_direction[1], state_ptr->light_direction[2]);
			//方向退化时取默认传播方向
			if (glm::dot(light_dir, light_dir) < 1e-12f)
				light_dir = glm::vec3(0.0f, 0.0f, -1.0f);
			light_dir = glm::normalize(light_dir);
			//上向量与光向平行时改用 X 轴
			glm::vec3 up = glm::abs(light_dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			/*
			级联阴影矩阵（第一百二十一轮）
			单级：与引入级联前完全一致 —— 光源机位固定在场景中心的「逆光向」一侧，
			      正交半宽取 shadow_half_extent，近远取 shadow_near / shadow_far。
			双级：两片各为「以相机为中心的包围球」，半径分别为 split_distance 与 split × far_scale；
				      片元按与相机的距离选择，故与着色器的选择口径严格一致（不依赖视场角与宽高比）。
				三级（第一百四十六轮）：再追加一片，半径为 split × far_scale²，选择阈值再加 split × far_scale。
			*/
			//本帧生效纹素世界尺寸（多级取各片最大；第六百六十一轮）
			state.shadow_texel_world = 0.0f;
			if (state_ptr->cascade_count >= 2 && state_ptr->cascade_far_scale > 1.0f)
			{
				//相机世界位置（各片包围球的共同中心）
				float camera_position[3] = {};
				state_ptr->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				//级联总数（2 或 3；第一百四十六轮）
				const uint32_t cascade_total = state_ptr->cascade_count == 3 ? 3u : 2u;
				for (uint32_t cascade = 0; cascade < cascade_total; ++cascade)
				{
					/*
					本片包围球半径
					第 0 片 = 切换距离；其后每片再乘一次远区倍数（三级时第 2 片为 split × far_scale²）。
					*/
					float radius = state_ptr->cascade_split_distance;
					for (uint32_t level = 0; level < cascade; ++level)
						radius *= state_ptr->cascade_far_scale;
					/*
					纹素尺寸钳制（第六百六十一轮）
					半径上限 = 纹素上限 × 分片边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
					*/
					const float tile_pixels = static_cast<float>(state_ptr->shadow_size) / static_cast<float>(cascade_total);
					if (state_ptr->shadow_texel_max > 0.0f)
					{
						const float radius_cap = state_ptr->shadow_texel_max * tile_pixels * 0.5f;
						if (radius > radius_cap)
							radius = radius_cap;
					}
					//光源机位：沿逆光向退到「球半径 + 近平面」处，使整球都落在近平面之后
					glm::vec3 eye = center - light_dir * (radius + state_ptr->shadow_near);
					glm::mat4 light_view = glm::lookAt(eye, center, up);
					//深度跨度：球直径 + 两端近平面余量
					float depth_far = 2.0f * radius + 2.0f * state_ptr->shadow_near;
					glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, state_ptr->shadow_near, depth_far);
					proj[1][1] *= -1.0f;
					glm::mat4 vp = proj * light_view;
					/*
					级联纹素对齐（第六百四十四轮）
					阴影视锥以相机为球心，相机移动会使整张阴影图逐帧平移（CSM 游泳 / 边缘闪烁）。
					故把光源空间裁剪坐标的平移量（X / Y）吸附到本片纹素网格：NDC 步长 = 2 / 分片边长；
					Z（深度）不吸附，避免改变深度精度分布。
					*/
					const float texel_ndc = 2.0f / (tile_pixels > 1.0f ? tile_pixels : 1.0f);
					vp[3][0] = std::floor(vp[3][0] / texel_ndc) * texel_ndc;
					vp[3][1] = std::floor(vp[3][1] / texel_ndc) * texel_ndc;
					//按片序写入对应矩阵（第四片不存在，级联数上限为 3）
					float* target_matrix = cascade == 0 ? state.light_view_proj
						: (cascade == 1 ? state.light_view_proj_far : state.light_view_proj_far2);
					std::memcpy(target_matrix, glm::value_ptr(vp), sizeof(float) * 16);
					//记录本片深度跨度（着色器法线偏移换算用）
					state.cascade_depth_span[cascade] = depth_far - state_ptr->shadow_near;
					/*
					记录本片纹素世界尺寸（第六百六十一轮）
					多级取各片最大值供诊断；钳制开启时该值不超过 shadow_texel_max。
					*/
					const float texel_world = 2.0f * radius / (tile_pixels > 1.0f ? tile_pixels : 1.0f);
					if (texel_world > state.shadow_texel_world)
						state.shadow_texel_world = texel_world;
				}
			}
			else
			{
				/*
				单级：以相机为中心的包围球（第六百五十五轮；修复单级固定原点）
				旧实现把光源机位固定在世界原点的逆光侧，相机离开原点后阴影整片丢失 / 复现；
				现改为「与多级同一拟合口径」：球心 = 相机位置、半径 = shadow_half_extent，
				深度跨度 = 球直径 ＋ 两端近平面余量，并同样做纹素对齐（相机平移不再重采样）。
				*/
				float camera_position[3] = {};
				state_ptr->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				float radius = state_ptr->shadow_half_extent;
				/*
				纹素尺寸钳制（第六百六十一轮）
				半径上限 = 纹素上限 × 阴影图边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
				*/
				const float tile_pixels = state_ptr->shadow_size > 1 ? static_cast<float>(state_ptr->shadow_size) : 1.0f;
				if (state_ptr->shadow_texel_max > 0.0f)
				{
					const float radius_cap = state_ptr->shadow_texel_max * tile_pixels * 0.5f;
					if (radius > radius_cap)
						radius = radius_cap;
				}
				glm::vec3 eye = center - light_dir * (radius + state_ptr->shadow_near);
				glm::mat4 light_view = glm::lookAt(eye, center, up);
				float depth_far = 2.0f * radius + 2.0f * state_ptr->shadow_near;
				glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius, state_ptr->shadow_near, depth_far);
				light_proj[1][1] *= -1.0f;
				glm::mat4 light_vp = light_proj * light_view;
				/*
				纹素对齐（与多级同口径）
				NDC 步长 = 2 / 阴影图边长；Z（深度）不吸附。
				*/
				const float texel_ndc = 2.0f / tile_pixels;
				light_vp[3][0] = std::floor(light_vp[3][0] / texel_ndc) * texel_ndc;
				light_vp[3][1] = std::floor(light_vp[3][1] / texel_ndc) * texel_ndc;
				std::memcpy(state.light_view_proj, glm::value_ptr(light_vp), sizeof(float) * 16);
				//单级：远区矩阵为零，三处深度跨度均取本片跨度（着色器按级联数取用）
				std::memset(state.light_view_proj_far, 0, sizeof(state.light_view_proj_far));
				std::memset(state.light_view_proj_far2, 0, sizeof(state.light_view_proj_far2));
				state.cascade_depth_span[0] = depth_far - state_ptr->shadow_near;
				state.cascade_depth_span[1] = state.cascade_depth_span[0];
				state.cascade_depth_span[2] = state.cascade_depth_span[0];
				//单级纹素世界尺寸（第六百六十一轮；钳制开启时不超过 shadow_texel_max）
				state.shadow_texel_world = 2.0f * radius / tile_pixels;
			}
			/*
			级联切换带自动淡化（第六百六十一轮）
			显式比例为 0、级联数 ≥ 2 且开关开启时，按「切换带半宽 ≥ 2 纹素」给出建议比例 4 / 分片边长，
			钳到 [0.005, 0.1]；显式比例 > 0 或开关关闭（默认）时保持显式值，画面逐字节不变。
			*/
			state.cascade_blend_effective = state_ptr->cascade_blend;
			if (state_ptr->cascade_blend_auto && state_ptr->cascade_blend <= 0.0f && state_ptr->cascade_count >= 2)
			{
				const float cascade_total_f = static_cast<float>(state_ptr->cascade_count == 3 ? 3u : 2u);
				const float tile_pixels_f = state_ptr->shadow_size > 1
					? static_cast<float>(state_ptr->shadow_size) / cascade_total_f : 1.0f;
				float recommended = 4.0f / tile_pixels_f;
				state.cascade_blend_effective = recommended < 0.005f ? 0.005f : (recommended > 0.1f ? 0.1f : recommended);
			}
		}
	}

	bool Render_Scene::spot_shadow_frame_build(Scene_Shadow_Frame& state) const
	{
		/*
		聚光灯阴影矩阵（第四百八十四轮；第五百零七轮扩为多盏）
		两路径：
		① spot_shadow_all 开启：前 4 盏聚光灯按声明顺序各占瓦片 0..3，各写一份矩阵与跨度；
		② 关闭：仅 spot_shadow_index 所选槽位写入瓦片 0（与旧单盏行为逐字节一致）。
		同时把「前向槽位 → 瓦片号」写入槽位映射，着色器按灯查找对应阴影图。
		*/
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			std::memset(state.spot_view_proj_tiles[tile], 0, sizeof(state.spot_view_proj_tiles[tile]));
			state.spot_shadow_spans[tile] = 0.0f;
			//逐瓦片偏置默认取全局值（第五百二十一轮；无自校准时与引入前一致；第六百五十八轮乘逐盏缩放）
			float slot_scale = 1.0f;
			if (state_ptr->spot_shadow_index >= 0 && state_ptr->spot_shadow_index < 8)
				slot_scale = state_ptr->spot_shadow_bias_scale[state_ptr->spot_shadow_index];
			state.spot_shadow_bias_tiles[tile] = state_ptr->spot_shadow_bias * slot_scale;
			//逐瓦片质量缩放默认 1（第七百零八轮；无逐盏设置时与全局参数相同）
			state.spot_shadow_softness_tiles[tile] = 1.0f;
			state.spot_shadow_strength_tiles[tile] = 1.0f;
		}
		for (std::size_t slot = 0; slot < 32; ++slot)
			state.spot_shadow_slot_tiles[slot] = -1.0f;
		//是否存在至少一盏有效聚光阴影
		bool spot_shadow_light_valid = false;
		//可投影的光源槽位（最多 8 盏）与数量
		int candidates[Device_State::SPOT_SHADOW_TILES] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		//本帧有效瓦片数
		std::size_t candidate_count = 0;
		if (state_ptr->spot_shadow_enabled)
		{
			if (state_ptr->spot_shadow_all)
			{
				/*
				多盏：优先「瓦片号 = 光源槽位」（第六百四十四轮；第七百零九轮加优先级）
				可投影聚光灯全部落在槽位 0..7 时按槽位固定，光源增减 / 重排不再使其它灯的瓦片整体前移；
				否则按优先级降序、同分槽位升序取前 8 盏，再按槽位升序分配到瓦片 0..7（默认优先级全 0 → 取登记顺序最前的 8 盏，
				与引入优先级前口径一致）。
				*/
				std::size_t spot_pool[32] = {};
				std::size_t spot_pool_count = 0;
				bool slot_mapping = true;
				for (std::size_t index = 0; index < state_ptr->point_light_count && index < 32; ++index)
				{
					//非聚光灯跳过
					if (!state_ptr->point_lights[index].is_spot)
						continue;
					//该槽位的聚光阴影被关闭则跳过（第六百四十八轮；槽位 ≥ 8 不适用）
					if (index < 8 && !state_ptr->spot_shadow_light_enabled[index])
						continue;
					//槽位超出 8 时无法按槽位固定瓦片号
					if (index >= Device_State::SPOT_SHADOW_TILES)
						slot_mapping = false;
					spot_pool[spot_pool_count++] = index;
				}
				if (slot_mapping)
				{
					for (std::size_t entry = 0; entry < spot_pool_count; ++entry)
						candidates[spot_pool[entry]] = static_cast<int>(spot_pool[entry]);
				}
				else
				{
					//超过 8 盏：按优先级降序、同分槽位升序取前 8（简单选择排序，池 ≤ 32）
					const std::size_t keep = spot_pool_count < Device_State::SPOT_SHADOW_TILES
						? spot_pool_count : Device_State::SPOT_SHADOW_TILES;
					for (std::size_t pick = 0; pick < keep; ++pick)
					{
						std::size_t best = pick;
						for (std::size_t probe = pick + 1; probe < spot_pool_count; ++probe)
						{
							const float probe_priority = state_ptr->spot_shadow_priority[spot_pool[probe]];
							const float best_priority = state_ptr->spot_shadow_priority[spot_pool[best]];
							//优先级更高者优先；同分保持较小槽位在前
							if (probe_priority > best_priority)
								best = probe;
						}
						if (best != pick)
						{
							const std::size_t swap = spot_pool[pick];
							spot_pool[pick] = spot_pool[best];
							spot_pool[best] = swap;
						}
					}
					//选中集合按槽位升序分配到瓦片 0..keep-1
					for (std::size_t first = 0; first < keep; ++first)
						for (std::size_t second = first + 1; second < keep; ++second)
							if (spot_pool[second] < spot_pool[first])
							{
								const std::size_t swap = spot_pool[first];
								spot_pool[first] = spot_pool[second];
								spot_pool[second] = swap;
							}
					for (std::size_t tile = 0; tile < keep; ++tile)
						candidates[tile] = static_cast<int>(spot_pool[tile]);
				}
				//统计本帧有效瓦片数
				for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
					if (candidates[tile] >= 0)
						++candidate_count;
			}
			//单盏：仅所选槽位且必须是聚光灯（瓦片固定为 0）
			else if (state_ptr->spot_shadow_index >= 0 && state_ptr->spot_shadow_index < static_cast<int>(state_ptr->point_light_count)
				&& state_ptr->point_lights[state_ptr->spot_shadow_index].is_spot
				//该槽位的聚光阴影被关闭则不投影（第六百四十八轮）
				&& (state_ptr->spot_shadow_index >= 8 || state_ptr->spot_shadow_light_enabled[state_ptr->spot_shadow_index]))
			{
				candidates[0] = state_ptr->spot_shadow_index;
				candidate_count = 1;
			}
			for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			{
				//该瓦片无对应光源则跳过
				if (candidates[tile] < 0)
					continue;
				const Scene_State::Point_Light& spot = state_ptr->point_lights[static_cast<std::size_t>(candidates[tile])];
				glm::vec3 spot_direction(spot.spot_direction[0], spot.spot_direction[1], spot.spot_direction[2]);
				//方向退化时取默认传播方向
				if (glm::dot(spot_direction, spot_direction) < 1e-12f)
					spot_direction = glm::vec3(0.0f, 0.0f, -1.0f);
				spot_direction = glm::normalize(spot_direction);
				//上向量与光向平行时改用 X 轴（避免 lookAt 退化）
				glm::vec3 spot_up = glm::abs(spot_direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
				glm::vec3 spot_position(spot.position[0], spot.position[1], spot.position[2]);
				//外锥余弦反解半锥角（钳到 (0.02, 1.5) 弧度）
				float outer_cos = spot.spot_outer_cos;
				outer_cos = outer_cos < -0.999f ? -0.999f : (outer_cos > 0.999f ? 0.999f : outer_cos);
				float half_angle = std::acos(outer_cos);
				half_angle = half_angle < 0.02f ? 0.02f : (half_angle > 1.5f ? 1.5f : half_angle);
				//衰减半径即远平面（钳到 ≥ 0.2）
				float far_plane = spot.radius > 0.2f ? spot.radius : 0.2f;
				glm::mat4 spot_view = glm::lookAt(spot_position, spot_position + spot_direction, spot_up);
				glm::mat4 spot_proj = glm::perspective(2.0f * half_angle, 1.0f, 0.1f, far_plane);
				spot_proj[1][1] *= -1.0f;
				glm::mat4 spot_vp = spot_proj * spot_view;
				std::memcpy(state.spot_view_proj_tiles[tile], glm::value_ptr(spot_vp), sizeof(float) * 16);
				//该瓦片深度跨度（着色器法线偏移换算用）
				state.spot_shadow_spans[tile] = far_plane - 0.1f;
				/*
				逐瓦片有效偏置（第五百二十一轮；第六百五十八轮加逐盏缩放）
				自校准开启时按「参考跨度 9 / 本瓦片跨度」缩放，保持世界法线偏移近似恒定；
				关闭时恒为全局值（与引入前逐字节一致）；再乘该槽位的逐盏缩放（默认 1）。
				*/
				float tile_scale = 1.0f;
				if (candidates[tile] >= 0 && candidates[tile] < 8)
					tile_scale = state_ptr->spot_shadow_bias_scale[candidates[tile]];
				state.spot_shadow_bias_tiles[tile] = (state_ptr->spot_shadow_bias_auto
					? state_ptr->spot_shadow_bias * (Device_State::SPOT_BIAS_REFERENCE_SPAN / state.spot_shadow_spans[tile])
					: state_ptr->spot_shadow_bias) * tile_scale;
				//前向槽位 → 瓦片号（第七百零九轮扩到 32 槽；未登记到映射表的槽位恒为 -1）
				if (candidates[tile] >= 0 && candidates[tile] < 32)
				{
					state.spot_shadow_slot_tiles[candidates[tile]] = static_cast<float>(tile);
					/*
					逐盏质量缩放（第七百零八轮；仅槽位 0..7 有逐盏配置）
					软度 / 强度按该槽位取值（默认 1 = 跟随全局），供着色器按瓦片读取。
					*/
					if (candidates[tile] < 8)
					{
						state.spot_shadow_softness_tiles[tile] = state_ptr->spot_shadow_softness_scale[candidates[tile]];
						state.spot_shadow_strength_tiles[tile] = state_ptr->spot_shadow_strength_scale[candidates[tile]];
					}
				}
				spot_shadow_light_valid = true;
			}
		}
		//本帧参与投影的聚光灯阴影瓦片数（第五百二十轮）
		state.spot_shadow_tiles = static_cast<uint32_t>(candidate_count);
		return spot_shadow_light_valid;
	}
	/*
	按需创建并绑定多盏聚光灯阴影瓦片（第五百一十一轮；第五百一十三轮扩为 8 瓦片）
	瓦片 0 常驻（与单盏行为一致）；瓦片 1..max_tiles-1 首次需要时创建，失败仅告警并保持「无阴影」占位。
	*/
	void spot_shadow_tiles_ensure_impl(Device_State& state, std::size_t max_tiles)
	{
		Renderer* owner = state.owner;
		//瓦片数钳到 [1, SPOT_SHADOW_TILES]
		if (max_tiles == 0)
			max_tiles = 1;
		if (max_tiles > Device_State::SPOT_SHADOW_TILES)
			max_tiles = Device_State::SPOT_SHADOW_TILES;
		/*
		图集生效时独立瓦片不再需要（第七百零七轮；Q-A）
		图集已承担全部瓦片采样与渲染，跳过创建可消除首帧资源分配与 8 份瓦片显存占用；
		图集创建失败（实际未生效）时仍走独立瓦片回落路径。
		*/
		if (state.spot_shadow_atlas)
			return;
		//默认采样器（与瓦片 0 共用）
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		uint64_t default_view_native = 0;
		uint64_t sampler_native = 0;
		owner->resource().texture_native_get(owner->resource().default_texture_handle(), default_view_native, sampler_native);
		default_view = reinterpret_cast<VkImageView>(default_view_native);
		sampler = reinterpret_cast<VkSampler>(sampler_native);
		for (std::size_t tile = 0; tile < max_tiles; ++tile)
		{
			//已建立则跳过
			if (state.spot_shadow_targets[tile] != 0)
				continue;
			if (!state.owner->resource().render_target_create_format(state.spot_shadow_targets[tile], owner->scene().shadow_resolution(),
				owner->scene().shadow_resolution(), static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM), state.swapchain.depth_format_get(), 1))
			{
				state.spot_shadow_targets[tile] = 0;
				logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 创建失败，按无阴影处理", tile);
				continue;
			}
			Render_Target_Native_Info target_info{};
			if (!state.owner->resource().render_target_native_get(state.spot_shadow_targets[tile], target_info))
			{
				state.owner->resource().render_target_release(state.spot_shadow_targets[tile]);
				state.spot_shadow_targets[tile] = 0;
				logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 查询失败，按无阴影处理", tile);
				continue;
			}
			//图集生效时 binding 9 全部元素仍指向图集，不改绑逐瓦片视图
			if (state.spot_shadow_atlas)
				continue;
			for (std::size_t slot = 0; slot < state.config.frame_overlap; ++slot)
				if (!owner->resource().scene_spot_shadow_bind(static_cast<uint32_t>(slot), sampler_native, target_info.color_view,
					static_cast<uint32_t>(tile)))
					logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 绑定失败（帧槽 {}）", tile, slot);
		}
	}

	/*
	聚光阴影图集切换（第六百七十轮；C1）
	按当前请求重建图集（4S × 2S）并重写 binding 9 的 8 个数组元素；
	请求关闭或创建失败时恢复逐瓦片视图（缺瓦片回落默认白纹理）。
	*/
	void spot_shadow_atlas_apply_impl(Device_State& state)
	{
		Renderer* owner = state.owner;
		//等尽在途帧，避免重建期间仍被采样
		state.frame.frames_wait();
		//回收旧图集
		if (state.spot_shadow_atlas_target != 0)
		{
			owner->resource().render_target_release(state.spot_shadow_atlas_target);
			state.spot_shadow_atlas_target = 0;
		}
		state.spot_shadow_atlas = false;
		//默认占位视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		uint64_t default_view_native = 0;
		uint64_t sampler_native = 0;
		owner->resource().texture_native_get(owner->resource().default_texture_handle(), default_view_native, sampler_native);
		default_view = reinterpret_cast<VkImageView>(default_view_native);
		sampler = reinterpret_cast<VkSampler>(sampler_native);
		//需要图集时按 4×2 创建（每瓦片 shadow_size 见方）
		if (owner->scene().spot_shadow_atlas_requested())
		{
			const uint32_t atlas_width = owner->scene().shadow_resolution() * 4;
			const uint32_t atlas_height = owner->scene().shadow_resolution() * 2;
			if (owner->resource().render_target_create_format(state.spot_shadow_atlas_target, atlas_width, atlas_height,
				static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM), state.swapchain.depth_format_get(), 1))
			{
				state.spot_shadow_atlas = true;
				/*
				图集接管后回收独立瓦片目标（第七百零七轮；Q-A）
				本函数开头已等尽在途帧，销毁安全；此后瓦片由图集承担，不再逐帧懒创建。
				*/
				for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
				{
					//未建立的瓦片跳过
					if (state.spot_shadow_targets[tile] == 0)
						continue;
					state.owner->resource().render_target_release(state.spot_shadow_targets[tile]);
					state.spot_shadow_targets[tile] = 0;
				}
			}
			else
			{
				state.spot_shadow_atlas_target = 0;
				logger.warn("渲染器:: 聚光阴影图集创建失败（{}×{}），回落独立瓦片", atlas_width, atlas_height);
			}
		}
		//图集颜色视图（未生效时为空）
		VkImageView atlas_view = VK_NULL_HANDLE;
		if (state.spot_shadow_atlas)
		{
			Render_Target_Native_Info atlas_info{};
			if (!owner->resource().render_target_native_get(state.spot_shadow_atlas_target, atlas_info))
			{
				owner->resource().render_target_release(state.spot_shadow_atlas_target);
				state.spot_shadow_atlas_target = 0;
				state.spot_shadow_atlas = false;
				atlas_view = VK_NULL_HANDLE;
				logger.warn("渲染器:: 聚光阴影图集查询失败，回落独立瓦片");
			}
			else
				atlas_view = reinterpret_cast<VkImageView>(atlas_info.color_view);
		}
		//逐帧槽重写 binding 9 的全部数组元素
		for (std::size_t slot = 0; slot < state.config.frame_overlap; ++slot)
		{
			for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			{
				//默认回落白纹理占位
				VkImageView view = default_view;
				if (state.spot_shadow_atlas)
					view = atlas_view;
				else if (state.spot_shadow_targets[tile] != 0)
				{
					Render_Target_Native_Info tile_info{};
					if (state.owner->resource().render_target_native_get(state.spot_shadow_targets[tile], tile_info))
						view = reinterpret_cast<VkImageView>(tile_info.color_view);
				}
				if (!owner->resource().scene_spot_shadow_bind(static_cast<uint32_t>(slot), reinterpret_cast<uint64_t>(sampler),
					reinterpret_cast<uint64_t>(view), static_cast<uint32_t>(tile)))
					logger.warn("渲染器:: 聚光阴影图集切换：帧槽 {} 元素 {} 绑定失败", slot, tile);
			}
		}
		//阴影图内容口径变化，下一帧必须重绘
		state.shadow_pass_ready = false;
	}
}
