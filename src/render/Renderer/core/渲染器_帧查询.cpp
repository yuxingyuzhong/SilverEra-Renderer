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

namespace engine
{
	//窗口是否请求退出
	bool Render_Device::close_requested(void) const
	{
		//若设备域状态未分配则视为未请求退出
		if (state_ptr == nullptr)
			return false;
		return state_ptr->platform.close_requested();
	}

	//已呈现帧数
	uint64_t Render_Device::frame_count(void) const
	{
		//若设备域状态未分配则返回零
		if (state_ptr == nullptr)
			return 0;
		return state_ptr->frame_count;
	}

	//等待全部在途帧完成（第二百一十八轮）
	void Render_Device::frame_wait_idle(void)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//等尽全部帧槽的完成栅栏（与门面卸载路径同一口径）
		state_ptr->frame.frames_wait();
	}

	//清零累计帧统计（第九十一轮）
	void Render_Device::frame_stats_reset(void)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//帧模块累计量归零
		state_ptr->frame.totals_reset();
		//门面自身累计量归零（帧数 / 剔除数 / 按需上传字节）
		state_ptr->frame_count = 0;
		state_ptr->culled_items = 0;
		state_ptr->culled_instances = 0;
		//剔除耗时累计归零（最近一帧值保留；第二百四十七轮）
		state_ptr->cull_total_ms = 0.0;
		owner->compute().frame_stats_reset();
		state_ptr->scene_upload_bytes = 0;
		state_ptr->instance_upload_bytes = 0;
		//阴影重绘 / 复用计数归零（第一百四十五轮；最近一次量保持不变）
		state_ptr->shadow_redraws = 0;
		state_ptr->shadow_reuses = 0;
	}

	//取帧统计快照
	Frame_Stats Render_Device::frame_stats_get(void) const
	{
		//默认全零
		Frame_Stats stats{};
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return stats;
		//已推进帧数取自门面自身计数（与 (*owner).frame_count() 同口径）
		stats.frames = state_ptr->frame_count;
		//帧耗时与绘制 / 实例计数取自帧模块（主机侧录制统计）
		stats.frame_ms = state_ptr->frame.last_frame_ms_get();
		//主机侧分段（第一百三十三轮；录制 / 提交 / 呈现）
		stats.host_record_ms = state_ptr->frame.last_host_record_ms_get();
		stats.host_submit_ms = state_ptr->frame.last_host_submit_ms_get();
		stats.host_present_ms = state_ptr->frame.last_host_present_ms_get();
		stats.draw_items = state_ptr->frame.total_draw_items_get();
		stats.instances = state_ptr->frame.total_instances_get();
		//GPU 侧耗时取自帧模块时间戳查询池（设备不支持时恒 0）
		stats.gpu_frame_ms = state_ptr->frame.last_gpu_frame_ms_get();
		//GPU 分解：场景 / 后处理取自主查询池，阴影取自阴影专用池（第一百二十八轮）
		stats.gpu_scene_ms = state_ptr->frame.last_gpu_scene_ms_get();
		stats.gpu_post_ms = state_ptr->frame.last_gpu_post_ms_get();
		stats.gpu_shadow_ms = state_ptr->shadow_gpu_ms;
		//累计分段耗时与阴影样本数（第一百三十一轮）
		stats.gpu_scene_total_ms = state_ptr->frame.total_gpu_scene_ms_get();
		stats.gpu_post_total_ms = state_ptr->frame.total_gpu_post_ms_get();
		stats.gpu_shadow_total_ms = state_ptr->frame.total_gpu_shadow_ms_get();
		stats.gpu_shadow_samples = state_ptr->frame.gpu_shadow_sample_count_get();
		//实际绘制量（不含被跳过的不完整项；与登记量 draw_items / instances 对照可见差异）
		stats.drawn_items = state_ptr->frame.total_drawn_items_get();
		stats.drawn_instances = state_ptr->frame.total_drawn_instances_get();
		//累计口径：主机侧耗时之和、GPU 侧成功读回之和与样本数
		stats.frame_total_ms = state_ptr->frame.total_frame_ms_get();
		//主机侧分段累计（第一百三十四轮）
		stats.host_record_total_ms = state_ptr->frame.total_host_record_ms_get();
		stats.host_submit_total_ms = state_ptr->frame.total_host_submit_ms_get();
		stats.host_present_total_ms = state_ptr->frame.total_host_present_ms_get();
		stats.gpu_total_ms = state_ptr->frame.total_gpu_frame_ms_get();
		stats.gpu_samples = state_ptr->frame.gpu_sample_count_get();
		//最近一帧实际执行的后处理级数（0 = 未走后处理链）
		stats.post_stages = state_ptr->frame.last_post_stages_get();
		//最近一帧实际使用的采样数（第一百零一轮）
		stats.msaa_samples = state_ptr->frame.last_sample_count_get();
		//累计因视锥剔除而未登记的项数
		stats.culled_items = state_ptr->culled_items;
		//实例级剔除（第一百五十三轮）
		stats.culled_instances = state_ptr->culled_instances;
		stats.last_culled_instances = state_ptr->last_culled_instances;
		//剔除耗时（第二百四十七轮；GPU 剔除 / Cluster / 间接绘制在接入前恒 0）
		stats.cpu_cull_ms = state_ptr->last_cull_ms;
		stats.cpu_cull_total_ms = state_ptr->cull_total_ms;
		//计算域统计快照（GPU 剔除与 Cluster）
		owner->compute().frame_stats_fill(stats);
		//间接绘制次数（第二百五十二轮；未走间接路径时恒 0）
		stats.indirect_draws = state_ptr->frame.indirect_draw_count_get();
		//阴影 pass 诊断（第六百四十七轮；A4 补齐）
		stats.shadow_passes = state_ptr->last_shadow_passes;
		stats.spot_shadow_passes = state_ptr->last_spot_shadow_passes;
		stats.shadow_resolution = state_ptr->shadow_target != 0 ? owner->scene().shadow_resolution() : 0u;
		//聚光阴影图集实际生效标志（第六百八十轮）
		stats.shadow_atlas = state_ptr->spot_shadow_atlas ? 1u : 0u;
		//已建立的独立瓦片目标数（第七百零七轮；图集生效时为 0）
		stats.spot_shadow_tile_targets = 0;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			if (state_ptr->spot_shadow_targets[tile] != 0)
				++stats.spot_shadow_tile_targets;
		//场景域光照与阴影配置统计（图集字节数使用上方实际生效标志）
		owner->scene().frame_stats_fill(stats);
		//级联纹素与淡化观测（第六百六十一轮）
		stats.shadow_texel_world = state_ptr->shadow_texel_world;
		stats.cascade_blend_applied = state_ptr->cascade_blend_effective;
		//阴影提交次数（第六百六十五轮；C2 基线）
		stats.shadow_submits = state_ptr->last_shadow_submits;
		//最近一帧即时观测（第一百三十二轮；清零累计量时保持不变）
		stats.last_draw_items = state_ptr->last_draw_items;
		stats.last_instances = state_ptr->last_instances;
		stats.last_culled_items = state_ptr->last_culled_items;
		//合批收益（第一百三十五轮）
		stats.last_merged_items = state_ptr->last_merged_items;
		stats.merged_items = state_ptr->merged_items;
		//阴影按需重绘观测（第一百四十五轮）
		stats.shadow_redraws = state_ptr->shadow_redraws;
		stats.shadow_reuses = state_ptr->shadow_reuses;
		stats.last_shadow_items = state_ptr->last_shadow_items;
		stats.shadow_last_redrawn = state_ptr->shadow_last_redrawn;
		stats.spot_shadow_tiles = state_ptr->spot_shadow_tiles;
		//最近一次阴影 pass 的并行段数（第二百一十二轮）
		stats.shadow_parallel_segments = state_ptr->last_shadow_parallel_segments;
		//累计按需上传字节（第七十二轮）
		stats.scene_upload_bytes = state_ptr->scene_upload_bytes;
		stats.instance_upload_bytes = state_ptr->instance_upload_bytes;
		//渲染工作线程观测（第一百七十七轮）
		stats.render_threads = state_ptr->thread_pool.thread_count_get();
		stats.parallel_dispatches = state_ptr->thread_pool.dispatch_count_get();
		//按用途拆分的派发次数（第二百二十四轮）
		stats.cull_parallel_dispatches = state_ptr->cull_parallel_dispatches;
		stats.scan_parallel_dispatches = state_ptr->scan_parallel_dispatches;
		stats.cache_parallel_dispatches = state_ptr->cache_parallel_dispatches;
		//最近一帧并行录制段数（第一百九十八轮）
		stats.parallel_segments = state_ptr->frame.last_parallel_segments_get();
		//粒子域统计快照（活跃数、模拟与最近绘制路径）
		owner->particle().frame_stats_fill(stats);
		return stats;
	}

}
