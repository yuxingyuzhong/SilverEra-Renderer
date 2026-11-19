#pragma once
//核心模块与生效配置
#include "渲染上下文.h"
//帧录制与阴影分组状态
#include "帧录制状态.h"
#include "阴影域状态.h"
//后处理常量所需管线类型
#include "Engine/EngineRenderer/src/render/Pipeline/管线.h"

//游戏引擎命名空间
namespace engine
{
	//后处理链级数上限
	inline constexpr uint32_t POST_CHAIN_MAX_STAGES = 8;
	//呈现目标上限
	inline constexpr uint32_t PRESENT_TARGET_MAX = Pipeline_Context::POST_SET_PER_SLOT;

	//设备域独占状态（核心模块、生效配置、输出状态，以及帧录制和阴影分组）
	struct Device_State : public Device_Frame_State, public Device_Shadow_State
	{
		Renderer* owner = nullptr;
		using Point_Light = Scene_State::Point_Light;
		static constexpr uint32_t POINT_LIGHT_UBO_MAX = Scene_State::POINT_LIGHT_UBO_MAX;
		static constexpr uint32_t POINT_LIGHT_MAX = Scene_State::POINT_LIGHT_MAX;

		bool swapchain_rebuild_deferred(uint32_t width, uint32_t height);
		bool hiz_ensure(void);
		bool cluster_ensure(void);
		void cluster_destroy(void);
		bool cluster_placeholder_ensure(void);
		bool cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity);
		void cluster_graphics_destroy(void);
		static void hiz_post_scene_entry(VkCommandBuffer command_buffer, VkImage depth_image, VkImageView depth_view,
			VkExtent2D extent, void* user_data);
		void outline_item_push(const Frame_Render_Item& item);
		bool post_mesh_ensure(void);
		bool post_pipeline_ensure(void);
		bool post_resources_ensure(void);
		bool post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler,
			VkImageView depth_view = VK_NULL_HANDLE);
		bool ssaa_pipeline_ensure(void);
		bool sky_pipeline_ensure(void);
		bool ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor);
		void ssaa_targets_destroy(void);
		bool hdr_scene_ensure(uint32_t width, uint32_t height, uint32_t samples = 1);
		void hdr_scene_destroy(void);
		//场景管线的颜色格式（第 757 轮：HDR 默认路径开启时烘焙为 16F）
		VkFormat scene_color_format(void) const
		{
			return hdr_targets ? VK_FORMAT_R16G16B16A16_SFLOAT : swapchain.format_get();
		}

		//平台模块
		Platform_Context platform;
		//设备模块
		Device_Context device;
		//内存模块
		Memory_Context memory;
		//交换链模块
		Swapchain_Context swapchain;
		//帧模块
		Frame_Context frame;
		//生效配置
		Renderer_Config config;
		//是否已初始化
		bool initialized = false;
		//设备是否支持间接绘制命令的 firstInstance 非零
		bool indirect_first_instance_supported = false;
		//当前 MSAA 采样数（1 = 关闭）
		uint32_t msaa_samples = 1;
		//MSAA 深度解析开关
		bool msaa_depth_resolve = true;
		//本帧实际生效的级联接缝淡化比例
		float cascade_blend_effective = 0.0f;
		//自校准偏置的参考锥远平面
		static constexpr float SPOT_BIAS_REFERENCE_SPAN = 9.0f;
		//各片光源空间深度跨度
		float cascade_depth_span[3] = { 59.0f, 59.0f, 59.0f };

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
		/*
		逐材质色调编号附件（第 812 轮）
		post_tone_id_target 为门面设定的「编号来源目标」句柄（0 = 未设定，binding 10 写默认零图）；
		post_bound_tone_id 为上次写入的编号视图（参与绑定签名比较）。
		*/
		uint64_t post_tone_id_target = 0;
		VkImageView post_bound_tone_id = VK_NULL_HANDLE;

		//设备是否支持 144 字节推送块
		bool post_extra_supported = false;

		/*
		全局后处理参数（第 795 轮由 bloom_param 更名：自动泛光已于 778 轮删除，该槽不再是泛光专属）
		x/y = 双轨色调映射的分轨位置 / 过渡半宽（算子 5）或链级阈值 / 软膝回落值；
		z = 链级强度回落值；w = 全局默认色调算子（逐材质未覆盖时使用）。
		*/
		float global_post_param[4] = { 0.6f, 0.1f, 1.0f, 0.0f };

		//SSAA 超采样状态（倍率钳制 1..4；1 = 关闭）
		uint32_t ssaa_factor = 1;

		//离屏渲染目标默认 16F 开关（第 756 轮；默认关闭，关闭时逐字节不变）
		bool hdr_targets = false;
		//HDR 默认路径的色调映射算子（第 759 轮；0 = ACES、1 = 胶片、2 = 指数、3 = 扩展 Reinhard、4 = AgX）
		uint32_t hdr_tone_operator = 0;
		//双轨色调映射的分轨位置与过渡半宽（第 774 轮；默认与第七百七十三轮常量一致）
		float hdr_dual_tone_split = 0.5f;
		float hdr_dual_tone_band = 0.1f;

		/*
		HDR 默认路径（第 757 轮）
		hdr_targets 开启且本帧未显式指定呈现 / 渲染目标时，内部以该 16F 场景目标接管场景渲染，
		再由内置色调映射级呈现到交换链；目标随交换链尺寸变化重建。
		*/
		uint64_t hdr_scene_target = 0;
		uint32_t hdr_scene_width = 0;
		uint32_t hdr_scene_height = 0;
		//HDR 场景目标的采样数（第 766 轮；与设备域 msaa_samples 同步，1 = 关闭）
		uint32_t hdr_scene_samples = 1;
		bool hdr_scene_failed = false;
		//本帧生效的呈现目标（用户设定 → 该句柄；否则 hdr 路径 → hdr_scene_target；其余 0）
		uint64_t effective_present_target = 0;

		uint64_t ssaa_scene_target = 0;

		uint64_t ssaa_resolve_target = 0;

		uint32_t ssaa_extent_width = 0;

		uint32_t ssaa_extent_height = 0;

		uint32_t ssaa_target_factor = 0;

		uint64_t ssaa_pipeline = 0;

		bool ssaa_extent_failed = false;

		/*
		逐材质色调编号通道（第 808 轮 · 8 位路径方案 B）
		tone_id_pipeline 为极简编号管线（R8_UINT 颜色 ＋ 场景深度、LESS_OR_EQUAL、不写深度）；
		tone_id_pass 为开关（默认关闭）：仅当渲染目标带编号附件时帧链路才录制该通道。
		*/
		uint64_t tone_id_pipeline = 0;
		bool tone_id_pass = false;
	};
}
