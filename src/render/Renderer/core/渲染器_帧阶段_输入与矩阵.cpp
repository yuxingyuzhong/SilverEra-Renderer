#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//帧内部辅助类型与函数
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

	void Render_Device::frame_stage_input_events(Renderer* owner, Device_State& state)
	{
		//门面缺失则跳过本阶段
		if (owner == nullptr)
			return;
		//① 泵事件
		state.platform.events_pump();
		/*
		①.5 帧输入锁存与原生 UI 交互
		事件泵已把 SDL 事件喂入输入聚合器；此处推进为「本帧快照」，再交给 UI 做命中测试与文本编辑。
		原生 UI 与外部 UI 共用同一 UI 阶段回调（见下方 ui_callback）。
		*/
		{
			//窗口客户区尺寸（UI 坐标与命中测试的基准）
			uint32_t input_width = 0;
			uint32_t input_height = 0;
			state.platform.window_size_get(input_width, input_height);
			//界面域按既定顺序推进输入、UI 交互与事件路由
			owner->ui().input_frame_advance(input_width, input_height, state.platform.display_scale_get());
		}
	}

	void Render_Device::frame_stage_shadow_matrices(Renderer* owner, Device_State& state)
	{
		Scene_Shadow_Frame frame{};
		std::memcpy(frame.light_view_proj, state.light_view_proj, sizeof(frame.light_view_proj));
		std::memcpy(frame.light_view_proj_far, state.light_view_proj_far, sizeof(frame.light_view_proj_far));
		std::memcpy(frame.light_view_proj_far2, state.light_view_proj_far2, sizeof(frame.light_view_proj_far2));
		std::memcpy(frame.cascade_depth_span, state.cascade_depth_span, sizeof(frame.cascade_depth_span));
		frame.shadow_texel_world = state.shadow_texel_world;
		frame.cascade_blend_effective = state.cascade_blend_effective;
		owner->scene().directional_shadow_frame_build(frame);
		std::memcpy(state.light_view_proj, frame.light_view_proj, sizeof(state.light_view_proj));
		std::memcpy(state.light_view_proj_far, frame.light_view_proj_far, sizeof(state.light_view_proj_far));
		std::memcpy(state.light_view_proj_far2, frame.light_view_proj_far2, sizeof(state.light_view_proj_far2));
		std::memcpy(state.cascade_depth_span, frame.cascade_depth_span, sizeof(state.cascade_depth_span));
		state.shadow_texel_world = frame.shadow_texel_world;
		state.cascade_blend_effective = frame.cascade_blend_effective;
	}

	bool Render_Device::frame_stage_spot_shadow_matrices(Renderer* owner, Device_State& state)
	{
		Scene_Shadow_Frame frame{};
		const bool valid = owner->scene().spot_shadow_frame_build(frame);
		std::memcpy(state.spot_view_proj_tiles, frame.spot_view_proj_tiles, sizeof(state.spot_view_proj_tiles));
		std::memcpy(state.spot_shadow_spans, frame.spot_shadow_spans, sizeof(state.spot_shadow_spans));
		std::memcpy(state.spot_shadow_bias_tiles, frame.spot_shadow_bias_tiles, sizeof(state.spot_shadow_bias_tiles));
		std::memcpy(state.spot_shadow_softness_tiles, frame.spot_shadow_softness_tiles, sizeof(state.spot_shadow_softness_tiles));
		std::memcpy(state.spot_shadow_strength_tiles, frame.spot_shadow_strength_tiles, sizeof(state.spot_shadow_strength_tiles));
		std::memcpy(state.spot_shadow_slot_tiles, frame.spot_shadow_slot_tiles, sizeof(state.spot_shadow_slot_tiles));
		state.spot_shadow_tiles = frame.spot_shadow_tiles;
		return valid;
	}

}
