#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//选择合成模式：优先不透明，否则取支持列表中的最低位
	static VkCompositeAlphaFlagBitsKHR composite_alpha_select(VkCompositeAlphaFlagsKHR supported)
	{
		//若支持不透明合成
		if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
			return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

		//候选合成模式（除不透明外）
		const VkCompositeAlphaFlagBitsKHR candidates[] = {
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		//逐项匹配受支持者
		for (VkCompositeAlphaFlagBitsKHR candidate : candidates)
			//若该模式受支持
			if (supported & candidate)
				return candidate;

		//兜底不透明（能力查询保证至少支持一项）
		return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}

	//创建交换链与图像视图、图像可用信号量
	bool Swapchain_Context::swapchain_create(uint32_t width, uint32_t height)
	{
		//查询表面能力
		VkSurfaceCapabilitiesKHR capabilities{};
		if (!capabilities_query(capabilities))
			return false;

		//选择表面格式与色彩空间（K3）
		VkSurfaceFormatKHR surface_format{};
		if (!format_select(surface_format))
			return false;

		//选择呈现模式（K4）
		VkPresentModeKHR present_mode = present_mode_select(vsync);

		//确定交换链范围（K6）
		VkExtent2D selected_extent = extent_select(capabilities, width, height);

		/*
		确定图像数量
		请求 minImageCount + 1；maxImageCount 为 0 表示无上限（K5）。
		*/
		uint32_t image_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
			image_count = capabilities.maxImageCount;

		//图像用法：颜色附件（与能力求交，避免能力不足时创建失败）
		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT & capabilities.supportedUsageFlags;
		//若颜色附件用法不受支持
		if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
		{
			logger.error("交换链:: 表面不支持颜色附件用法");
			return false;
		}

		//合成模式（优先不透明）
		VkCompositeAlphaFlagBitsKHR composite_alpha = composite_alpha_select(capabilities.supportedCompositeAlpha);

		/*
		共享方式
		图形族与呈现族相同用独占；不同用并发（本层不做队列族所有权转移）。
		*/
		uint32_t family_indices[2] = { graphics_family, present_family };
		VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
		//若图形族与呈现族不同
		if (graphics_family != present_family)
			sharing_mode = VK_SHARING_MODE_CONCURRENT;

		//交换链创建信息
		VkSwapchainCreateInfoKHR create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create_info.surface = surface;
		create_info.minImageCount = image_count;
		create_info.imageFormat = surface_format.format;
		create_info.imageColorSpace = surface_format.colorSpace;
		create_info.imageExtent = selected_extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = usage;
		create_info.imageSharingMode = sharing_mode;
		//若用并发共享则给出两个队列族
		if (sharing_mode == VK_SHARING_MODE_CONCURRENT)
		{
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = family_indices;
		}
		create_info.preTransform = capabilities.currentTransform;
		create_info.compositeAlpha = composite_alpha;
		create_info.presentMode = present_mode;
		create_info.clipped = VK_TRUE;
		/*
		旧交换链句柄
		重建时已在 rebuild 中经 vkDeviceWaitIdle 并销毁旧交换链，故此处恒为空（§5.3）。
		*/
		create_info.oldSwapchain = VK_NULL_HANDLE;

		//创建交换链
		if (!vk_result_check(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain), "vkCreateSwapchainKHR"))
		{
			swapchain = VK_NULL_HANDLE;
			return false;
		}

		//记录格式与范围
		format = surface_format.format;
		extent = selected_extent;

		//获取交换链图像数量
		uint32_t actual_count = 0;
		if (!vk_result_check(vkGetSwapchainImagesKHR(device, swapchain, &actual_count, nullptr), "vkGetSwapchainImagesKHR"))
			return false;
		//收集交换链图像
		images.resize(actual_count);
		if (!vk_result_check(vkGetSwapchainImagesKHR(device, swapchain, &actual_count, images.data()), "vkGetSwapchainImagesKHR"))
			return false;
		//按实际数量收敛（防御性）
		images.resize(actual_count);

		//创建图像视图（二维颜色附件）
		image_views.resize(actual_count);
		for (uint32_t index = 0; index < actual_count; ++index)
		{
			//图像视图创建信息
			VkImageViewCreateInfo view_info{};
			view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image = images[index];
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = format;
			view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount = 1;
			//若图像视图创建失败
			if (!vk_result_check(vkCreateImageView(device, &view_info, nullptr, &image_views[index]), "vkCreateImageView"))
				return false;
		}

		/*
		创建图像可用信号量
		按 §5.2 归属表，图像可用信号量属「每交换链图像」，数量 = 交换链图像数（K10）；
		随交换链重建一并销毁与重建，故由本模块持有。
		*/
		image_available_semaphores.resize(actual_count);
		//信号量创建信息（复用同一份）
		VkSemaphoreCreateInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (uint32_t index = 0; index < actual_count; ++index)
			//若任一信号量创建失败
			if (!vk_result_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphores[index]), "vkCreateSemaphore"))
				return false;

		return true;
	}
}
