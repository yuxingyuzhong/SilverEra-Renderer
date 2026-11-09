#include "命令录制_内部.h"

namespace engine
{
	void Frame_Context::command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
		VkImage depth_image, VkImageView depth_view, VkExtent2D extent,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool,
		VkImage msaa_image, VkImageView msaa_color_view, VkImageView msaa_depth_view, VkImage msaa_depth_image,
		uint32_t sample_count, const Frame_Post_Scene_Callback* post_scene)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 4);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		颜色布局转换：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
		同步由 acquire 信号量保护，故 srcStage = COLOR_ATTACHMENT_OUTPUT、srcAccess = 0（K11）。
		*/
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样颜色图像内容逐帧丢弃（loadOp CLEAR），故 oldLayout 用 UNDEFINED；
		解析目标（交换链图像）已在上方转入 COLOR_ATTACHMENT_OPTIMAL。
		*/
		if (sample_count > 1 && msaa_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier{};
			msaa_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_barrier.image = msaa_image;
			msaa_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		//颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到 image_view）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = sample_count > 1 ? msaa_color_view : image_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color.float32[0] = clear_color[0];
		color_attachment.clearValue.color.float32[1] = clear_color[1];
		color_attachment.clearValue.color.float32[2] = clear_color[2];
		color_attachment.clearValue.color.float32[3] = clear_color[3];
		//MSAA：声明解析目标（单采样交换链图像）与平均解析
		if (sample_count > 1)
		{
			color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color_attachment.resolveImageView = image_view;
			color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		/*
		深度附件信息
		深度图像由交换链模块持有，布局已在交换链重建时统一转为 DEPTH_ATTACHMENT_OPTIMAL，
		故此处不再逐帧转换，仅作清屏（深度 1.0，模板 0）后保留；MSAA 时用多重采样深度附件。
		*/
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = sample_count > 1 ? msaa_depth_view : depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil.depth = 1.0f;
		depth_attachment.clearValue.depthStencil.stencil = 0;
		/*
		深度解析（第二百六十轮）
		MSAA 下场景只写多重采样深度；当多重采样深度图像与单采样深度图像均就绪时，
		以「样本 0」解析到单采样深度，供 Hi-Z 生成读取（不影响既有颜色 / UI 路径）。
		*/
		if (sample_count > 1 && msaa_depth_image != VK_NULL_HANDLE && depth_image != VK_NULL_HANDLE)
		{
			depth_attachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depth_attachment.resolveImageView = depth_view;
			depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		//渲染信息（渲染区 = 交换链范围，单颜色附件 + 深度附件）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		last_parallel_segments = 0;
		/*
		视口 / 裁剪（正高度；Y 轴向下由投影处理，K2）
		声明在此（而非单线程分支内）：MSAA 解析后的 UI 通道同样需要这两个值。
		*/
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段清屏、其余保留；MSAA 解析仅末段；
		单采样时 UI 与场景共用本通道，故 UI 回调放在末段（位置与单线程一致）。
		返回 0 表示未规划并行（线程数 / 项数 / 次级缓冲不满足），走下方单线程录制。
		*/
		if (scene_parallel_record(command_buffer, rendering_info, color_attachment, depth_attachment,
			viewport, scissor, items, item_count, sample_count, ui_callback) == 0)
		{
		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);

		//视口 / 裁剪（值见上方声明）
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		//逐渲染项绘制（带状态缓存）
		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		frame_recorded = items_record(command_buffer, items, item_count);

		/*
		UI 阶段
		单采样与 MSAA 统一改为「场景 pass 结束后另起无深度颜色通道」：
		UI 管线声明 depthAttachmentFormat = UNDEFINED 且关闭深度测试，
		若与场景通道（含深度附件）共用会在 vkCmdDraw* 触发附件格式不匹配的校验错误。
		*/
		//结束场景动态渲染（MSAA 时此处完成解析到交换链图像）
		vkCmdEndRendering(command_buffer);
		}

		/*
		场景退场回调（第二百五十四轮；Hi-Z 深度拷贝与金字塔生成）
		必须在渲染通道之外执行；单采样直接用场景深度，MSAA 用深度解析后的单采样深度（第二百六十轮）。
		回调自行完成深度布局往返，返回后深度仍为 DEPTH_ATTACHMENT_OPTIMAL。
		*/
		if (post_scene != nullptr && post_scene->fn != nullptr
			&& (sample_count <= 1 || msaa_depth_image != VK_NULL_HANDLE)
			&& depth_image != VK_NULL_HANDLE && depth_view != VK_NULL_HANDLE)
			post_scene->fn(command_buffer, depth_image, depth_view, extent, post_scene->user_data);

		/*
		场景 / 后处理分段时间戳（第一百二十八轮）
		直通路径无后处理：场景结束处同时记录查询 1 与 2，使「后处理耗时」恒 0。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		}

		/*
		UI 阶段（单采样与 MSAA 统一）
		场景 pass 结束后以「加载后保留」的单采样颜色通道绘制 UI（无深度附件）；视图 / 裁剪按全屏。
		*/
		if (ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_pass_record(command_buffer, image, image_view, extent, viewport, scissor, ui_callback);

		/*
		像素回读（可选）
		渲染通道结束后先把颜色附件转入 TRANSFER_SRC_OPTIMAL，再把整张附件拷入主机可见的回读缓冲；
		屏障 src 为颜色附件输出写入、dst 为传输读取，保证拷贝读到本帧已完成写入的像素。
		*/
		if (readback_buffer != VK_NULL_HANDLE)
		{
			//布局转换：颜色附件 → 传输源
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			//拷贝区域（整张颜色附件，RGBA8 紧排；行距 / 图距为 0 表示按范围紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { extent.width, extent.height, 1 };
			//把颜色附件拷入回读缓冲
			vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				readback_buffer, 1, &copy_region);

			/*
			颜色布局转换：TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR
			呈现同步由 render_finished 信号量与 present 等待承担，故 dstStage = BOTTOM_OF_PIPE、dstAccess = 0（K12）。
			*/
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		//若不回读则直接由颜色附件转入呈现布局（与无回读路径逐字节一致）
		else
		{
			/*
			颜色布局转换：COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
			呈现同步由 render_finished 信号量与 present 等待承担，故 dstStage = BOTTOM_OF_PIPE、dstAccess = 0（K12）。
			*/
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		//GPU 时间戳终点（可选；管线底部，含此前全部命令与收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
