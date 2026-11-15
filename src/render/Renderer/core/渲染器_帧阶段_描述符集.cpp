#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//帧内部辅助类型与函数
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	void Render_Device::frame_stage_descriptor_sets(Renderer* owner, Device_State& state, uint32_t slot)
	{
		//写逐绘制项集前等尽在途帧，再转交资源域填装原生描述符
		state.frame.frames_wait();
		owner->resource().frame_descriptor_sets_fill(state.pending_items, slot,
			reinterpret_cast<uint64_t>(state.instance_buffers[slot]), state.shadow_target,
			state.spot_shadow_atlas, state.spot_shadow_atlas_target, state.spot_shadow_targets);
	}

	//帧阶段：实例模型矩阵写入本帧槽实例缓冲（返回假表示本帧失败）
}
