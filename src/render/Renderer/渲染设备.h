#pragma once
//公共语义类型
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;
	struct Device_State;
	struct Render_Core_Context;
	struct Frame_Dispatch_Input;
	struct Frame_Render_Item;

	//设备域（核心模块、帧编排、离屏目标、输出与后处理）
	class Render_Device
	{
	public:
		explicit Render_Device(Renderer* owner);
		~Render_Device(void);
		Render_Device(const Render_Device&) = delete;
		Render_Device& operator=(const Render_Device&) = delete;

		bool init(const Renderer_Config& config);
		void shutdown(void);
		bool valid(void) const;
		bool frame_render(uint8_t* readback_pixels = nullptr, uint32_t readback_capacity = 0,
			uint32_t* readback_width = nullptr, uint32_t* readback_height = nullptr);
		bool close_requested(void) const;
		uint64_t frame_count(void) const;
		Frame_Stats frame_stats_get(void) const;
		void frame_wait_idle(void);
		void frame_stats_reset(void);
		Renderer_Resource_Counts resource_counts(void) const;
		uint64_t scene_upload_bytes(void) const;
		uint64_t instance_upload_bytes(void) const;
		bool shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out);
		void light_fill_set(const float direction[3], const float color[3], float intensity, float specular = 1.0f);

		void ssaa_factor_set(uint32_t factor);
		uint32_t ssaa_factor(void) const;
		void msaa_samples_set(uint32_t samples);
		uint32_t msaa_samples(void) const;
		void msaa_depth_resolve_set(bool enabled);
		bool msaa_depth_resolve(void) const;
		bool hdr_targets(void) const;
		//当前生效的色调映射算子（第 776 轮；0 ACES / 1 胶片 / 2 指数 / 3 扩展 Reinhard / 4 AgX / 5 双轨）
		uint32_t hdr_tone_operator(void) const;
		uint32_t msaa_max_samples(void) const;
		bool sampler_anisotropy_supported(void) const;
		float sampler_max_anisotropy(void) const;
		float sampler_max_lod_bias(void) const;
		uint32_t extent_width(void) const;
		uint32_t extent_height(void) const;
		bool window_size_set(uint32_t width, uint32_t height);
		bool window_fullscreen_set(bool fullscreen);
		bool window_fullscreen(void) const;
		float display_scale(void) const;

		void render_target_set(uint64_t handle);
		void render_target_present_set(uint64_t handle);
		bool render_target_present_chain_set(const uint64_t* handles, uint32_t count);
		bool post_chain_set(const uint64_t* handles, uint32_t count);
		/*
		泛光预设（第 778 轮 · 通用引擎化整改；第 783 轮支持显式尺寸）
		泛光不再由引擎在背后自动介入配置：调用方显式调用 build 建立三个 16F 中间目标与三条管线，
		再用 post_chain_set([提取,模糊,叠加,末级],4) 与 render_target_present_chain_set([场景,目标0,目标1,目标2],4)
		自行装配，最后用 params 按描述下发三级参数。build 为空描述时按默认值建立；
		width / height 为 0 时取当前客户区尺寸，非 0 时按该尺寸建立（供离屏 / SSAA / 自定义分辨率使用）。
		*/
		bool post_bloom_preset_build(Bloom_Preset& preset, const Bloom_Preset_Desc& desc = Bloom_Preset_Desc(),
			uint32_t width = 0, uint32_t height = 0);
		bool post_bloom_preset_params(const Bloom_Preset& preset, const Bloom_Preset_Desc& desc);
		void post_bloom_preset_release(Bloom_Preset& preset);
		uint32_t post_chain_count(void) const;
		uint32_t post_chain_get(uint64_t* out, uint32_t capacity) const;
		uint32_t post_chain_sample_counts(uint32_t* out, uint32_t capacity) const;
		void post_lut_bind(uint64_t texture_handle);
		uint64_t post_lut_get(void) const;
		/*
		逐材质色调编号来源（第 812 轮 · 8 位路径段 4）
		指定一个带编号附件的渲染目标：后处理采样集 binding 10 将绑定该目标的编号视图，
		`材质色调映射.frag` 按「id 附件 > alpha > 全局」选曲线；传 0 撤回（binding 10 写 1×1 零图）。
		*/
		void post_tone_id_target_set(uint64_t render_target_handle);
		uint64_t post_tone_id_target(void) const;
		/*
		全局后处理回落参数（第 795 轮补注；原 namesake 为「泛光参数」，自动泛光已于 778 轮删除）
		设置链级参数在「该级未专设」时的回落值：threshold / knee / intensity 分别写入全局槽 x/y/z；
		逐级专设（post_stage_param_set / post_stage_param4_set）优先。
		该全局槽的 w 与 x/y 另被 HDR 默认路径占用：w = 全局默认色调算子、x/y = 双轨分轨位置与过渡半宽。
		*/
		void bloom_param_set(float threshold, float knee, float intensity);
		void post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity);
		/*
		逐材质色调映射的回落算子（第 781 轮）
		显式装配的后处理链若使用 材质色调映射.frag，可用本接口指定该级的全局回落算子（0..5）；
		未覆盖像素用该值，逐材质覆盖仍按场景 alpha 优先。越界按 0（ACES）。
		*/
		void post_stage_tone_operator_set(uint32_t stage, uint32_t tone_operator);
		//读取后处理链某一级生效的色调算子（第 805 轮；未专设时回落全局默认值，级序无效返回 0）
		uint32_t post_stage_tone_operator_get(uint32_t stage) const;
		/*
		逐材质色调编号通道开关（第 808 轮 · 8 位路径方案 B）
		开启后，帧链路在场景 pass 之后以同一深度图重绘不透明项，把每个可见片元所属材质的
		色调算子写入目标的 R8_UINT 编号附件（由 render_target_create_format_ex 的 tone_id_format 建立）；
		目标不带编号附件或编号管线未登记时不产生任何额外命令，默认关闭。
		*/
		void tone_id_pass_set(bool enabled);
		bool tone_id_pass(void) const;
		uint32_t post_stage_param_get(uint32_t stage, float out[4]) const;
		void post_stage_param4_set(uint32_t stage, float x, float y, float z, float w);
		void post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w);
		void post_stage_color_set(uint32_t stage, float r, float g, float b);
		void post_stage_color4_set(uint32_t stage, float r, float g, float b, float a);
		void post_stage_extra_set(uint32_t stage, float x, float y, float z, float w);
		uint32_t post_chain_params_get(float* out, uint32_t capacity) const;

		void cull_enabled_set(bool enabled);
		bool cull_enabled(void) const;
		void static_batching_set(bool enabled);
		bool static_batching(void) const;
		void render_threads_set(uint32_t count);
		uint32_t render_threads(void) const;
		void cull_parallel_min_instances_set(uint32_t count);
		uint32_t cull_parallel_min_instances(void) const;
		void instance_scan_parallel_min_set(uint32_t count);
		uint32_t instance_scan_parallel_min(void) const;

		bool hiz_ensure(void);
		bool cluster_ensure(void);
		void cluster_destroy(void);
		bool cluster_placeholder_ensure(void);
		bool cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity);
		void cluster_graphics_destroy(void);
		bool compute_buffer_create_usage(Compute_Buffer_Handle& handle_out, uint32_t size, const void* data,
			uint32_t extra_usage);
		uint64_t compute_buffer_native(uint64_t handle) const;
		void outline_item_push(const Frame_Render_Item& item);
		void spot_shadow_tiles_ensure(std::size_t max_tiles);
		void spot_shadow_atlas_apply(void);
		bool spot_shadow_atlas_active(void) const;
		void shadow_invalidate(void);
		void shadow_bias_tiles_get(float out[8]) const;
		void indirect_first_instance_supported_set(bool supported);
		//资源管线卸载后摘除后处理引用并清除逐级参数
		bool pipeline_unloaded(uint64_t handle, std::size_t& remaining_stages);
		//解除设备帧路径对一个即将销毁渲染目标的全部引用
		void render_target_references_release(uint64_t handle);
		//当前方向光阴影目标句柄
		uint64_t shadow_target_handle(void) const;
		//阴影管线与目标组的值快照和整体替换
		uint64_t shadow_pipeline_handle(void) const;
		void shadow_pipeline_set(uint64_t handle);
		void shadow_targets_get(uint64_t& directional, uint64_t spot_targets[8]) const;
		void shadow_targets_replace(uint64_t directional, const uint64_t spot_targets[8]);
		uint64_t shadow_pass_total(void) const;
		//网格实例剔除、容量检查与材质实例追加
		bool mesh_single_prepare(const float* transform, bool bounds_valid,
			const float bounds_min[3], const float bounds_max[3], const Instance_Material_Values& material,
			bool& registered, uint64_t& instance_offset);
		bool mesh_instances_prepare(const float* transforms, uint32_t count, bool bounds_valid,
			const float bounds_min[3], const float bounds_max[3], const Instance_Material_Values& material,
			uint32_t& visible_count, uint64_t& instance_offset);
		//复制推送块并登记网格或粒子绘制项
		void mesh_item_push(Frame_Render_Item item, const std::array<float, 32>* push_constants);
		void particle_item_push(Frame_Render_Item item, const std::array<float, 32>& push_constants);
		std::size_t frame_item_count(void) const;
		std::size_t frame_push_count(void) const;
		bool particle_item_finish(std::size_t items_before, std::size_t push_before,
			uint64_t default_view, uint64_t default_sampler, uint64_t depth_view,
			uint64_t depth_sampler, float soft_range);
		//逐帧槽绑定粒子场景、状态与紧凑索引缓冲
		bool particle_scene_buffers_bind(uint64_t state_buffer, uint64_t index_buffer);

		//其余域创建时注入的窄核心上下文
		Render_Core_Context* core_context(void);

	private:
		void frame_stage_input_events(Renderer* owner, Device_State& state);
		void frame_stage_shadow_matrices(Renderer* owner, Device_State& state);
		bool frame_stage_spot_shadow_matrices(Renderer* owner, Device_State& state);
		void frame_stage_descriptor_sets(Renderer* owner, Device_State& state, uint32_t slot);
		bool frame_stage_instance_buffers(Renderer* owner, Device_State& state, uint32_t slot);
		bool frame_stage_shadow_pass(Renderer* owner, Device_State& state, bool spot_shadow_light_valid);
		bool frame_stage_scene_ubo(Renderer* owner, Device_State& state, uint32_t slot,
			const float view_proj[16], bool spot_shadow_light_valid);
		void frame_stage_sky_item(Renderer* owner, Device_State& state, uint32_t slot,
			const float view_proj[16], uint64_t sky_instance_offset, bool sky_frame);
		bool frame_stage_dispatch(Renderer* owner, Device_State& state, const Frame_Dispatch_Input& input);
		void output_reset(void);
		bool swapchain_rebuild_deferred(uint32_t width, uint32_t height);
		bool post_mesh_ensure(void);
		bool post_pipeline_ensure(void);
		bool post_resources_ensure(void);
		bool ssaa_pipeline_ensure(void);
		bool sky_pipeline_ensure(void);
		bool ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor);

		Renderer* owner = nullptr;
		std::unique_ptr<Device_State> state_ptr;
		std::unique_ptr<Render_Core_Context> core_context_ptr;
	};
}
