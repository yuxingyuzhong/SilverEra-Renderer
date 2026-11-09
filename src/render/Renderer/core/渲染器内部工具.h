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
	//帧阶段：阴影 pass（按需重绘；返回假表示本帧失败）
	bool frame_stage_shadow_pass(Renderer_Impl_Holder::Impl* impl, bool spot_shadow_light_valid);
	//帧阶段：场景 UBO 组装与上传（返回假表示本帧失败）
	bool frame_stage_scene_ubo(Renderer_Impl_Holder::Impl* impl, uint32_t slot, const float view_proj[16], bool spot_shadow_light_valid);
}
