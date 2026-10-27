#include "src/render/Memory/局部命名空间使用.h"
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	namespace
	{
		/*
		一次性提交（复用传入的命令池与栅栏）
		每次上传：重置命令池（连带回收上次分配的命令缓冲）→ 分配命令缓冲 → 录制 →
		提交 → 等待完成。命令池与栅栏由 Memory_Context 持有并跨多次上传复用，
		不再逐次创建 / 销毁，避免每资源一次的命令池与栅栏开销。
		record：在当前命令缓冲上录制全部拷贝与布局转换。
		*/
		template <typename Record_Fn>
		bool upload_submit_once(VkDevice device, VkCommandPool pool, VkFence fence,
			VkQueue graphics_queue, Record_Fn record)
		{
			//重置命令池（连带回收上次分配的命令缓冲，使其可再次分配）
			if (!vk_result_check(vkResetCommandPool(device, pool, 0), "上传批次:: 重置上传命令池"))
				return false;

			//分配命令缓冲
			VkCommandBufferAllocateInfo buffer_alloc_info{};
			buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc_info.commandPool = pool;
			buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc_info.commandBufferCount = 1;

			VkCommandBuffer command_buffer = VK_NULL_HANDLE;
			//若命令缓冲分配失败
			if (!vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc_info, &command_buffer), "上传批次:: 分配命令缓冲"))
				return false;

			//开始录制（声明为一次性提交）
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			//若开始录制失败
			if (!vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "上传批次:: 开始命令录制"))
				return false;

			//录制具体拷贝与布局转换
			record(command_buffer);

			//结束录制
			if (!vk_result_check(vkEndCommandBuffer(command_buffer), "上传批次:: 结束命令录制"))
				return false;

			/*
			重置完成栅栏后再提交
			栅栏在上一次「等待完成」后已发出，此处须先复位才能再次作为本次提交的完成信号；
			首次使用时栅栏尚未发出，复位为无操作。
			*/
			if (!vk_result_check(vkResetFences(device, 1, &fence), "上传批次:: 重置上传栅栏"))
				return false;

			//提交到图形队列（同步完成，故不设信号量）
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;

			//记录提交是否成功
			bool ok = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, fence), "上传批次:: 提交上传命令");
			//若提交成功则等待其完成
			if (ok)
				ok = vk_result_check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "上传批次:: 等待上传完成");
			return ok;
		}
	}

	//确保一次性上传的命令池与栅栏就绪（首次调用创建，之后复用；幂等）
	bool Memory_Context::upload_resources_ensure(void)
	{
		//命令池与栅栏均已就绪则直接返回
		if (upload_pool != VK_NULL_HANDLE && upload_fence != VK_NULL_HANDLE)
			return true;
		//前置：逻辑设备与队列族必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("上传批次:: 逻辑设备未就绪，无法创建上传资源");
			return false;
		}

		//创建上传命令池（一次性用途；跨多次上传经 vkResetCommandPool 复用）
		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = graphics_family;
		//若命令池创建失败
		if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &upload_pool), "上传批次:: 创建上传命令池"))
		{
			upload_pool = VK_NULL_HANDLE;
			return false;
		}

		//创建上传完成栅栏（未触发态，供等待本次提交用）
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		//若栅栏创建失败
		if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &upload_fence), "上传批次:: 创建上传栅栏"))
		{
			vkDestroyCommandPool(device, upload_pool, nullptr);
			upload_pool = VK_NULL_HANDLE;
			upload_fence = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	//上传到缓冲（暂存缓冲 → 设备本地缓冲）
	bool Memory_Context::upload_buffer(VkBuffer dst_buffer, const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		//前置：上下文与队列就绪、目标句柄与源数据齐备
		if (!valid() || dst_buffer == VK_NULL_HANDLE || data == nullptr || graphics_queue == VK_NULL_HANDLE)
		{
			logger.error("上传批次:: 上传缓冲的参数不完整");
			return false;
		}

		//创建暂存缓冲（主机可见、仅顺序写入，并用作传输源）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true))
			return false;

		//写入暂存缓冲
		if (!buffer_write(staging_buffer, staging_alloc, data, size, 0))
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//确保上传命令池与栅栏就绪（首次创建，之后复用）
		if (!upload_resources_ensure())
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//提交「暂存缓冲 → 目标缓冲」的拷贝
		bool ok = upload_submit_once(device, upload_pool, upload_fence, graphics_queue, [&](VkCommandBuffer command_buffer)
		{
			VkBufferCopy copy_region{};
			copy_region.srcOffset = 0;
			copy_region.dstOffset = offset;
			copy_region.size = size;
			vkCmdCopyBuffer(command_buffer, staging_buffer, dst_buffer, 1, &copy_region);
		});

		//回收暂存缓冲（无论成败）
		buffer_destroy(staging_buffer, staging_alloc);

		if (ok)
			logger.info("上传批次:: 缓冲上传完成（{} 字节）", size);
		return ok;
	}

	/*
	上传像素到图像
	终止布局固定为 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL（采样用途）；
	若将来需要其他终态（如作为渲染目标或再次传输），在此扩参即可。
	*/
	bool Memory_Context::upload_image(VkImage dst_image, const void* data, VkDeviceSize data_size,
		uint32_t width, uint32_t height, uint32_t row_pixels, uint32_t layer)
	{
		//前置：上下文与队列就绪、目标句柄与源数据齐备
		if (!valid() || dst_image == VK_NULL_HANDLE || data == nullptr || graphics_queue == VK_NULL_HANDLE)
		{
			logger.error("上传批次:: 上传图像的参数不完整");
			return false;
		}

		//创建暂存缓冲（主机可见、仅顺序写入，并用作传输源）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true))
			return false;

		//写入暂存缓冲
		if (!buffer_write(staging_buffer, staging_alloc, data, data_size, 0))
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//确保上传命令池与栅栏就绪（首次创建，之后复用）
		if (!upload_resources_ensure())
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//提交「布局转换 → 拷贝到图像 → 布局转换」三步
		bool ok = upload_submit_once(device, upload_pool, upload_fence, graphics_queue, [&](VkCommandBuffer command_buffer)
		{
			//布局：未定义 → 传输目标（旧内容不必保留）
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = dst_image;
			to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			to_transfer.subresourceRange.baseMipLevel = 0;
			to_transfer.subresourceRange.levelCount = 1;
			to_transfer.subresourceRange.baseArrayLayer = layer;
			to_transfer.subresourceRange.layerCount = 1;
			to_transfer.srcAccessMask = 0;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//暂存缓冲 → 图像
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = row_pixels;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = layer;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { width, height, 1 };
			vkCmdCopyBufferToImage(command_buffer, staging_buffer, dst_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

			//布局：传输目标 → 着色器可读（本层图像的默认终态）
			VkImageMemoryBarrier to_shader{};
			to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_shader.image = dst_image;
			to_shader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_shader);
		});

		//回收暂存缓冲（无论成败）
		buffer_destroy(staging_buffer, staging_alloc);

		if (ok)
			logger.info("上传批次:: 图像上传完成（{}x{}）", width, height);
		return ok;
	}

	//上传像素到图像的子区域（第一百五十一轮；第一百五十八轮补 mip 级）
	bool Memory_Context::upload_image_region(VkImage dst_image, const void* data, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, uint32_t mip_level)
	{
		//前置：上下文与队列就绪、目标句柄与源数据齐备、区域非空
		if (!valid() || dst_image == VK_NULL_HANDLE || data == nullptr || graphics_queue == VK_NULL_HANDLE
			|| width == 0 || height == 0)
		{
			logger.error("上传批次:: 上传图像区域的参数不完整");
			return false;
		}
		//源字节数（RGBA8 紧排）
		const VkDeviceSize data_size = static_cast<VkDeviceSize>(width) * height * 4;
		//创建暂存缓冲（主机可见、仅顺序写入，并用作传输源）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true))
			return false;
		//写入暂存缓冲
		if (!buffer_write(staging_buffer, staging_alloc, data, data_size, 0))
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}
		//确保上传命令池与栅栏就绪
		if (!upload_resources_ensure())
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}
		//提交「布局转换 → 区域拷贝 → 布局转换」三步（旧布局为着色器可读）
		bool ok = upload_submit_once(device, upload_pool, upload_fence, graphics_queue, [&](VkCommandBuffer command_buffer)
		{
			//布局：着色器可读 → 传输目标（仅目标级）
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = dst_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip_level, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//暂存缓冲 → 图像子区域（目标级）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = mip_level;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
			copy_region.imageExtent = { width, height, 1 };
			vkCmdCopyBufferToImage(command_buffer, staging_buffer, dst_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

			//布局：传输目标 → 着色器可读（恢复默认终态）
			VkImageMemoryBarrier to_shader{};
			to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_shader.image = dst_image;
			to_shader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip_level, 1, 0, 1 };
			to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_shader);
		});
		//回收暂存缓冲（无论成败）
		buffer_destroy(staging_buffer, staging_alloc);
		if (ok)
			logger.info("上传批次:: 图像区域上传完成（{}x{} @ {},{}, 级 {}）", width, height, x, y, mip_level);
		return ok;
	}

	/*
	从图像读回像素（第 0 级、整幅；RGBA8 紧排）
	流程：SHADER_READ_ONLY → TRANSFER_SRC → 拷贝到主机暂存缓冲 → 转回 SHADER_READ_ONLY
	     → 等栅栏 → buffer_read 读出 → 销毁暂存缓冲。
	调用方须保证此刻无在途帧引用该图像（门面读回接口已先 frames_wait）。
	*/
	bool Memory_Context::image_readback(VkImage image, uint32_t width, uint32_t height, void* data, VkDeviceSize data_size)
	{
		//整幅即「原点 ＋ 完整尺寸」的区域读回
		return image_readback_region(image, 0, 0, width, height, data, data_size);
	}

	//从图像读回指定子区域（第一百五十八轮补 mip 级）
	bool Memory_Context::image_readback_region(VkImage image, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, void* data, VkDeviceSize data_size,
		uint32_t mip_level)
	{
		//前置：上下文与队列就绪、图像与出参齐备、尺寸与容量一致
		if (!valid() || image == VK_NULL_HANDLE || data == nullptr || graphics_queue == VK_NULL_HANDLE
			|| region_width == 0 || region_height == 0
			|| data_size < static_cast<VkDeviceSize>(region_width) * region_height * 4)
		{
			logger.error("上传批次:: 图像读回的参数不完整");
			return false;
		}

		//创建主机可见暂存缓冲（传输目标用途）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, data_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
			return false;

		//确保一次性命令资源就绪
		if (!upload_resources_ensure())
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//提交「转入传输源 → 拷贝 → 转回着色器可读」三步
		bool ok = upload_submit_once(device, upload_pool, upload_fence, graphics_queue, [&](VkCommandBuffer command_buffer)
		{
			//着色器可读 → 传输源
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip_level, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//图像子区域 → 暂存缓冲（目标级、紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = mip_level;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
			copy_region.imageExtent = { region_width, region_height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer, 1, &copy_region);

			//传输源 → 着色器可读（恢复默认终态）
			VkImageMemoryBarrier to_shader = to_transfer;
			to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_shader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_shader);
		});

		//从暂存缓冲读出像素
		if (ok)
			ok = buffer_read(staging_buffer, staging_alloc, data, data_size, 0);
		//回收暂存缓冲（无论成败）
		buffer_destroy(staging_buffer, staging_alloc);
		return ok;
	}

	/*
	生成 mip 链（整链入口；第一百五十八轮改为委托 image_generate_mips_from）
	第 0 级须已由 upload_image 写入且处于着色器可读布局；
	第 1..mip_levels-1 级逐级以线性过滤降采样（源 = 上一级、目标 = 本级，同图像不同级）；
	每级用「源级转传输源 + 目标级转传输目标」→ blit →「目标级转着色器可读」三段完成，
	循环结束后把起始级..mip_levels-2 这些「曾作 blit 源」的级由传输源一并恢复为着色器可读
	（末级仅作目标级、已是着色器可读）。全部命令一次提交完成。
	*/
	bool Memory_Context::image_generate_mips(VkImage image, uint32_t width, uint32_t height, uint32_t mip_levels)
	{
		//整链生成即「从第 0 级起重生成」
		return image_generate_mips_from(image, 0, width, height, mip_levels);
	}

	//从指定级起重生成 mip 链（第一百五十八轮）
	bool Memory_Context::image_generate_mips_from(VkImage image, uint32_t base_level,
		uint32_t base_width, uint32_t base_height, uint32_t mip_levels)
	{
		//起始级已是末级（或级数不足）时为空操作
		if (mip_levels <= 1 || base_level + 1 >= mip_levels)
			return true;
		//前置：上下文与队列就绪、目标句柄齐备
		if (!valid() || image == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE)
		{
			logger.error("上传批次:: 生成 mip 链的参数不完整");
			return false;
		}
		//确保上传命令池与栅栏就绪（与像素上传共用同一套一次性资源）
		if (!upload_resources_ensure())
			return false;

		//提交全部降采样命令（一次栅栏等待）
		bool ok = upload_submit_once(device, upload_pool, upload_fence, graphics_queue, [&](VkCommandBuffer command_buffer)
		{
			//当前源级尺寸（起始级尺寸）
			uint32_t source_width = base_width;
			uint32_t source_height = base_height;

			//逐级生成（起始级之后到末级）
			for (uint32_t level = base_level + 1; level < mip_levels; ++level)
			{
				//目标级尺寸（不小于 1）
				uint32_t target_width = source_width > 1 ? source_width / 2 : 1;
				uint32_t target_height = source_height > 1 ? source_height / 2 : 1;

				//源级：着色器可读 → 传输源
				VkImageMemoryBarrier source_barrier{};
				source_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				source_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				source_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				source_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				source_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				source_barrier.image = image;
				source_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1 };
				source_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				source_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

				//目标级：未定义 → 传输目标（旧内容无须保留）
				VkImageMemoryBarrier target_barrier{};
				target_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				target_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				target_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				target_barrier.image = image;
				target_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
				target_barrier.srcAccessMask = 0;
				target_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

				//三条屏障中前两条合并提交：源级走「片元着色器 → 传输」，目标级走「顶部管线 → 传输」
				VkImageMemoryBarrier pre_barriers[2] = { source_barrier, target_barrier };
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 2, pre_barriers);
				//目标级为「未定义 → 传输目标」，故其阶段对单独再走一次「顶部管线 → 传输」
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &target_barrier);

				//线性过滤降采样（源级 → 目标级）
				VkImageBlit blit{};
				blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 };
				blit.srcOffsets[0] = { 0, 0, 0 };
				blit.srcOffsets[1] = { static_cast<int32_t>(source_width), static_cast<int32_t>(source_height), 1 };
				blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
				blit.dstOffsets[0] = { 0, 0, 0 };
				blit.dstOffsets[1] = { static_cast<int32_t>(target_width), static_cast<int32_t>(target_height), 1 };
				vkCmdBlitImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

				//目标级：传输目标 → 着色器可读（下一轮循环作为源级时再转入传输源）
				VkImageMemoryBarrier back_barrier{};
				back_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				back_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				back_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				back_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				back_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				back_barrier.image = image;
				back_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
				back_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				back_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &back_barrier);

				//推进源尺寸
				source_width = target_width;
				source_height = target_height;
			}

			/*
			起始级 .. 末级前一级由传输源恢复为着色器可读
			循环中每级的「上一级」（即 base_level .. mip_levels-2 级）都被转入传输源作 blit 源，且各轮只恢复
			「本级」（目标级）的布局，故这些源级在循环结束后仍停在传输源，必须一并恢复为着色器可读，
			否则后续采样该图像的这些 mip 级会因布局不符触发布局校验错误。
			末级（mip_levels-1）仅作目标级、已是着色器可读，故不纳入本屏障。
			*/
			VkImageMemoryBarrier source_levels_barrier{};
			source_levels_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			source_levels_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			source_levels_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			source_levels_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			source_levels_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			source_levels_barrier.image = image;
			source_levels_barrier.subresourceRange = {
				VK_IMAGE_ASPECT_COLOR_BIT, base_level, mip_levels - 1 - base_level, 0, 1 };
			source_levels_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			source_levels_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &source_levels_barrier);
		});

		if (ok)
			logger.info("上传批次:: mip 链已生成（起始级 {}，共 {} 级，{}x{}）",
				base_level, mip_levels, base_width, base_height);
		return ok;
	}
}
