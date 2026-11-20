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
		//编号附件 / 管线任一缺失则跳过
		if (pipeline == VK_NULL_HANDLE || target.tone_id_view == VK_NULL_HANDLE)
			return;
		/*
		附件选择（第 822 轮：多重采样；第 827 轮：布局口径修正）
		单采样：编号直接写 tone_id_image；
		多重采样：写 msaa 编号图（创建即颜色附件、此后常驻该布局），pass 结束时按「样本 0」解析到单采样编号图；
		单采样编号图在多重采样路径下是解析目标，需在 pass 前后做「着色器可读 ↔ 颜色附件」往返；
		深度取与主 pass 相同的深度附件（MSAA 用多重采样深度）。
		*/
		VkImage tone_image = target.tone_id_image;
		VkImageView tone_view = target.tone_id_view;
		VkImageView depth_view = target.depth_view;
		VkImageView resolve_view = VK_NULL_HANDLE;
		//多重采样路径下需要在 pass 前后做布局往返的图（单采样编号图）
		VkImage transition_image = tone_image;
		if (target.sample_count > 1)
		{
			//缺少多重采样编号图则回到不录制（保持既有行为）
			if (target.tone_id_msaa_view == VK_NULL_HANDLE || target.depth_view == VK_NULL_HANDLE)
				return;
			tone_image = target.tone_id_msaa_image;
			tone_view = target.tone_id_msaa_view;
			resolve_view = target.tone_id_view;
			transition_image = target.tone_id_image;
			if (target.msaa_depth_view != VK_NULL_HANDLE)
				depth_view = target.msaa_depth_view;
		}

		//布局屏障（编号附件：着色器可读 → 颜色附件；多重采样时作用于单采样解析图）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = transition_image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		//编号附件信息（清 0；0 = 无覆盖）
		VkRenderingAttachmentInfo tone_attachment{};
		tone_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		tone_attachment.imageView = tone_view;
		tone_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		tone_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		tone_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		tone_attachment.clearValue.color.uint32[0] = 0;
		//多重采样：按样本 0 解析（编号不做平均，避免两种算子混出第三种）
		if (resolve_view != VK_NULL_HANDLE)
		{
			tone_attachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			tone_attachment.resolveImageView = resolve_view;
			tone_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		//深度附件信息（保留场景深度；本通道不写深度）
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = depth_view;
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
			//透明项 / 非实例项 / 间接项不承载「稳定的」逐材质编号，跳过
			if (item.transparent || !item.instance_input || item.gpu_indirect)
				continue;
			//管线或缓冲缺失、索引数或实例数为 0 的项跳过（与主通道同口径）
			if (item.vertex_buffer == VK_NULL_HANDLE || item.index_buffer == VK_NULL_HANDLE
				|| item.instance_buffer == VK_NULL_HANDLE || item.index_count == 0 || item.instance_count == 0)
				continue;
			//绑定描述符集（编号着色器需要 set0 场景 UBO 与 set1 反照率）
			if (item.descriptor_set_count > 0 && item.scene_set != VK_NULL_HANDLE)
			{
				const VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
					0, item.descriptor_set_count, sets, 0, nullptr);
			}
			/*
			推送材质参数（第 819 轮；alpha 裁剪需要阈值 / UV 变换 / 色调 alpha）
			与主通道同一批推送常量，编号片元按同一式判定 discard。
			*/
			if (item.push_constants != nullptr && item.push_constant_size > 0)
				vkCmdPushConstants(command_buffer, item.layout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					item.push_constant_offset, item.push_constant_size, item.push_constants);
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

		//布局屏障（编号附件：颜色附件 → 着色器可读，供末级采样；多重采样时作用于单采样解析图）
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = transition_image;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}
}
