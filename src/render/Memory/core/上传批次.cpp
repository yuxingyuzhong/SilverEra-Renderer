#include "../局部命名空间使用.h"
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	namespace
	{
		/*
		一次性提交
		本文件共用的一次性提交路径：临时命令池 → 命令缓冲 → 录制 → 提交 → 等待完成 →
		销毁命令池。Phase 3 尚无帧模块（帧槽命令池自 Phase 5 起提供），故每次上传各建一次；
		起步阶段上传次数很少，这一开销可接受。
		record：在当前命令缓冲上录制全部拷贝与布局转换。
		*/
		template <typename Record_Fn>
		bool upload_submit_once(VkDevice device, uint32_t graphics_family, VkQueue graphics_queue, Record_Fn record)
		{
			//创建临时命令池（一次性用途，用完整池销毁）
			VkCommandPoolCreateInfo pool_info{};
			pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pool_info.queueFamilyIndex = graphics_family;

			VkCommandPool pool = VK_NULL_HANDLE;
			//若命令池创建失败
			if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "上传批次:: 创建临时命令池"))
				return false;

			//创建命令缓冲
			VkCommandBufferAllocateInfo buffer_alloc_info{};
			buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc_info.commandPool = pool;
			buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc_info.commandBufferCount = 1;

			VkCommandBuffer command_buffer = VK_NULL_HANDLE;
			//若命令缓冲分配失败
			if (!vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc_info, &command_buffer), "上传批次:: 分配命令缓冲"))
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			//开始录制（声明为一次性提交）
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			//若开始录制失败
			if (!vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "上传批次:: 开始命令录制"))
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			//录制具体拷贝与布局转换
			record(command_buffer);

			//结束录制
			if (!vk_result_check(vkEndCommandBuffer(command_buffer), "上传批次:: 结束命令录制"))
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

			//创建完成栅栏（未触发态，供等待本次提交用）
			VkFenceCreateInfo fence_info{};
			fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

			VkFence fence = VK_NULL_HANDLE;
			//若栅栏创建失败
			if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &fence), "上传批次:: 创建完成栅栏"))
			{
				vkDestroyCommandPool(device, pool, nullptr);
				return false;
			}

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

			//回收栅栏与命令池（命令缓冲随池一并销毁）
			vkDestroyFence(device, fence, nullptr);
			vkDestroyCommandPool(device, pool, nullptr);
			return ok;
		}
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

		//创建暂存缓冲（主机可见并用作传输源）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
			return false;

		//写入暂存缓冲
		if (!buffer_write(staging_buffer, staging_alloc, data, size, 0))
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//提交「暂存缓冲 → 目标缓冲」的拷贝
		bool ok = upload_submit_once(device, graphics_family, graphics_queue, [&](VkCommandBuffer command_buffer)
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

		//创建暂存缓冲（主机可见并用作传输源）
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!buffer_create(staging_buffer, staging_alloc, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
			return false;

		//写入暂存缓冲
		if (!buffer_write(staging_buffer, staging_alloc, data, data_size, 0))
		{
			buffer_destroy(staging_buffer, staging_alloc);
			return false;
		}

		//提交「布局转换 → 拷贝到图像 → 布局转换」三步
		bool ok = upload_submit_once(device, graphics_family, graphics_queue, [&](VkCommandBuffer command_buffer)
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
