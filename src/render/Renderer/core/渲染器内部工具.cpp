#include "../局部命名空间使用.h"
#include "渲染器内部工具.h"
#include "渲染器实现.h"

namespace engine
{
	/*
	查询颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十六轮）
	按 8 → 4 → 2 依次探测两种格式的「最优平铺 ＋ 对应附件用途」图像属性；
	返回不超过 requested 的最高支持值；均不支持则返回 1（关闭）。
	*/
	uint32_t msaa_supported_max(VkPhysicalDevice physical_device, VkFormat color_format,
		VkFormat depth_format, uint32_t requested)
	{
		//颜色格式属性（含其支持的采样数位集）
		VkImageFormatProperties color_properties{};
		if (vkGetPhysicalDeviceImageFormatProperties(physical_device, color_format, VK_IMAGE_TYPE_2D,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &color_properties) != VK_SUCCESS)
			return 1;
		//深度格式属性（含其支持的采样数位集）
		VkImageFormatProperties depth_properties{};
		if (vkGetPhysicalDeviceImageFormatProperties(physical_device, depth_format, VK_IMAGE_TYPE_2D,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depth_properties) != VK_SUCCESS)
			return 1;
		//候选采样数（自高到低；两种格式须同时支持）
		const uint32_t candidates[3] = { 8, 4, 2 };
		for (uint32_t candidate : candidates)
		{
			//超过请求值则跳过
			if (candidate > requested)
				continue;
			VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(candidate);
			//颜色与深度格式均须支持该采样数
			if ((color_properties.sampleCounts & samples) != 0 && (depth_properties.sampleCounts & samples) != 0)
				return candidate;
		}
		return 1;
	}
}
