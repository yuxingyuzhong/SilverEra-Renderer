#include "../局部命名空间使用.h"
//设备域完整状态与核心上下文
#include "渲染器内部状态.h"
//设备域内部辅助
#include "渲染器内部工具.h"

namespace engine
{
	//构造（独占核心模块并建立只读配置上下文）
	Render_Device::Render_Device(Renderer* owner)
		: owner(owner),
		state_ptr(new(std::nothrow) Device_State())
	{
		//状态分配成功后建立核心窄上下文
		if (state_ptr != nullptr)
		{
			state_ptr->owner = owner;
			core_context_ptr.reset(new(std::nothrow) Render_Core_Context{
				state_ptr->platform, state_ptr->device, state_ptr->memory,
				state_ptr->swapchain, state_ptr->frame, state_ptr->config });
		}
	}

	//析构（资源由设备域关闭流程统一回收）
	Render_Device::~Render_Device(void)
	{
	}

	//核心上下文
	Render_Core_Context* Render_Device::core_context(void)
	{
		return core_context_ptr.get();
	}

	//复位输出与后处理状态
	void Render_Device::output_reset(void)
	{
		//本帧渲染目标回落交换链
		state_ptr->active_target = 0;
		//关闭同帧后处理呈现路径并清空目标组
		state_ptr->present_target = 0;
		state_ptr->present_extra.clear();
		//清空后处理链与懒登记句柄（资源由对应模块统一回收）
		state_ptr->post_chain.clear();
		state_ptr->post_pipeline = 0;
		state_ptr->fullscreen_mesh = 0;
		//复位 SSAA 状态（目标已随资源域渲染目标模块销毁）
		state_ptr->ssaa_scene_target = 0;
		state_ptr->ssaa_resolve_target = 0;
		state_ptr->ssaa_extent_width = 0;
		state_ptr->ssaa_extent_height = 0;
		state_ptr->ssaa_target_factor = 0;
		state_ptr->ssaa_pipeline = 0;
		state_ptr->ssaa_factor = 1;
		state_ptr->ssaa_extent_failed = false;
		//离屏目标默认 16F 复位为关闭（第 756 轮）
		state_ptr->hdr_targets = false;
		//色调映射算子复位为 ACES（第 759 轮）
		state_ptr->hdr_tone_operator = 0;
		//自动泛光复位（第 761 轮；目标已随资源域销毁）
		state_ptr->hdr_auto_bloom = false;
		//自动泛光参数复位为默认（第 763 轮）
		state_ptr->hdr_bloom_threshold = 0.6f;
		state_ptr->hdr_bloom_knee = 0.1f;
		state_ptr->hdr_bloom_radius = 4.0f;
		state_ptr->hdr_bloom_intensity = 1.0f;
		//半分辨率自动泛光复位为关闭（第 765 轮）
		state_ptr->hdr_bloom_half_res = false;
		for (std::size_t index = 0; index < 3; ++index)
		{
			state_ptr->hdr_bloom_targets[index] = 0;
			state_ptr->hdr_bloom_pipelines[index] = 0;
		}
		state_ptr->hdr_bloom_width = 0;
		state_ptr->hdr_bloom_height = 0;
		state_ptr->hdr_bloom_failed = false;
		state_ptr->hdr_bloom_chain_active = false;
		//复位 HDR 默认路径（第 757 轮；目标已随资源域渲染目标模块销毁）
		state_ptr->hdr_scene_target = 0;
		state_ptr->hdr_scene_width = 0;
		state_ptr->hdr_scene_height = 0;
		state_ptr->hdr_scene_samples = 1;
		state_ptr->hdr_scene_failed = false;
		state_ptr->effective_present_target = 0;
		//逐级专设值失效（参数本身与全局泛光参数按原语义保留）
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			state_ptr->post_stage_param_valid[stage] = false;
			state_ptr->post_stage_color_valid[stage] = false;
			state_ptr->post_stage_extra_valid[stage] = false;
		}
	}

	//交换链延迟重建
	bool Render_Device::swapchain_rebuild_deferred(uint32_t width, uint32_t height) { return state_ptr->swapchain_rebuild_deferred(width, height); }
	//Hi-Z 资源确保
	bool Render_Device::hiz_ensure(void) { return state_ptr->hiz_ensure(); }
	//Cluster 光源缓冲确保
	bool Render_Device::cluster_ensure(void) { return state_ptr->cluster_ensure(); }
	//Cluster 光源缓冲回收
	void Render_Device::cluster_destroy(void) { state_ptr->cluster_destroy(); }
	//Cluster 占位缓冲确保
	bool Render_Device::cluster_placeholder_ensure(void) { return state_ptr->cluster_placeholder_ensure(); }
	//图形簇缓冲确保
	bool Render_Device::cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity) { return state_ptr->cluster_graphics_ensure(cluster_total, list_capacity); }
	//图形簇缓冲回收
	void Render_Device::cluster_graphics_destroy(void) { state_ptr->cluster_graphics_destroy(); }
	//扩展用法计算缓冲创建
	bool Render_Device::compute_buffer_create_usage(Compute_Buffer_Handle& handle_out, uint32_t size, const void* data,
		uint32_t extra_usage)
	{
		return owner->compute().compute_buffer_create_usage(handle_out, size, data, extra_usage);
	}
	//计算缓冲原生句柄
	uint64_t Render_Device::compute_buffer_native(uint64_t handle) const { return owner->compute().compute_buffer_native(handle); }
	//追加反向外扩描边项
	void Render_Device::outline_item_push(const Frame_Render_Item& item) { state_ptr->outline_item_push(item); }
	//聚光阴影瓦片确保
	void Render_Device::spot_shadow_tiles_ensure(std::size_t max_tiles) { spot_shadow_tiles_ensure_impl(*state_ptr, max_tiles); }
	//聚光阴影图集应用
	void Render_Device::spot_shadow_atlas_apply(void) { spot_shadow_atlas_apply_impl(*state_ptr); }
	//聚光阴影图集是否生效
	bool Render_Device::spot_shadow_atlas_active(void) const { return state_ptr->spot_shadow_atlas; }
	//阴影内容失效
	void Render_Device::shadow_invalidate(void) { state_ptr->shadow_pass_ready = false; }
	//逐瓦片有效偏置
	void Render_Device::shadow_bias_tiles_get(float out[8]) const
	{
		if (out == nullptr)
			return;
		for (std::size_t index = 0; index < 8; ++index)
			out[index] = state_ptr->spot_shadow_bias_tiles[index];
	}
	//间接绘制首实例能力设置
	void Render_Device::indirect_first_instance_supported_set(bool supported) { state_ptr->indirect_first_instance_supported = supported; }
	//当前方向光阴影目标句柄
	uint64_t Render_Device::shadow_target_handle(void) const { return state_ptr->shadow_target; }
	//当前阴影管线句柄
	uint64_t Render_Device::shadow_pipeline_handle(void) const { return state_ptr->shadow_pipeline; }
	//应用已登记的阴影管线
	void Render_Device::shadow_pipeline_set(uint64_t handle) { state_ptr->shadow_pipeline = handle; }
	//读取当前阴影目标组
	void Render_Device::shadow_targets_get(uint64_t& directional, uint64_t spot_targets[8]) const
	{
		directional = state_ptr->shadow_target;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			spot_targets[tile] = state_ptr->spot_shadow_targets[tile];
	}
	//整体应用已成功改绑的阴影目标组
	void Render_Device::shadow_targets_replace(uint64_t directional, const uint64_t spot_targets[8])
	{
		state_ptr->shadow_target = directional;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			if (spot_targets[tile] != 0)
				state_ptr->spot_shadow_targets[tile] = spot_targets[tile];
	}
	//累计阴影 pass 数
	uint64_t Render_Device::shadow_pass_total(void) const { return state_ptr->shadow_pass_count; }
	//资源管线卸载后的后处理链清理
	bool Render_Device::pipeline_unloaded(uint64_t handle, std::size_t& remaining_stages)
	{
		const std::size_t chain_before = state_ptr->post_chain.size();
		state_ptr->post_chain.erase(std::remove(state_ptr->post_chain.begin(), state_ptr->post_chain.end(), handle),
			state_ptr->post_chain.end());
		remaining_stages = state_ptr->post_chain.size();
		//级序压缩后逐级参数会错位，确有摘除时整体失效
		if (remaining_stages == chain_before)
			return false;
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			state_ptr->post_stage_param_valid[stage] = false;
			state_ptr->post_stage_color_valid[stage] = false;
			state_ptr->post_stage_extra_valid[stage] = false;
		}
		return true;
	}
	//后处理网格确保
	bool Render_Device::post_mesh_ensure(void) { return state_ptr->post_mesh_ensure(); }
	//后处理管线确保
	bool Render_Device::post_pipeline_ensure(void) { return state_ptr->post_pipeline_ensure(); }
	//后处理资源确保
	bool Render_Device::post_resources_ensure(void) { return state_ptr->post_resources_ensure(); }
	//SSAA 管线确保
	bool Render_Device::ssaa_pipeline_ensure(void) { return state_ptr->ssaa_pipeline_ensure(); }
	//天空管线确保
	bool Render_Device::sky_pipeline_ensure(void) { return state_ptr->sky_pipeline_ensure(); }
	//SSAA 目标确保
	bool Render_Device::ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor) { return state_ptr->ssaa_targets_ensure(width, height, factor); }
}
