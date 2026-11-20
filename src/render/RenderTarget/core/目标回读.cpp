#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像 / 视图创建与销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构

	bool Render_Target_Context::target_transition(Render_Target_Record& record)
	{
		//重置命令池（连带回收上次分配的命令缓冲）
		if (!vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置转换命令池"))
			return false;

		//分配命令缓冲
		VkCommandBufferAllocateInfo buffer_alloc{};
		buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		buffer_alloc.commandPool = transient_pool;
		buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		buffer_alloc.commandBufferCount = 1;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		//若分配失败
		if (!vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配转换命令缓冲"))
			return false;

		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始转换录制");

		//颜色屏障（未定义 → 着色器可读）
		VkImageMemoryBarrier color_barrier{};
		color_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		color_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		color_barrier.image = record.color_image;
		color_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		color_barrier.srcAccessMask = 0;
		color_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		//深度屏障（未定义 → 深度附件）
		VkImageMemoryBarrier depth_barrier{};
		depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depth_barrier.image = record.depth_image;
		depth_barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		depth_barrier.srcAccessMask = 0;
		depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		//屏障集合（基础两条 ＋ 色调编号一条 ＋ MSAA 三条）
		VkImageMemoryBarrier barriers[6] = { color_barrier, depth_barrier, {}, {}, {}, {} };
		uint32_t barrier_count = 2;
		//若启用逐材质色调编号附件（第 801 轮；未定义 → 着色器可读）
		if (record.tone_id_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier tone_id_barrier{};
			tone_id_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			tone_id_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			tone_id_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			tone_id_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			tone_id_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			tone_id_barrier.image = record.tone_id_image;
			tone_id_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			tone_id_barrier.srcAccessMask = 0;
			tone_id_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barriers[barrier_count++] = tone_id_barrier;
		}
		//若启用 MSAA
		if (record.sample_count > 1)
		{
			//多重采样编号屏障（第 821 轮；未定义 → 颜色附件）
			if (record.msaa_tone_id_image != VK_NULL_HANDLE)
			{
				VkImageMemoryBarrier msaa_tone_barrier = color_barrier;
				msaa_tone_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				msaa_tone_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				msaa_tone_barrier.image = record.msaa_tone_id_image;
				msaa_tone_barrier.srcAccessMask = 0;
				msaa_tone_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barriers[barrier_count++] = msaa_tone_barrier;
			}
			//MSAA 颜色屏障（未定义 → 颜色附件）
			VkImageMemoryBarrier msaa_color_barrier{};
			msaa_color_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			msaa_color_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_color_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_color_barrier.image = record.msaa_color_image;
			msaa_color_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			msaa_color_barrier.srcAccessMask = 0;
			msaa_color_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barriers[barrier_count++] = msaa_color_barrier;
			//MSAA 深度屏障（未定义 → 深度附件）
			VkImageMemoryBarrier msaa_depth_barrier = depth_barrier;
			msaa_depth_barrier.image = record.msaa_depth_image;
			barriers[barrier_count++] = msaa_depth_barrier;
		}

		//一次提交全部屏障（srcStage = 顶部管线；dstStage 取各目的阶段并集）
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, barrier_count, barriers);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束转换录制");

		//复位完成栅栏后再提交（上次等待完成后已发出）
		if (!vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置转换栅栏"))
			return false;

		//提交到图形队列（同步完成，故不设信号量）
		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;
		//若提交成功则等待其完成
		if (!vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交转换命令"))
			return false;
		return vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待转换完成");
	}

	bool Render_Target_Context::target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//先取范围，整幅即「原点 ＋ 完整尺寸」
		if (handle == 0 || handle > records.size())
			return false;
		//若目标已空
		if (!records[handle - 1].valid)
			return false;
		//转交实现（整幅）
		return target_readback_impl(handle, 0, 0, records[handle - 1].extent.width,
			records[handle - 1].extent.height, pixels, capacity, width_out, height_out);
	}

	bool Render_Target_Context::target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 子区域尺寸校验
		if (handle == 0 || handle > records.size() || region_width == 0 || region_height == 0)
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则拒绝
		if (!record.valid)
			return false;
		//子区域须完整落在目标范围内（含右 / 下边界）
		if (x >= record.extent.width || y >= record.extent.height
			|| region_width > record.extent.width - x || region_height > record.extent.height - y)
		{
			logger.error("渲染目标:: 读回子区域越界（{}×{} 目标，区域 {}×{} @ {},{}）",
				record.extent.width, record.extent.height, region_width, region_height, x, y);
			return false;
		}
		//转交实现（子区域）
		return target_readback_impl(handle, x, y, region_width, region_height, pixels, capacity,
			width_out, height_out);
	}

	bool Render_Target_Context::target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//整幅即「原点 ＋ 完整尺寸」的区域读回（尺寸由记录取得）
		VkExtent2D extent{};
		if (!target_extent_get(handle, extent))
		{
			if (width_out != nullptr)
				*width_out = 0;
			if (height_out != nullptr)
				*height_out = 0;
			return false;
		}
		return target_readback_depth_region(handle, 0, 0, extent.width, extent.height, values, capacity,
			width_out, height_out);
	}

	bool Render_Target_Context::target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 出参校验
		if (!valid() || values == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空或无深度图像则拒绝
		if (!record.valid || record.depth_image == VK_NULL_HANDLE)
			return false;
		/*
		MSAA 开启时深度读回不可用（第九十六轮）
		实际深度写入多重采样附件，而多重采样深度无法解析 / 拷贝为单采样值；
		目标上的单采样深度图像在 MSAA 路径未被写入，读回只会得到清屏值，故直接拒绝。
		*/
		if (record.sample_count > 1)
		{
			logger.error("渲染目标:: MSAA 开启时深度读回不可用（采样数 {}）", record.sample_count);
			return false;
		}
		//子区域须完整落在目标范围内（含右 / 下边界）
		if (x >= record.extent.width || y >= record.extent.height
			|| region_width == 0 || region_height == 0
			|| region_width > record.extent.width - x || region_height > record.extent.height - y)
		{
			logger.error("渲染目标:: 深度读回区域越界（{}×{} 目标，区域 {}×{} @ {},{}）",
				record.extent.width, record.extent.height, region_width, region_height, x, y);
			return false;
		}
		//容量（元素个数）校验
		VkDeviceSize value_count = static_cast<VkDeviceSize>(region_width) * region_height;
		if (value_count > capacity)
		{
			logger.error("渲染目标:: 深度读回容量不足（需要 {} 个元素，给出 {}）", value_count, capacity);
			return false;
		}
		//字节数（每像素 32 位）
		VkDeviceSize byte_size = value_count * 4;
		//确保一次性命令资源就绪
		if (!transient_resources_ensure())
			return false;

		//主机可见暂存缓冲
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!memory->buffer_create(staging, staging_alloc, byte_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
		{
			logger.error("渲染目标:: 深度读回暂存缓冲创建失败");
			return false;
		}

		//重置命令池并分配命令缓冲
		bool recorded = vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置深度读回命令池");
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (recorded)
		{
			VkCommandBufferAllocateInfo buffer_alloc{};
			buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc.commandPool = transient_pool;
			buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc.commandBufferCount = 1;
			recorded = vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配深度读回命令缓冲");
		}
		//开始录制
		if (recorded)
		{
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			recorded = vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始深度读回录制");
		}

		if (recorded)
		{
			//深度附件 → 传输源
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = record.depth_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//深度图像 → 暂存缓冲（整幅、紧排）
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
			region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
			region.imageExtent = { region_width, region_height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, record.depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

			//传输源 → 深度附件（恢复帧间不变量）
			VkImageMemoryBarrier to_depth = to_transfer;
			to_depth.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			to_depth.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			to_depth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_depth);

			//结束录制
			recorded = vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束深度读回录制");
		}

		//提交并等待
		if (recorded)
			recorded = vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置深度读回栅栏");
		if (recorded)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			recorded = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交深度读回命令");
		}
		if (recorded)
			recorded = vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待深度读回完成");
		//读出原始值
		if (recorded)
			recorded = memory->buffer_read(staging, staging_alloc, values, byte_size, 0);
		//归还暂存缓冲
		memory->buffer_destroy(staging, staging_alloc);

		//写出尺寸（区域尺寸）
		if (recorded)
		{
			if (width_out != nullptr)
				*width_out = region_width;
			if (height_out != nullptr)
				*height_out = region_height;
		}
		return recorded;
	}

	//读回实现（RGBA8：复用原始字节路径）
	bool Render_Target_Context::target_readback_impl(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		return target_readback_raw(handle, x, y, region_width, region_height, 4,
			pixels, capacity, width_out, height_out);
	}

	//文件内部工具：半精度 → 单精度
	namespace
	{
		//IEEE 754 binary16 转 binary32（含符号 / 非规格化 / 无穷 / NaN）
		float half_to_float(uint16_t value)
		{
			const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
			uint32_t exponent = (value >> 10) & 0x1Fu;
			uint32_t mantissa = value & 0x3FFu;
			uint32_t bits = 0;
			//零或非规格化
			if (exponent == 0)
			{
				if (mantissa == 0)
					bits = sign;
				else
				{
					exponent = 1;
					while ((mantissa & 0x400u) == 0)
					{
						mantissa <<= 1;
						--exponent;
					}
					mantissa &= 0x3FFu;
					bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
				}
			}
			//无穷 / NaN
			else if (exponent == 0x1Fu)
				bits = sign | 0x7F800000u | (mantissa << 13);
			//规格化
			else
				bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
			float result = 0.0f;
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}
	}

	//读回颜色为浮点 RGBA
	bool Render_Target_Context::target_readback_float(uint64_t handle, float* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 出参 / 上下文校验
		if (!valid() || values == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则拒绝
		if (!record.valid)
			return false;
		//纹素字节数（本层只出现 RGBA16F 与 8 位）
		const uint32_t bytes_per_texel = record.color_format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8u : 4u;
		const uint32_t width = record.extent.width;
		const uint32_t height = record.extent.height;
		const std::size_t float_count = static_cast<std::size_t>(width) * height * 4;
		//若容量不足
		if (float_count > capacity)
		{
			logger.error("渲染目标:: 浮点读回容量不足（需要 {} 个 float，给出 {}）", float_count, capacity);
			return false;
		}
		//原始字节暂存（按纹素字节数紧排）
		std::vector<uint8_t> raw(static_cast<std::size_t>(width) * height * bytes_per_texel);
		if (!target_readback_raw(handle, 0, 0, width, height, bytes_per_texel, raw.data(),
			static_cast<uint32_t>(raw.size()), width_out, height_out))
			return false;
		//转换为浮点
		if (bytes_per_texel == 8)
		{
			const uint16_t* halves = reinterpret_cast<const uint16_t*>(raw.data());
			for (std::size_t index = 0; index < float_count; ++index)
				values[index] = half_to_float(halves[index]);
		}
		else
		{
			for (std::size_t index = 0; index < float_count; ++index)
				values[index] = static_cast<float>(raw[index]) / 255.0f;
		}
		return true;
	}

	//读回逐材质色调编号附件（第 808 轮）
	bool Render_Target_Context::target_readback_tone_id(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 出参校验
		if (!valid() || pixels == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空或未建立编号附件则拒绝
		if (!record.valid || record.tone_id_image == VK_NULL_HANDLE)
			return false;
		//容量校验（每像素 1 字节）
		VkDeviceSize pixel_count = static_cast<VkDeviceSize>(record.extent.width) * record.extent.height;
		if (pixel_count > capacity)
		{
			logger.error("渲染目标:: 编号附件读回容量不足（需要 {} 字节，给出 {}）", pixel_count, capacity);
			return false;
		}
		//转交原始字节读回（源 = 编号图像、每像素 1 字节）
		return target_readback_raw(handle, 0, 0, record.extent.width, record.extent.height, 1,
			pixels, capacity, width_out, height_out, record.tone_id_image);
	}

	//取默认编号附件（第八百一十二轮；1×1 R8_UINT 零图）
	bool Render_Target_Context::tone_id_default_get(VkImageView& view_out)
	{
		//出参先复位
		view_out = VK_NULL_HANDLE;
		//已创建则直接返回
		if (default_tone_id_view != VK_NULL_HANDLE)
		{
			view_out = default_tone_id_view;
			return true;
		}
		//前置：上下文与内存模块齐备
		if (!valid() || memory == nullptr)
			return false;
		//创建 1×1 R8_UINT 图像与视图（采样 ＋ 传输目的）
		if (!memory->image_create(default_tone_id_image, default_tone_id_alloc, 1, 1,
			VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		{
			logger.error("渲染目标:: 默认编号附件创建失败");
			return false;
		}
		if (!memory->image_view_create(default_tone_id_view, default_tone_id_image,
			VK_FORMAT_R8_UINT, VK_IMAGE_ASPECT_COLOR_BIT))
		{
			logger.error("渲染目标:: 默认编号附件视图创建失败");
			memory->image_destroy(default_tone_id_image, default_tone_id_alloc);
			default_tone_id_image = VK_NULL_HANDLE;
			default_tone_id_alloc = VK_NULL_HANDLE;
			return false;
		}
		//确保一次性命令资源就绪
		if (!transient_resources_ensure())
			return false;
		//重置命令池并分配命令缓冲
		bool recorded = vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置默认编号命令池");
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (recorded)
		{
			VkCommandBufferAllocateInfo buffer_alloc{};
			buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc.commandPool = transient_pool;
			buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc.commandBufferCount = 1;
			recorded = vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer),
				"渲染目标:: 分配默认编号命令缓冲");
		}
		//开始录制
		if (recorded)
		{
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			recorded = vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始默认编号录制");
		}
		if (recorded)
		{
			//屏障：未定义 → 传输目的
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = default_tone_id_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			to_transfer.srcAccessMask = 0;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);
			//清 0（0 = 无覆盖）
			VkClearColorValue clear{};
			clear.uint32[0] = 0;
			VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdClearColorImage(command_buffer, default_tone_id_image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
			//屏障：传输目的 → 着色器可读
			VkImageMemoryBarrier to_sample = to_transfer;
			to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_sample);
			//结束录制
			recorded = vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束默认编号录制");
		}
		//提交并等待
		if (recorded)
			recorded = vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置默认编号栅栏");
		if (recorded)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			recorded = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence),
				"渲染目标:: 提交默认编号命令");
		}
		if (recorded)
			recorded = vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX),
				"渲染目标:: 等待默认编号完成");
		//失败则归还刚创建的资源
		if (!recorded)
		{
			memory->image_view_destroy(default_tone_id_view);
			memory->image_destroy(default_tone_id_image, default_tone_id_alloc);
			default_tone_id_view = VK_NULL_HANDLE;
			default_tone_id_image = VK_NULL_HANDLE;
			default_tone_id_alloc = VK_NULL_HANDLE;
			return false;
		}
		view_out = default_tone_id_view;
		return true;
	}

	bool Render_Target_Context::target_readback_raw(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint32_t bytes_per_texel,
		uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out, VkImage source)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 像素出参 / 容量校验
		if (!valid() || pixels == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则拒绝
		if (!record.valid)
			return false;
		//读回源图像（未指定时取颜色图像）
		const VkImage source_image = source != VK_NULL_HANDLE ? source : record.color_image;
		//子区域字节数（按纹素字节数紧排）
		VkDeviceSize byte_size = static_cast<VkDeviceSize>(region_width) * region_height * bytes_per_texel;
		//若容量不足
		if (byte_size > capacity)
		{
			logger.error("渲染目标:: 读回容量不足（需要 {} 字节，给出 {}）", byte_size, capacity);
			return false;
		}
		//确保一次性命令资源就绪
		if (!transient_resources_ensure())
			return false;

		//创建主机可见暂存缓冲（传输目标用途）
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!memory->buffer_create(staging, staging_alloc, byte_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
		{
			logger.error("渲染目标:: 读回暂存缓冲创建失败");
			return false;
		}

		//重置命令池并分配命令缓冲
		bool recorded = vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置读回命令池");
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (recorded)
		{
			VkCommandBufferAllocateInfo buffer_alloc{};
			buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc.commandPool = transient_pool;
			buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc.commandBufferCount = 1;
			recorded = vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配读回命令缓冲");
		}
		//开始录制
		if (recorded)
		{
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			recorded = vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始读回录制");
		}

		if (recorded)
		{
			//进入传输源布局（着色器读 → 传输读）
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = source_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//拷贝指定子区域到暂存缓冲（紧排、无行填充）
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
			region.imageExtent = { region_width, region_height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, source_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

			//转回着色器可读（后续帧继续采样该目标）
			VkImageMemoryBarrier to_sample = to_transfer;
			to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_sample.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_sample);

			//结束录制
			recorded = vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束读回录制");
		}

		//提交并等待（同步完成，不依赖帧模块）
		if (recorded)
		{
			recorded = vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置读回栅栏");
		}
		if (recorded)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			recorded = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交读回命令");
		}
		if (recorded)
		{
			recorded = vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待读回完成");
		}

		//从暂存缓冲读出像素
		if (recorded)
		{
			recorded = memory->buffer_read(staging, staging_alloc, pixels, byte_size, 0);
		}
		//归还暂存缓冲
		memory->buffer_destroy(staging, staging_alloc);

		//成功后写出子区域尺寸
		if (recorded)
		{
			if (width_out != nullptr)
				*width_out = region_width;
			if (height_out != nullptr)
				*height_out = region_height;
		}
		return recorded;
	}
}
