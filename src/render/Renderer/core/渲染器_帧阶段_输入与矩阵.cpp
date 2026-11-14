#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//阶段函数声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"
//glm 数学库（光源空间矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	//帧阶段：泵事件 + 本帧输入锁存与原生 UI 交互

	void frame_stage_input_events(Renderer* owner, Renderer_Impl_Holder::Impl* impl)
	{
		//门面或实现体缺失则跳过本阶段
		if (owner == nullptr || impl == nullptr)
			return;
		//① 泵事件
		impl->platform.events_pump();
		/*
		①.5 帧输入锁存与原生 UI 交互
		事件泵已把 SDL 事件喂入输入聚合器；此处推进为「本帧快照」，再交给 UI 做命中测试与文本编辑。
		原生 UI 与外部 UI 共用同一 UI 阶段回调（见下方 ui_callback）。
		*/
		{
			//界面域状态
			Ui_State& ui_state = owner->ui().state();
			//窗口客户区尺寸（UI 坐标与命中测试的基准）
			uint32_t input_width = 0;
			uint32_t input_height = 0;
			impl->platform.window_size_get(input_width, input_height);
			//锁存本帧输入（UI 不使用 dt，按 0 传）
			ui_state.input.frame_advance(0.0, input_width, input_height, impl->platform.display_scale_get());
			/*
			帧前准备：按上一帧系统焦点决定控件键盘总闸（不在 UI 主对象时不响应 Tab/回车），
			并按系统文本模式决定本地拼接或系统回传落文本（必须在 frame_begin 之前）。
			*/
			ui_state.ui_event.text_route_prepare(ui_state.ui);
			//控件树消费本帧输入
			ui_state.ui.frame_begin(ui_state.input.frame_get());
			//系统文本模式或 UI 文本框焦点都启停平台文本输入
			ui_state.input.text_input_enable(ui_state.ui.text_focus_active() || ui_state.ui_event.system_text_mode());
			/*
			UI / 渲染层事件发布（已接入时）
			Raw → 焦点系统；UI 命中 → MouseFocus；模态切换 → ModalPush/Pop；
			系统文本模式下的文本框提交 → TextSession。
			*/
			ui_state.ui_event.frame_publish(ui_state.input.frame_get(), ui_state.ui);
			/*
			应用已收到的系统事件
			FocusChanged → 控件系统焦点环；Command → 绑定控件激活；
			Text → 焦点文本框落文本。放在发布之后，使同帧收发即时生效。
			*/
			ui_state.ui_event.frame_apply(ui_state.ui);
		}
	}

	void frame_stage_shadow_matrices(Renderer_Impl_Holder::Impl* impl)
	{
		/*
		⑤a 光源空间矩阵（第五十二轮）
		无论阴影是否开启都先算好并写入 UBO，使开关切换可即时生效。
		以方向光传播方向构造正交视锥（覆盖以原点为中心、半径 12 的场景）；
		投影与相机同口径做 Y 翻转；方向与世界上方平行时改用 X 轴作上向量，避免 lookAt 退化。
		*/
		{
			glm::vec3 light_dir(impl->light_direction[0], impl->light_direction[1], impl->light_direction[2]);
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
			impl->shadow_texel_world = 0.0f;
			if (impl->cascade_count >= 2 && impl->cascade_far_scale > 1.0f)
			{
				//相机世界位置（各片包围球的共同中心）
				float camera_position[3] = {};
				impl->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				//级联总数（2 或 3；第一百四十六轮）
				const uint32_t cascade_total = impl->cascade_count == 3 ? 3u : 2u;
				for (uint32_t cascade = 0; cascade < cascade_total; ++cascade)
				{
					/*
					本片包围球半径
					第 0 片 = 切换距离；其后每片再乘一次远区倍数（三级时第 2 片为 split × far_scale²）。
					*/
					float radius = impl->cascade_split_distance;
					for (uint32_t level = 0; level < cascade; ++level)
						radius *= impl->cascade_far_scale;
					/*
					纹素尺寸钳制（第六百六十一轮）
					半径上限 = 纹素上限 × 分片边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
					*/
					const float tile_pixels = static_cast<float>(impl->shadow_size) / static_cast<float>(cascade_total);
					if (impl->shadow_texel_max > 0.0f)
					{
						const float radius_cap = impl->shadow_texel_max * tile_pixels * 0.5f;
						if (radius > radius_cap)
							radius = radius_cap;
					}
					//光源机位：沿逆光向退到「球半径 + 近平面」处，使整球都落在近平面之后
					glm::vec3 eye = center - light_dir * (radius + impl->shadow_near);
					glm::mat4 light_view = glm::lookAt(eye, center, up);
					//深度跨度：球直径 + 两端近平面余量
					float depth_far = 2.0f * radius + 2.0f * impl->shadow_near;
					glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, impl->shadow_near, depth_far);
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
					float* target_matrix = cascade == 0 ? impl->light_view_proj
						: (cascade == 1 ? impl->light_view_proj_far : impl->light_view_proj_far2);
					std::memcpy(target_matrix, glm::value_ptr(vp), sizeof(float) * 16);
					//记录本片深度跨度（着色器法线偏移换算用）
					impl->cascade_depth_span[cascade] = depth_far - impl->shadow_near;
					/*
					记录本片纹素世界尺寸（第六百六十一轮）
					多级取各片最大值供诊断；钳制开启时该值不超过 shadow_texel_max。
					*/
					const float texel_world = 2.0f * radius / (tile_pixels > 1.0f ? tile_pixels : 1.0f);
					if (texel_world > impl->shadow_texel_world)
						impl->shadow_texel_world = texel_world;
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
				impl->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				float radius = impl->shadow_half_extent;
				/*
				纹素尺寸钳制（第六百六十一轮）
				半径上限 = 纹素上限 × 阴影图边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
				*/
				const float tile_pixels = impl->shadow_size > 1 ? static_cast<float>(impl->shadow_size) : 1.0f;
				if (impl->shadow_texel_max > 0.0f)
				{
					const float radius_cap = impl->shadow_texel_max * tile_pixels * 0.5f;
					if (radius > radius_cap)
						radius = radius_cap;
				}
				glm::vec3 eye = center - light_dir * (radius + impl->shadow_near);
				glm::mat4 light_view = glm::lookAt(eye, center, up);
				float depth_far = 2.0f * radius + 2.0f * impl->shadow_near;
				glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius, impl->shadow_near, depth_far);
				light_proj[1][1] *= -1.0f;
				glm::mat4 light_vp = light_proj * light_view;
				/*
				纹素对齐（与多级同口径）
				NDC 步长 = 2 / 阴影图边长；Z（深度）不吸附。
				*/
				const float texel_ndc = 2.0f / tile_pixels;
				light_vp[3][0] = std::floor(light_vp[3][0] / texel_ndc) * texel_ndc;
				light_vp[3][1] = std::floor(light_vp[3][1] / texel_ndc) * texel_ndc;
				std::memcpy(impl->light_view_proj, glm::value_ptr(light_vp), sizeof(float) * 16);
				//单级：远区矩阵为零，三处深度跨度均取本片跨度（着色器按级联数取用）
				std::memset(impl->light_view_proj_far, 0, sizeof(impl->light_view_proj_far));
				std::memset(impl->light_view_proj_far2, 0, sizeof(impl->light_view_proj_far2));
				impl->cascade_depth_span[0] = depth_far - impl->shadow_near;
				impl->cascade_depth_span[1] = impl->cascade_depth_span[0];
				impl->cascade_depth_span[2] = impl->cascade_depth_span[0];
				//单级纹素世界尺寸（第六百六十一轮；钳制开启时不超过 shadow_texel_max）
				impl->shadow_texel_world = 2.0f * radius / tile_pixels;
			}
			/*
			级联切换带自动淡化（第六百六十一轮）
			显式比例为 0、级联数 ≥ 2 且开关开启时，按「切换带半宽 ≥ 2 纹素」给出建议比例 4 / 分片边长，
			钳到 [0.005, 0.1]；显式比例 > 0 或开关关闭（默认）时保持显式值，画面逐字节不变。
			*/
			impl->cascade_blend_effective = impl->cascade_blend;
			if (impl->cascade_blend_auto && impl->cascade_blend <= 0.0f && impl->cascade_count >= 2)
			{
				const float cascade_total_f = static_cast<float>(impl->cascade_count == 3 ? 3u : 2u);
				const float tile_pixels_f = impl->shadow_size > 1
					? static_cast<float>(impl->shadow_size) / cascade_total_f : 1.0f;
				float recommended = 4.0f / tile_pixels_f;
				impl->cascade_blend_effective = recommended < 0.005f ? 0.005f : (recommended > 0.1f ? 0.1f : recommended);
			}
		}
	}

	bool frame_stage_spot_shadow_matrices(Renderer_Impl_Holder::Impl* impl)
	{
		/*
		聚光灯阴影矩阵（第四百八十四轮；第五百零七轮扩为多盏）
		两路径：
		① spot_shadow_all 开启：前 4 盏聚光灯按声明顺序各占瓦片 0..3，各写一份矩阵与跨度；
		② 关闭：仅 spot_shadow_index 所选槽位写入瓦片 0（与旧单盏行为逐字节一致）。
		同时把「前向槽位 → 瓦片号」写入槽位映射，着色器按灯查找对应阴影图。
		*/
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			std::memset(impl->spot_view_proj_tiles[tile], 0, sizeof(impl->spot_view_proj_tiles[tile]));
			impl->spot_shadow_spans[tile] = 0.0f;
			//逐瓦片偏置默认取全局值（第五百二十一轮；无自校准时与引入前一致；第六百五十八轮乘逐盏缩放）
			float slot_scale = 1.0f;
			if (impl->spot_shadow_index >= 0 && impl->spot_shadow_index < 8)
				slot_scale = impl->spot_shadow_bias_scale[impl->spot_shadow_index];
			impl->spot_shadow_bias_tiles[tile] = impl->spot_shadow_bias * slot_scale;
			//逐瓦片质量缩放默认 1（第七百零八轮；无逐盏设置时与全局参数相同）
			impl->spot_shadow_softness_tiles[tile] = 1.0f;
			impl->spot_shadow_strength_tiles[tile] = 1.0f;
		}
		for (std::size_t slot = 0; slot < 32; ++slot)
			impl->spot_shadow_slot_tiles[slot] = -1.0f;
		//是否存在至少一盏有效聚光阴影
		bool spot_shadow_light_valid = false;
		//可投影的光源槽位（最多 8 盏）与数量
		int candidates[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		//本帧有效瓦片数
		std::size_t candidate_count = 0;
		if (impl->spot_shadow_enabled)
		{
			if (impl->spot_shadow_all)
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
				for (std::size_t index = 0; index < impl->point_light_count && index < 32; ++index)
				{
					//非聚光灯跳过
					if (!impl->point_lights[index].is_spot)
						continue;
					//该槽位的聚光阴影被关闭则跳过（第六百四十八轮；槽位 ≥ 8 不适用）
					if (index < 8 && !impl->spot_shadow_light_enabled[index])
						continue;
					//槽位超出 8 时无法按槽位固定瓦片号
					if (index >= Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)
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
					const std::size_t keep = spot_pool_count < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES
						? spot_pool_count : Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES;
					for (std::size_t pick = 0; pick < keep; ++pick)
					{
						std::size_t best = pick;
						for (std::size_t probe = pick + 1; probe < spot_pool_count; ++probe)
						{
							const float probe_priority = impl->spot_shadow_priority[spot_pool[probe]];
							const float best_priority = impl->spot_shadow_priority[spot_pool[best]];
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
				for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
					if (candidates[tile] >= 0)
						++candidate_count;
			}
			//单盏：仅所选槽位且必须是聚光灯（瓦片固定为 0）
			else if (impl->spot_shadow_index >= 0 && impl->spot_shadow_index < static_cast<int>(impl->point_light_count)
				&& impl->point_lights[impl->spot_shadow_index].is_spot
				//该槽位的聚光阴影被关闭则不投影（第六百四十八轮）
				&& (impl->spot_shadow_index >= 8 || impl->spot_shadow_light_enabled[impl->spot_shadow_index]))
			{
				candidates[0] = impl->spot_shadow_index;
				candidate_count = 1;
			}
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			{
				//该瓦片无对应光源则跳过
				if (candidates[tile] < 0)
					continue;
				const Renderer_Impl_Holder::Impl::Point_Light& spot = impl->point_lights[static_cast<std::size_t>(candidates[tile])];
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
				std::memcpy(impl->spot_view_proj_tiles[tile], glm::value_ptr(spot_vp), sizeof(float) * 16);
				//该瓦片深度跨度（着色器法线偏移换算用）
				impl->spot_shadow_spans[tile] = far_plane - 0.1f;
				/*
				逐瓦片有效偏置（第五百二十一轮；第六百五十八轮加逐盏缩放）
				自校准开启时按「参考跨度 9 / 本瓦片跨度」缩放，保持世界法线偏移近似恒定；
				关闭时恒为全局值（与引入前逐字节一致）；再乘该槽位的逐盏缩放（默认 1）。
				*/
				float tile_scale = 1.0f;
				if (candidates[tile] >= 0 && candidates[tile] < 8)
					tile_scale = impl->spot_shadow_bias_scale[candidates[tile]];
				impl->spot_shadow_bias_tiles[tile] = (impl->spot_shadow_bias_auto
					? impl->spot_shadow_bias * (Renderer_Impl_Holder::Impl::SPOT_BIAS_REFERENCE_SPAN / impl->spot_shadow_spans[tile])
					: impl->spot_shadow_bias) * tile_scale;
				//前向槽位 → 瓦片号（第七百零九轮扩到 32 槽；未登记到映射表的槽位恒为 -1）
				if (candidates[tile] >= 0 && candidates[tile] < 32)
				{
					impl->spot_shadow_slot_tiles[candidates[tile]] = static_cast<float>(tile);
					/*
					逐盏质量缩放（第七百零八轮；仅槽位 0..7 有逐盏配置）
					软度 / 强度按该槽位取值（默认 1 = 跟随全局），供着色器按瓦片读取。
					*/
					if (candidates[tile] < 8)
					{
						impl->spot_shadow_softness_tiles[tile] = impl->spot_shadow_softness_scale[candidates[tile]];
						impl->spot_shadow_strength_tiles[tile] = impl->spot_shadow_strength_scale[candidates[tile]];
					}
				}
				spot_shadow_light_valid = true;
			}
		}
		//本帧参与投影的聚光灯阴影瓦片数（第五百二十轮）
		impl->spot_shadow_tiles = static_cast<uint32_t>(candidate_count);
		return spot_shadow_light_valid;
	}
}
