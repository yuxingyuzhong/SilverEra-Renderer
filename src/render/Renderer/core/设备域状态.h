#pragma once
//依赖：core/渲染器实现.h 已提供模块类型与后处理常量

//游戏引擎命名空间
namespace engine
{
	//设备域状态（从 Impl 迁出的渲染目标选择、后处理与超采样成员）
	struct Device_State
	{
		//本帧渲染目标句柄（0 = 直接渲染到交换链）
		uint64_t active_target = 0;

		//全屏四边形网格句柄（0 = 尚未创建；后处理 pass 的几何）
		Mesh_Handle fullscreen_mesh = 0;

		//本帧同帧后处理呈现目标句柄（0 = 关闭）
		uint64_t present_target = 0;

		//本帧同帧后处理链的额外中间目标句柄
		std::vector<uint64_t> present_extra;

		//后处理管线句柄（0 = 尚未登记）
		uint64_t post_pipeline = 0;

		//本帧后处理链（空 = 走既有单级默认后处理路径）
		std::vector<uint64_t> post_chain;

		//逐级后处理参数与有效标记
		float post_stage_param[POST_CHAIN_MAX_STAGES][4] = {};

		bool post_stage_param_valid[POST_CHAIN_MAX_STAGES] = {};

		//逐级描边色与有效标记
		float post_stage_color[POST_CHAIN_MAX_STAGES][4] = {};

		bool post_stage_color_valid[POST_CHAIN_MAX_STAGES] = {};

		//逐级额外四参与有效标记
		float post_stage_extra[POST_CHAIN_MAX_STAGES][4] = {};

		bool post_stage_extra_valid[POST_CHAIN_MAX_STAGES] = {};

		//后处理级管线序列复用缓冲
		std::vector<uint64_t> post_stage_pipelines_scratch;

		//后处理绘制项复用缓冲
		std::vector<Frame_Render_Item> post_items_scratch;

		//后处理链目标组复用缓冲
		std::vector<Frame_Render_Target> chain_targets_scratch;

		//后处理采样集绑定签名
		VkImageView post_bound_views[PRESENT_TARGET_MAX] = {};

		uint32_t post_bound_view_count = 0;

		VkSampler post_bound_sampler = VK_NULL_HANDLE;

		VkImageView post_bound_depth = VK_NULL_HANDLE;

		VkImageView post_bound_shadow = VK_NULL_HANDLE;

		bool post_bind_cached = false;

		//设备是否支持 144 字节推送块
		bool post_extra_supported = false;

		//全局泛光参数（阈值 / 软膝 / 强度 / 保留）
		float bloom_param[4] = { 0.6f, 0.1f, 1.0f, 0.0f };

		//SSAA 超采样状态（倍率钳制 1..4；1 = 关闭）
		uint32_t ssaa_factor = 1;

		uint64_t ssaa_scene_target = 0;

		uint64_t ssaa_resolve_target = 0;

		uint32_t ssaa_extent_width = 0;

		uint32_t ssaa_extent_height = 0;

		uint32_t ssaa_target_factor = 0;

		uint64_t ssaa_pipeline = 0;

		bool ssaa_extent_failed = false;
	};
}
