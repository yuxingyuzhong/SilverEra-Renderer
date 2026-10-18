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
		uint32_t width, uint32_t height, uint32_t row_pixels)
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
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
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
			copy_region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
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
}
