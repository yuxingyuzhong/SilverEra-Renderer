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

namespace engine
{
	//窗口是否请求退出
	bool Renderer_Core::close_requested(void) const
	{
		//若实现体未分配则视为未请求退出
		if (impl == nullptr)
			return false;
		return impl->platform.close_requested();
	}

	//已呈现帧数
	uint64_t Renderer_Core::frame_count(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->frame_count;
	}

	//等待全部在途帧完成（第二百一十八轮）
	void Renderer_Core::frame_wait_idle(void)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//等尽全部帧槽的完成栅栏（与门面卸载路径同一口径）
		impl->frame.frames_wait();
	}

	//清零累计帧统计（第九十一轮）
	void Renderer_Core::frame_stats_reset(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//帧模块累计量归零
		impl->frame.totals_reset();
		//门面自身累计量归零（帧数 / 剔除数 / 按需上传字节）
		impl->frame_count = 0;
		impl->culled_items = 0;
		impl->culled_instances = 0;
		//剔除耗时累计归零（最近一帧值保留；第二百四十七轮）
		impl->cull_total_ms = 0.0;
		impl->gpu_cull_dispatches = 0;
		impl->last_gpu_occluded = 0;
		impl->scene_upload_bytes = 0;
		impl->instance_upload_bytes = 0;
		//阴影重绘 / 复用计数归零（第一百四十五轮；最近一次量保持不变）
		impl->shadow_redraws = 0;
		impl->shadow_reuses = 0;
	}

	//取帧统计快照
	Frame_Stats Renderer_Core::frame_stats_get(void) const
	{
		//默认全零
		Frame_Stats stats{};
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return stats;
		//已推进帧数取自门面自身计数（与 self().frame_count() 同口径）
		stats.frames = impl->frame_count;
		//帧耗时与绘制 / 实例计数取自帧模块（主机侧录制统计）
		stats.frame_ms = impl->frame.last_frame_ms_get();
		//主机侧分段（第一百三十三轮；录制 / 提交 / 呈现）
		stats.host_record_ms = impl->frame.last_host_record_ms_get();
		stats.host_submit_ms = impl->frame.last_host_submit_ms_get();
		stats.host_present_ms = impl->frame.last_host_present_ms_get();
		stats.draw_items = impl->frame.total_draw_items_get();
		stats.instances = impl->frame.total_instances_get();
		//GPU 侧耗时取自帧模块时间戳查询池（设备不支持时恒 0）
		stats.gpu_frame_ms = impl->frame.last_gpu_frame_ms_get();
		//GPU 分解：场景 / 后处理取自主查询池，阴影取自阴影专用池（第一百二十八轮）
		stats.gpu_scene_ms = impl->frame.last_gpu_scene_ms_get();
		stats.gpu_post_ms = impl->frame.last_gpu_post_ms_get();
		stats.gpu_shadow_ms = impl->shadow_gpu_ms;
		//累计分段耗时与阴影样本数（第一百三十一轮）
		stats.gpu_scene_total_ms = impl->frame.total_gpu_scene_ms_get();
		stats.gpu_post_total_ms = impl->frame.total_gpu_post_ms_get();
		stats.gpu_shadow_total_ms = impl->frame.total_gpu_shadow_ms_get();
		stats.gpu_shadow_samples = impl->frame.gpu_shadow_sample_count_get();
		//实际绘制量（不含被跳过的不完整项；与登记量 draw_items / instances 对照可见差异）
		stats.drawn_items = impl->frame.total_drawn_items_get();
		stats.drawn_instances = impl->frame.total_drawn_instances_get();
		//累计口径：主机侧耗时之和、GPU 侧成功读回之和与样本数
		stats.frame_total_ms = impl->frame.total_frame_ms_get();
		//主机侧分段累计（第一百三十四轮）
		stats.host_record_total_ms = impl->frame.total_host_record_ms_get();
		stats.host_submit_total_ms = impl->frame.total_host_submit_ms_get();
		stats.host_present_total_ms = impl->frame.total_host_present_ms_get();
		stats.gpu_total_ms = impl->frame.total_gpu_frame_ms_get();
		stats.gpu_samples = impl->frame.gpu_sample_count_get();
		//最近一帧实际执行的后处理级数（0 = 未走后处理链）
		stats.post_stages = impl->frame.last_post_stages_get();
		//最近一帧实际使用的采样数（第一百零一轮）
		stats.msaa_samples = impl->frame.last_sample_count_get();
		//累计因视锥剔除而未登记的项数
		stats.culled_items = impl->culled_items;
		//实例级剔除（第一百五十三轮）
		stats.culled_instances = impl->culled_instances;
		stats.last_culled_instances = impl->last_culled_instances;
		//剔除耗时（第二百四十七轮；GPU 剔除 / Cluster / 间接绘制在接入前恒 0）
		stats.cpu_cull_ms = impl->last_cull_ms;
		stats.cpu_cull_total_ms = impl->cull_total_ms;
		//GPU 剔除耗时（第二百四十九轮；未走 GPU 路径时为 0）
		stats.gpu_cull_ms = impl->last_gpu_cull_ms;
		//GPU 压缩派发次数（第二百五十一轮）
		stats.gpu_cull_dispatches = impl->gpu_cull_dispatches;
		//最近一次遮挡剔除数（第二百六十一轮）
		stats.gpu_occluded = impl->last_gpu_occluded;
		//间接绘制次数（第二百五十二轮；未走间接路径时恒 0）
		stats.indirect_draws = impl->frame.indirect_draw_count_get();
		//簇数据整备观测（第二百七十三轮；Cluster 关闭时恒 0）
		stats.cluster_build_ms = impl->last_cluster_build_ms;
		stats.cluster_entries = impl->last_cluster_entries;
		stats.cluster_gpu_bytes = impl->last_cluster_gpu_bytes;
		//光源截断标志（第二百八十轮；Cluster 关闭且超过 8 盏时置 1）
		stats.light_truncated = impl->light_truncated ? 1u : 0u;
		//光照诊断（第六百四十五轮；A4）
		stats.lights_active = impl->point_light_count;
		stats.lights_dropped = impl->light_dropped;
		stats.light_select_mode = impl->light_select_mode;
		//阴影 pass 诊断（第六百四十七轮；A4 补齐）
		stats.shadow_passes = impl->last_shadow_passes;
		stats.spot_shadow_passes = impl->last_spot_shadow_passes;
		stats.shadow_resolution = impl->shadow_target != 0 ? impl->shadow_size : 0u;
		//聚光阴影图集实际生效标志（第六百八十轮）
		stats.shadow_atlas = impl->spot_shadow_atlas ? 1u : 0u;
		//已建立的独立瓦片目标数（第七百零七轮；图集生效时为 0）
		stats.spot_shadow_tile_targets = 0;
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			if (impl->spot_shadow_targets[tile] != 0)
				++stats.spot_shadow_tile_targets;
		//图集颜色附件字节数（第六百八十四轮；RGBA8 = 4 字节/像素，4S × 2S）
		stats.shadow_atlas_bytes = impl->spot_shadow_atlas
			? static_cast<uint64_t>(impl->shadow_size) * 8u * impl->shadow_size * 4u : 0u;
		//PCSS 软阴影状态（第六百五十七轮）
		stats.shadow_pcss = impl->shadow_pcss_enabled ? 1u : 0u;
		//级联纹素与淡化观测（第六百六十一轮）
		stats.shadow_texel_world = impl->shadow_texel_world;
		stats.cascade_blend_applied = impl->cascade_blend_effective;
		//阴影提交次数（第六百六十五轮；C2 基线）
		stats.shadow_submits = impl->last_shadow_submits;
		//最近一帧即时观测（第一百三十二轮；清零累计量时保持不变）
		stats.last_draw_items = impl->last_draw_items;
		stats.last_instances = impl->last_instances;
		stats.last_culled_items = impl->last_culled_items;
		//合批收益（第一百三十五轮）
		stats.last_merged_items = impl->last_merged_items;
		stats.merged_items = impl->merged_items;
		//阴影按需重绘观测（第一百四十五轮）
		stats.shadow_redraws = impl->shadow_redraws;
		stats.shadow_reuses = impl->shadow_reuses;
		stats.last_shadow_items = impl->last_shadow_items;
		stats.shadow_last_redrawn = impl->shadow_last_redrawn;
		stats.spot_shadow_tiles = impl->spot_shadow_tiles;
		//最近一次阴影 pass 的并行段数（第二百一十二轮）
		stats.shadow_parallel_segments = impl->last_shadow_parallel_segments;
		//累计按需上传字节（第七十二轮）
		stats.scene_upload_bytes = impl->scene_upload_bytes;
		stats.instance_upload_bytes = impl->instance_upload_bytes;
		//渲染工作线程观测（第一百七十七轮）
		stats.render_threads = impl->thread_pool.thread_count_get();
		stats.parallel_dispatches = impl->thread_pool.dispatch_count_get();
		//按用途拆分的派发次数（第二百二十四轮）
		stats.cull_parallel_dispatches = impl->cull_parallel_dispatches;
		stats.scan_parallel_dispatches = impl->scan_parallel_dispatches;
		stats.cache_parallel_dispatches = impl->cache_parallel_dispatches;
		//最近一帧并行录制段数（第一百九十八轮）
		stats.parallel_segments = impl->frame.last_parallel_segments_get();
		//粒子模拟统计（第三百二十二轮；关闭时活跃与累计均为 0）
		//常驻模式取 GPU 活跃计数（第三百四十轮）；否则按绑定的活跃标记统计（模拟归应用层）
		uint32_t 活跃数 = 0;
		if (self().particle().state().particle_enabled && !self().particle().state().particle_resident
			&& self().particle().state().particle_alive != nullptr && self().particle().state().particle_source_capacity > 0)
		{
			for (uint32_t index = 0; index < self().particle().state().particle_source_capacity; ++index)
				if (self().particle().state().particle_alive[index] != 0u)
					活跃数 += 1;
		}
		stats.particle_active = self().particle().state().particle_enabled
			? (self().particle().state().particle_resident ? self().particle().state().particle_resident_active : 活跃数) : 0u;
		stats.particle_born_total = self().particle().state().particle_born_total;
		stats.particle_died_total = self().particle().state().particle_died_total;
		stats.particle_sim_ms = self().particle().state().particle_sim_ms_last;
		stats.particle_gpu_ms = self().particle().state().particle_gpu_ms_last;
		stats.particle_compact_ms = self().particle().state().particle_compact_ms_last;
		//最近一次粒子绘制路径（第三百四十八轮；0 未登记 / 1 CPU / 2 GPU 直接 / 3 GPU 间接）
		stats.particle_draw_path = self().particle().state().particle_draw_path_last;
		return stats;
	}

}
