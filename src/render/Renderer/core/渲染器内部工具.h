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
}
