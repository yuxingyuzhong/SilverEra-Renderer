#include "命令录制_内部.h"

namespace engine
{
	void Frame_Context::command_record_target(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool, bool shadow_pass,
		const Frame_Post_Scene_Callback* post_scene,
		bool reset_query, bool write_begin_timestamp, bool write_end_timestamp, bool leading_barrier)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点。
		批量路径只在首段重置 / 写起点、末段写终点，中间段两者都不做（第六百六十九轮）。
		*/
		if (timestamp_pool != VK_NULL_HANDLE && reset_query)
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, shadow_pass ? 2 : 4);
		if (timestamp_pool != VK_NULL_HANDLE && write_begin_timestamp)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);

		/*
		前导内存屏障（第六百六十九轮；批量切片共用同一颜色 / 深度图像时，跨命令缓冲的写后写依赖）
		仅批量入口的后续段传入 true；单 pass 路径保持引入前逐字节一致的命令序列。
		*/
		if (leading_barrier)
		{
			VkMemoryBarrier memory_barrier{};
			memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				0, 1, &memory_barrier, 0, nullptr, 0, nullptr);
		}

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		颜色布局转换：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
		离屏目标颜色在帧间恒为着色器可读布局（可能已被采样），故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
		*/
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = target.color_image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样附件内容逐帧丢弃，oldLayout 用 UNDEFINED；解析目标（target.color_image）
		已在上方转入 COLOR_ATTACHMENT_OPTIMAL。
		*/
		if (target.sample_count > 1 && target.msaa_color_view != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier = barrier;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		//颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到目标颜色图像）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = target.sample_count > 1 ? target.msaa_color_view : target.color_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		/*
		清屏或保留（第一百二十一轮）
		clear_color 为空表示「保留已有内容」，供同一目标的后续分片 pass 使用（级联阴影第 2 片）。
		*/
		color_attachment.loadOp = clear_color != nullptr ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (clear_color != nullptr)
		{
			color_attachment.clearValue.color.float32[0] = clear_color[0];
			color_attachment.clearValue.color.float32[1] = clear_color[1];
			color_attachment.clearValue.color.float32[2] = clear_color[2];
			color_attachment.clearValue.color.float32[3] = clear_color[3];
		}
		else
			color_attachment.clearValue.color.float32[0] = 0.0f;
		//MSAA：解析到目标单采样颜色图像
		if (target.sample_count > 1)
		{
			color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color_attachment.resolveImageView = target.color_view;
			color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		//深度附件信息（清屏后保留；目标深度在帧间恒为深度附件布局；MSAA 时用多重采样深度）
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = target.sample_count > 1 ? target.msaa_depth_view : target.depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil.depth = 1.0f;
		depth_attachment.clearValue.depthStencil.stencil = 0;

		//渲染信息（渲染区 = 目标范围，单颜色附件 + 深度附件）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = target.extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		last_parallel_segments = 0;
		/*
		视口 / 裁剪（含子视口收缩；第二百轮）
		声明在分支外：并行段与 MSAA 解析后的 UI 通道都要用同一组值。
		*/
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(target.extent.width);
		viewport.height = static_cast<float>(target.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = target.extent;
		if (target.viewport_rect.extent.width != 0 && target.viewport_rect.extent.height != 0)
		{
			viewport.x = static_cast<float>(target.viewport_rect.offset.x);
			viewport.y = static_cast<float>(target.viewport_rect.offset.y);
			viewport.width = static_cast<float>(target.viewport_rect.extent.width);
			viewport.height = static_cast<float>(target.viewport_rect.extent.height);
			scissor = target.viewport_rect;
		}
		/*
		渲染区跟随子视口（第六百七十六轮）
		图集瓦片等「各分片独立清屏」场景把 renderArea 收缩到该矩形，
		清屏与 load / store 只作用于本片，避免整图带宽浪费；默认仍覆盖整幅目标。
		*/
		if (target.render_area_from_viewport
			&& target.viewport_rect.extent.width != 0 && target.viewport_rect.extent.height != 0)
			rendering_info.renderArea = target.viewport_rect;
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段沿用清屏、其余保留；MSAA 解析仅末段；
		单采样时 UI 与场景共用本通道，故 UI 回调放在末段（位置与单线程一致）。
		返回 0 表示未规划并行（线程数 / 项数 / 次级缓冲不满足），走下方单线程录制。
		*/
		if (scene_parallel_record(command_buffer, rendering_info, color_attachment, depth_attachment,
			viewport, scissor, items, item_count, target.sample_count, ui_callback) == 0)
		{
		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		//逐渲染项绘制（带状态缓存）
		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		frame_recorded = items_record(command_buffer, items, item_count);

		//单采样与 MSAA 统一：UI 另起无深度颜色通道（见下方 UI 阶段）
		//结束场景动态渲染（MSAA 时此处完成解析到目标颜色图像）
		vkCmdEndRendering(command_buffer);
		}

		/*
		场景退场回调（第三百六十八轮；离屏路径的 Hi-Z 深度拷贝与金字塔生成）
		必须在渲染通道之外执行；单采样直接用目标深度，MSAA 用深度解析后的单采样目标深度。
		回调自行完成深度布局往返，返回后深度仍为 DEPTH_ATTACHMENT_OPTIMAL。
		*/
		if (post_scene != nullptr && post_scene->fn != nullptr
			&& (target.sample_count <= 1 || target.msaa_depth_view != VK_NULL_HANDLE)
			&& target.depth_image != VK_NULL_HANDLE && target.depth_view != VK_NULL_HANDLE)
			post_scene->fn(command_buffer, target.depth_image, target.depth_view, target.extent, post_scene->user_data);

		/*
		场景 / 后处理分段时间戳（第一百二十八轮）
		离屏与阴影 pass 均无后处理：普通离屏路径在场景结束处同时写查询 1 / 2；
		阴影 pass 使用两查询池，末点改由下方收尾处记录，故此处不写。
		*/
		if (timestamp_pool != VK_NULL_HANDLE && !shadow_pass)
		{
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		}

		/*
		UI 阶段（单采样与 MSAA 统一）
		场景 pass 结束后以「加载后保留」的单采样颜色通道绘制 UI（无深度附件）。
		*/
		if (ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_pass_record(command_buffer, target.color_image, target.color_view, target.extent,
				viewport, scissor, ui_callback);

		//若请求回读：颜色附件 → 传输源，拷贝到回读缓冲，再转到着色器可读
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

			//拷贝区域（整张目标颜色附件，RGBA8 紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { target.extent.width, target.extent.height, 1 };
			//把颜色附件拷入回读缓冲
			vkCmdCopyImageToBuffer(command_buffer, target.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				readback_buffer, 1, &copy_region);

			//布局转换：传输源 → 着色器可读（使目标帧间可被采样）
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		//若不回读：颜色附件直接转回着色器可读（使目标帧间可被采样）
		else
		{
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		//GPU 时间戳终点（可选；阴影 pass 为末点 1，普通离屏为末点 3；批量路径仅末段写入）
		if (timestamp_pool != VK_NULL_HANDLE && write_end_timestamp)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool,
				shadow_pass ? 1 : 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
