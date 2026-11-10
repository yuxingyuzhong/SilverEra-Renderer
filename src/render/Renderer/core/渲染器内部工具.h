#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//实现体定义（提供 Vulkan 类型）
#include "渲染器实现.h"

namespace engine
{
	/*
	查询颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十六轮）
	生命周期与采样尺寸两处共用，故独立于任意单个实现文件。
	*/
	uint32_t msaa_supported_max(VkPhysicalDevice physical_device, VkFormat color_format,
		VkFormat depth_format, uint32_t requested);

	//按需创建聚光灯阴影瓦片视图（跨接口共用；实现在 渲染器_雾与阴影.cpp）
	void spot_shadow_tiles_ensure_impl(Renderer_Impl_Holder::Impl* impl, std::size_t max_tiles);
	//聚光灯阴影图集切换（跨接口共用；实现在 渲染器_雾与阴影.cpp）
	void spot_shadow_atlas_apply_impl(Renderer_Impl_Holder::Impl* impl);

	//帧阶段：泵事件 + 本帧输入锁存与原生 UI 交互（实现在 渲染器_帧阶段.cpp）
	void frame_stage_input_events(Renderer_Impl_Holder::Impl* impl);
	//帧阶段：光源空间矩阵与级联拟合
	void frame_stage_shadow_matrices(Renderer_Impl_Holder::Impl* impl);
	//帧阶段：聚光阴影矩阵与瓦片选择（返回本帧是否存在有效聚光阴影）
	bool frame_stage_spot_shadow_matrices(Renderer_Impl_Holder::Impl* impl);
	//帧阶段：为本帧待绘制项填装描述符集与实例缓冲句柄
	void frame_stage_descriptor_sets(Renderer_Impl_Holder::Impl* impl, uint32_t slot);
	//帧阶段：实例模型矩阵写入本帧槽实例缓冲（返回假表示本帧失败）
	bool frame_stage_instance_buffers(Renderer_Impl_Holder::Impl* impl, uint32_t slot);
	//帧阶段：阴影 pass（按需重绘；返回假表示本帧失败）
	bool frame_stage_shadow_pass(Renderer_Impl_Holder::Impl* impl, bool spot_shadow_light_valid);
	//帧阶段：场景 UBO 组装与上传（返回假表示本帧失败）
	bool frame_stage_scene_ubo(Renderer_Impl_Holder::Impl* impl, uint32_t slot, const float view_proj[16], bool spot_shadow_light_valid);
	//帧阶段：追加天空盒绘制项
	void frame_stage_sky_item(Renderer_Impl_Holder::Impl* impl, uint32_t slot, const float view_proj[16], VkDeviceSize sky_instance_offset, bool sky_frame);

	//帧分派输入（主链 → 分派阶段；只读）
	struct Frame_Dispatch_Input
	{
		//窗口客户区尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		//本帧槽序号
		uint32_t slot = 0;
		//本帧是否已重建交换链
		bool rebuilt = false;
		//同帧多 pass 后处理路径开关 / 链模式
		bool post_frame = false;
		bool post_chain_mode = false;
		//SSAA 路径开关
		bool ssaa_frame = false;
		//后处理 / SSAA 首个实例的字节偏移
		VkDeviceSize post_instance_offset = 0;
		VkDeviceSize ssaa_instance_offset = 0;
		//视图投影矩阵（列主序 16 浮点）
		const float* view_proj = nullptr;
		//UI 阶段回调
		Frame_Ui_Callback ui_callback{};
		//像素回读请求（调用方缓冲）
		uint8_t* readback_pixels = nullptr;
		uint32_t readback_capacity = 0;
		uint32_t* readback_width = nullptr;
		uint32_t* readback_height = nullptr;
	};
	//帧阶段：分派渲染路径（SSAA / 多 pass 后处理 / 离屏 / 交换链）并交付回读（返回假表示本帧失败）
	bool frame_stage_dispatch(Renderer_Impl_Holder::Impl* impl, const Frame_Dispatch_Input& input);

	//初始化阶段：管线 / 描述符 / 采样 / 粒子 / 文字 / 阴影 / 后处理 / 天空 等图形资源（实现在 渲染器_初始化_图形资源.cpp）
	bool renderer_init_graphics(Renderer* owner, Renderer_Impl_Holder::Impl* impl, const Renderer_Config& effective);

	//初始化阶段：平台 / 实例 / 设备 / 表面
	bool renderer_init_platform(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective);
	//初始化阶段：VMA 分配器 / 交换链 / 在途帧数钳制
	bool renderer_init_memory_swapchain(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective);
	//初始化阶段：帧 / 着色器模块 / 描述符布局与管线
	bool renderer_init_frame_shader_pipeline(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective);
	//初始化阶段：网格 / 渲染目标 / 字体 / 相机
	bool renderer_init_services(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective);
	//初始化阶段：UI 通道 / 事件桥接 / 原生 UI
	bool renderer_init_ui(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective);
}
