#include "命令录制_内部.h"

namespace engine
{
	/*
	逐材质色调编号预通道（第 808 轮 · 8 位路径「方案 B」）
	在场景 pass 结束、场景深度仍为 DEPTH_ATTACHMENT_OPTIMAL 时，以同一深度图再绘一遍不透明项：
	深度比较 LESS_OR_EQUAL ＋ 不写深度，故只有最终可见片元通过深度测试并写下编号；
	编号附件清 0（无覆盖），片元按实例属性 z 写 (算子 + 1)。主 pass 保持单颜色附件。
	跳过透明项、不使用实例属性的项（粒子）与 GPU 侧间接项 —— 它们不承载逐材质算子。
	*/
	void Frame_Context::tone_id_pass_record(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, VkPipeline pipeline)
	{
		//编号附件 / 深度 / 管线任一缺失，或多重采样（编号附件只在单采样目标创建）则跳过
		if (pipeline == VK_NULL_HANDLE || target.tone_id_view == VK_NULL_HANDLE
			|| target.depth_view == VK_NULL_HANDLE || target.sample_count > 1)
			return;

		//布局屏障（编号附件：着色器可读 → 颜色附件）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = target.tone_id_image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		//编号附件信息（清 0；0 = 无覆盖）
		VkRenderingAttachmentInfo tone_attachment{};
		tone_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		tone_attachment.imageView = target.tone_id_view;
		tone_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		tone_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		tone_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		tone_attachment.clearValue.color.uint32[0] = 0;

		//深度附件信息（保留场景深度；本通道不写深度）
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = target.depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		//渲染信息（单颜色附件 ＋ 场景深度）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = target.extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &tone_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//视口 / 裁剪（与场景 pass 同口径）
		VkViewport viewport{};
		viewport.width = static_cast<float>(target.extent.width);
		viewport.height = static_cast<float>(target.extent.height);
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.extent = target.extent;
		if (target.viewport_rect.extent.width != 0 && target.viewport_rect.extent.height != 0)
		{
			viewport.x = static_cast<float>(target.viewport_rect.offset.x);
			viewport.y = static_cast<float>(target.viewport_rect.offset.y);
			viewport.width = static_cast<float>(target.viewport_rect.extent.width);
			viewport.height = static_cast<float>(target.viewport_rect.extent.height);
			scissor = target.viewport_rect;
		}

		//进入编号通道
		vkCmdBeginRendering(command_buffer, &rendering_info);
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);
		//编号管线只需绑定一次（批内全部绘制项共用）
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		for (uint32_t index = 0; index < item_count; ++index)
		{
			const Frame_Render_Item& item = items[index];
			//透明项 / 非实例项 / 间接项不承载逐材质算子，跳过
			if (item.transparent || !item.instance_input || item.gpu_indirect)
				continue;
			//管线或缓冲缺失、索引数或实例数为 0 的项跳过（与主通道同口径）
			if (item.vertex_buffer == VK_NULL_HANDLE || item.index_buffer == VK_NULL_HANDLE
				|| item.instance_buffer == VK_NULL_HANDLE || item.index_count == 0 || item.instance_count == 0)
				continue;
			//绑定描述符集（编号着色器只需 set0 的场景 UBO）
			if (item.descriptor_set_count > 0 && item.scene_set != VK_NULL_HANDLE)
			{
				const VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
					0, item.descriptor_set_count, sets, 0, nullptr);
			}
			//绑定顶点与实例缓冲（实例缓冲偏移由绘制项给出）
			VkBuffer vertex_buffers[2] = { item.vertex_buffer, item.instance_buffer };
			VkDeviceSize vertex_offsets[2] = { 0, item.instance_offset };
			vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, vertex_offsets);
			//绑定索引缓冲
			vkCmdBindIndexBuffer(command_buffer, item.index_buffer, item.index_offset, VK_INDEX_TYPE_UINT32);
			//绘制（深度 LESS_OR_EQUAL ＋ 不写深度，只有最终可见片元通过）
			vkCmdDrawIndexed(command_buffer, item.index_count, item.instance_count, 0, 0, 0);
		}
		//结束编号通道
		vkCmdEndRendering(command_buffer);

		//布局屏障（编号附件：颜色附件 → 着色器可读，供末级采样）
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}
}
