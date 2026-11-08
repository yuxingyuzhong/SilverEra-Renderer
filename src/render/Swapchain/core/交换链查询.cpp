#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//查询表面能力
	bool Swapchain_Context::capabilities_query(VkSurfaceCapabilitiesKHR& capabilities_out) const
	{
		return vk_result_check(
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities_out),
			"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	}

	/*
	选择表面格式
	优先 B8G8R8A8_SRGB / R8G8B8A8_SRGB，其次对应 _UNORM，最后兜底首个可用格式；
	色彩空间随所选格式（优先 SRGB_NONLINEAR）。不硬编码格式（K3 / §6.3）。
	*/
	bool Swapchain_Context::format_select(VkSurfaceFormatKHR& format_out) const
	{
		//查询支持的格式数量
		uint32_t format_count = 0;
		if (!vk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR"))
			return false;
		//若没有任何可用格式
		if (format_count == 0)
		{
			logger.error("交换链查询:: 表面不提供任何可用格式");
			return false;
		}

		//收集格式清单
		vector<VkSurfaceFormatKHR> formats(format_count);
		if (!vk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR"))
			return false;

		/*
		仅一项且格式为 VK_FORMAT_UNDEFINED：表示「任意格式皆可」，
		按首选格式取 B8G8R8A8_SRGB，并配 SRGB_NONLINEAR 色彩空间。
		*/
		if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		{
			format_out.format = VK_FORMAT_B8G8R8A8_SRGB;
			format_out.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			return true;
		}

		//首选格式序列（sRGB 优先，其次 UNORM）
		const VkFormat preferred_formats[] = {
			VK_FORMAT_B8G8R8A8_SRGB,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_FORMAT_R8G8B8A8_UNORM,
		};
		//按优先级匹配格式
		for (VkFormat preferred : preferred_formats)
			for (const auto& candidate : formats)
				//若格式命中
				if (candidate.format == preferred)
				{
					format_out = candidate;
					return true;
				}

		//兜底：首个可用格式（K3）
		format_out = formats[0];
		return true;
	}

	/*
	选择呈现模式
	vsync 为假且支持 MAILBOX 时用 MAILBOX（低延迟无撕裂）；否则用 FIFO（永远可用，K4 / §6.2）。
	*/
	VkPresentModeKHR Swapchain_Context::present_mode_select(bool vsync) const
	{
		//查询支持的呈现模式数量
		uint32_t mode_count = 0;
		//若查询失败则直接兜底 FIFO
		if (!vk_result_check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR"))
			return VK_PRESENT_MODE_FIFO_KHR;
		//若没有任何可用模式
		if (mode_count == 0)
			return VK_PRESENT_MODE_FIFO_KHR;

		//收集呈现模式清单
		vector<VkPresentModeKHR> modes(mode_count);
		//若清单获取失败则兜底 FIFO
		if (!vk_result_check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR"))
			return VK_PRESENT_MODE_FIFO_KHR;

		//若关闭垂直同步
		if (!vsync)
		{
			//逐项查找 MAILBOX
			for (VkPresentModeKHR mode : modes)
				//若支持 MAILBOX 则采用
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
					return VK_PRESENT_MODE_MAILBOX_KHR;
		}

		//兜底 FIFO（规范保证始终可用）
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	//选择交换链范围
	VkExtent2D Swapchain_Context::extent_select(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) const
	{
		//若能力给出明确范围（非 UINT32_MAX）则直接采用
		if (capabilities.currentExtent.width != UINT32_MAX)
			return capabilities.currentExtent;

		//否则用窗口客户区尺寸，并夹取到能力允许区间（K6）
		VkExtent2D extent{ width, height };
		extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		return extent;
	}
}