#include "src/render/Swapchain/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（深度图像创建 / 视图创建 / 销毁）
#include "src/render/Memory/内存上下文.h"

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
	创建图像可用与呈现完成信号量
	按 §5.2 归属表，两者均属「每交换链图像」，数量 = 交换链图像数（K10）；
	随交换链重建一并销毁与重建，故由本模块持有。
	呈现完成信号量按图像持有：同一信号量只会在同一张交换链图像被再次获取时才复用，
	而那时上一次 present 必然已完成（帧栅栏只保证提交完成，不保证 present 已取走信号量）。
	*/
	image_available_semaphores.resize(actual_count);
	render_finished_semaphores.resize(actual_count);
	//信号量创建信息（复用同一份）
	VkSemaphoreCreateInfo semaphore_info{};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (uint32_t index = 0; index < actual_count; ++index)
	{
		//若任一图像可用信号量创建失败
		if (!vk_result_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphores[index]), "vkCreateSemaphore"))
			return false;
		//若任一呈现完成信号量创建失败
		if (!vk_result_check(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphores[index]), "vkCreateSemaphore"))
			return false;
	}

		//创建深度图像与视图并完成首帧前的一次布局转换
		if (!depth_resources_create())
			return false;

		return true;
	}

	/*
	选择深度格式
	优先 D32_SFLOAT，回退 D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT；
	候选项须经 vkGetPhysicalDeviceFormatProperties 的 optimalTilingFeatures 查
	VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT；均不可用则记错误返回假。
	*/
	bool Swapchain_Context::depth_format_select(void)
	{
		//候选深度格式（按优先级）
		const VkFormat candidates[3] = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
		};
		//逐项检查深度附件特性
		for (VkFormat candidate : candidates)
		{
			//查询该格式的属性
			VkFormatProperties properties{};
			vkGetPhysicalDeviceFormatProperties(physical_device, candidate, &properties);
			//若支持深度 / 模板附件特性
			if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				depth_format = candidate;
				return true;
			}
		}

		logger.error("交换链:: 无任何可用深度格式（D32/D24 均不可作深度附件）");
		return false;
	}

	/*
	创建深度图像与视图
	每交换链图像一张；创建完成后（首次）执行一次布局转换，使后续帧直接以其为深度附件。
	*/
	bool Swapchain_Context::depth_resources_create(void)
	{
		//前置：内存模块与逻辑设备必须齐备
		if (memory == nullptr || !memory->valid())
		{
			logger.error("交换链:: 内存上下文不可用，无法创建深度图像");
			return false;
		}
		//选择深度格式（若尚未选定）
		if (depth_format == VK_FORMAT_UNDEFINED && !depth_format_select())
			return false;

		//深度图像数量 = 交换链图像数
		uint32_t count = static_cast<uint32_t>(images.size());
		depth_images.assign(count, VK_NULL_HANDLE);
		depth_allocations.assign(count, VK_NULL_HANDLE);
		depth_views.assign(count, VK_NULL_HANDLE);

		//逐张创建深度图像与视图
		for (uint32_t index = 0; index < count; ++index)
		{
			//创建深度图像（设备本地、深度附件用途）
			if (!memory->image_create(depth_images[index], depth_allocations[index], extent.width, extent.height,
				depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
				return false;
			//创建深度图像视图
			if (!memory->image_view_create(depth_views[index], depth_images[index], depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
				return false;
		}

		//首帧前转一次（后续帧沿用，故仅首次执行）
		if (!depth_initialized)
		{
			if (!depth_transition())
				return false;
			depth_initialized = true;
		}

		logger.info("交换链:: 深度图像已建立（{} 张，格式 {}）", count, static_cast<int32_t>(depth_format));
		return true;
	}

	//销毁深度图像与视图
	void Swapchain_Context::depth_resources_destroy(void)
	{
		//内存模块就绪时归还深度图像与视图
		if (memory != nullptr)
		{
			for (std::size_t index = 0; index < depth_views.size(); ++index)
			{
				//归还深度图像视图
				memory->image_view_destroy(depth_views[index]);
				//归还深度图像
				memory->image_destroy(depth_images[index], depth_allocations[index]);
			}
		}

		//清空容器并复位状态
		depth_views.clear();
		depth_images.clear();
		depth_allocations.clear();
		depth_initialized = false;
		depth_format = VK_FORMAT_UNDEFINED;
	}

	/*
	深度图像一次性布局转换
	UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL：srcStage=TOP_OF_PIPE、srcAccess=0、
	dstStage=EARLY|LATE_FRAGMENT_TESTS、dstAccess=DEPTH_STENCIL_ATTACHMENT_WRITE（U5）。
	经临时命令池一次性提交并等待完成，不依赖帧模块。
	*/
	bool Swapchain_Context::depth_transition(void)
	{
		//前置：设备与深度图像齐备
		if (device == VK_NULL_HANDLE || depth_images.empty())
			return false;

		//临时命令池
		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = graphics_family;
		VkCommandPool pool = VK_NULL_HANDLE;
		//若命令池创建失败
		if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "交换链:: 深度转换创建临时命令池"))
			return false;

		//命令缓冲
		VkCommandBufferAllocateInfo alloc_info{};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		//若命令缓冲分配失败
		if (!vk_result_check(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "交换链:: 深度转换分配命令缓冲"))
		{
			vkDestroyCommandPool(device, pool, nullptr);
			return false;
		}

		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "交换链:: 深度转换开始录制");

		//逐张深度图像转换布局
		for (VkImage depth_image : depth_images)
		{
			//布局转换屏障（UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL）
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = depth_image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "交换链:: 深度转换结束录制");

		//完成栅栏（未触发态）
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VkFence fence = VK_NULL_HANDLE;
		//若栅栏创建失败
		if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &fence), "交换链:: 深度转换创建栅栏"))
		{
			vkDestroyCommandPool(device, pool, nullptr);
			return false;
		}

		//获取图形队列
		VkQueue queue = VK_NULL_HANDLE;
		vkGetDeviceQueue(device, graphics_family, 0, &queue);

		//提交并等待完成
		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;
		bool ok = vk_result_check(vkQueueSubmit(queue, 1, &submit_info, fence), "交换链:: 深度转换提交");
		//若提交成功则等待其完成
		if (ok)
			ok = vk_result_check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "交换链:: 深度转换等待完成");

		//回收栅栏与命令池
		vkDestroyFence(device, fence, nullptr);
		vkDestroyCommandPool(device, pool, nullptr);
		return ok;
	}
}

