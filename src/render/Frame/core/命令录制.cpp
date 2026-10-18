#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//录制命令缓冲：颜色 / 深度布局转换 + 动态渲染（清屏 + 逐项绘制 + UI 阶段）+ 呈现前布局转换
	void Frame_Context::command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
		VkImageView depth_view, VkExtent2D extent,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

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

		//颜色附件信息（清屏后保留）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = image_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color.float32[0] = clear_color[0];
		color_attachment.clearValue.color.float32[1] = clear_color[1];
		color_attachment.clearValue.color.float32[2] = clear_color[2];
		color_attachment.clearValue.color.float32[3] = clear_color[3];

		/*
		深度附件信息
		深度图像由交换链模块持有，布局已在交换链重建时统一转为 DEPTH_ATTACHMENT_OPTIMAL，
		故此处不再逐帧转换，仅作清屏（深度 1.0，模板 0）后保留。
		*/
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil.depth = 1.0f;
		depth_attachment.clearValue.depthStencil.stencil = 0;

		//渲染信息（渲染区 = 交换链范围，单颜色附件 + 深度附件）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);

		//视口（正高度；Y 轴向下由投影处理，K2）
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		//裁剪区
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		/*
		逐渲染项绘制（带状态缓存）
		本层只有一条场景管线，且同一帧内所有渲染项的 set0 / set1 描述符集
		都是「本帧槽」的同一组值（渲染器门面按 slot 统一填装）；故管线与描述符集
		只在首项绑定一次，后续相同项跳过重复的状态切换（K 优化：减少 GPU 状态切换）。
		推送常量（各网格模型矩阵）与顶点 / 索引缓冲各网格不同，仍须逐项绑定。
		*/
		//当前已绑定的图形管线（初始为空）
		VkPipeline bound_pipeline = VK_NULL_HANDLE;
		//当前已绑定的管线布局
		VkPipelineLayout bound_layout = VK_NULL_HANDLE;
		//当前已绑定的描述符集数量
		uint32_t bound_set_count = 0;
		//当前已绑定的描述符集（set0 在前，set1 在后）
		VkDescriptorSet bound_sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		for (uint32_t index = 0; index < item_count; ++index)
		{
			//当前渲染项
			const Frame_Render_Item& item = items[index];
			//管线或顶点 / 索引缓冲缺失则跳过该项
			if (item.pipeline == VK_NULL_HANDLE || item.vertex_buffer == VK_NULL_HANDLE
				|| item.index_buffer == VK_NULL_HANDLE || item.index_count == 0)
				continue;

			//仅在与当前已绑定管线不同时重绑（同帧各渲染项共用同一条场景管线）
			if (item.pipeline != bound_pipeline)
			{
				//绑定图形管线
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline);
				bound_pipeline = item.pipeline;
				//管线变化使「已绑定布局 / 描述符集」失效，一并复位
				bound_layout = VK_NULL_HANDLE;
				bound_set_count = 0;
				bound_sets[0] = VK_NULL_HANDLE;
				bound_sets[1] = VK_NULL_HANDLE;
			}

			//若存在可绑定描述符集
			if (item.descriptor_set_count > 0)
			{
				//仅在与当前已绑定（布局 + 数量 + 集合）不同时重绑
				if (item.layout != bound_layout || item.descriptor_set_count != bound_set_count
					|| item.scene_set != bound_sets[0] || item.sampler_set != bound_sets[1])
				{
					//待绑定描述符集数组（set0 在前，set1 在后）
					VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
					//绑定描述符集
					vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
						0, item.descriptor_set_count, sets, 0, nullptr);
					//记录当前已绑定的布局与描述符集
					bound_layout = item.layout;
					bound_set_count = item.descriptor_set_count;
					bound_sets[0] = item.scene_set;
					bound_sets[1] = item.sampler_set;
				}
			}

			//若存在推送常量
			if (item.push_constants != nullptr && item.push_constant_size > 0)
				//推送常量到顶点阶段
				vkCmdPushConstants(command_buffer, item.layout, VK_SHADER_STAGE_VERTEX_BIT,
					0, item.push_constant_size, item.push_constants);

			//绑定顶点缓冲（binding 0，偏移 0）
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(command_buffer, 0, 1, &item.vertex_buffer, &offset);
			//绑定索引缓冲（uint32 索引；偏移由网格记录给出，顶点与索引可共用一个缓冲）
			vkCmdBindIndexBuffer(command_buffer, item.index_buffer, item.index_offset, VK_INDEX_TYPE_UINT32);

			//绘制索引几何
			vkCmdDrawIndexed(command_buffer, item.index_count, 1, 0, 0, 0);
		}

		/*
		UI 阶段
		若设置了 UI 绘制回调，则交还上层绘制 UI：上层（Test 层）在此调用
		ImGui_ImplVulkan_RenderDrawData —— 直接画在本帧已开启的动态渲染通道内
		（与场景共用同一通道；该通道附件为交换链颜色附件 + 深度附件，故上层
		ImGui 管线的 PipelineRenderingCreateInfo 必须与本通道附件格式相容）。
		回调返回后本通道仍处于开启状态。
		*/
		if (ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_callback->fn(command_buffer, ui_callback->user_data);

		//结束场景动态渲染
		vkCmdEndRendering(command_buffer);

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

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
