#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	逐渲染项绘制（带状态缓存）
	本层每帧各渲染项的 set0 / set1 描述符集都是「本帧槽」的同一组值（门面按 slot 统一填装），
	而各渲染项**可挂不同图形管线**（多材质 / 多管线），故管线与描述符集只在发生变化时重绑，
	以减少 GPU 状态切换（K 优化）。推送常量与顶点 / 索引 / 实例缓冲各网格不同，仍须逐项绑定。
	由 command_record（交换链路径）与 command_record_target（离屏路径）共用。
	*/
	void Frame_Context::items_record(VkCommandBuffer command_buffer, const Frame_Render_Item* items, uint32_t item_count)
	{
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
			//管线或顶点 / 索引 / 实例缓冲缺失则跳过该项
			if (item.pipeline == VK_NULL_HANDLE || item.vertex_buffer == VK_NULL_HANDLE
				|| item.index_buffer == VK_NULL_HANDLE || item.index_count == 0
				|| item.instance_buffer == VK_NULL_HANDLE || item.instance_count == 0)
				continue;

			//仅在与当前已绑定管线不同时重绑（同帧各渲染项可挂不同管线）
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
			/*
			绑定实例缓冲（binding 1）
			模型矩阵逐实例步进读取；实例数为 1 时即「逐次绘制」的等价路径，
			故绑定 1 在任何合法绘制项下都必须有有效缓冲（管线已恒定声明实例属性）。
			*/
			VkDeviceSize instance_offset = item.instance_offset;
			vkCmdBindVertexBuffers(command_buffer, 1, 1, &item.instance_buffer, &instance_offset);

			//绘制索引几何（实例数由渲染项给出；实例数 ≥ 1）
			vkCmdDrawIndexed(command_buffer, item.index_count, item.instance_count, 0, 0, 0);
		}
	}

	//录制命令缓冲：颜色 / 深度布局转换 + 动态渲染（清屏 + 逐项绘制 + UI 阶段）+ 可选像素回读 + 呈现前布局转换
	void Frame_Context::command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
		VkImageView depth_view, VkExtent2D extent,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool)
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
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 2);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

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

		//逐渲染项绘制（带状态缓存）
		items_record(command_buffer, items, item_count);

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
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}

	/*
	录制「渲染到离屏目标」的命令缓冲
	颜色进入布局为 SHADER_READ_ONLY_OPTIMAL（目标创建时已转换、帧间恒保持），故先转入 COLOR_ATTACHMENT_OPTIMAL；
	深度进入布局恒为 DEPTH_ATTACHMENT_OPTIMAL，逐帧以 CLEAR 清空，故此处不再逐帧转换；
	场景 pass 结束后若给定回读缓冲，则转入 TRANSFER_SRC 把颜色附件拷入缓冲，再转回 SHADER_READ_ONLY；
	否则直接由 COLOR_ATTACHMENT_OPTIMAL 转回 SHADER_READ_ONLY，使目标在帧间保持可采样布局（不呈现到窗口）。
	*/
	void Frame_Context::command_record_target(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool)
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
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 2);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

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

		//颜色附件信息（清屏后保留）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = target.color_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color.float32[0] = clear_color[0];
		color_attachment.clearValue.color.float32[1] = clear_color[1];
		color_attachment.clearValue.color.float32[2] = clear_color[2];
		color_attachment.clearValue.color.float32[3] = clear_color[3];

		//深度附件信息（清屏后保留；目标深度在帧间恒为深度附件布局）
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = target.depth_view;
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

		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);

		//视口（正高度；与交换链路径同口径）
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(target.extent.width);
		viewport.height = static_cast<float>(target.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		//裁剪区
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = target.extent;
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		//逐渲染项绘制（带状态缓存）
		items_record(command_buffer, items, item_count);

		//UI 阶段（可选；与交换链路径同一时机——场景 pass 内、结束动态渲染之前）
		if (ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_callback->fn(command_buffer, ui_callback->user_data);

		//结束场景动态渲染
		vkCmdEndRendering(command_buffer);

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

		//GPU 时间戳终点（可选；管线底部，含此前全部命令与收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}

	/*
	录制「同帧多 pass（离屏目标 → 全屏后处理 → 交换链）」的命令缓冲
	pass 1：目标颜色 SHADER_READ_ONLY → COLOR_ATTACHMENT，清屏并以 items 绘制，结束后转回 SHADER_READ_ONLY；
	pass 2：交换链图像 UNDEFINED → COLOR_ATTACHMENT，清屏并以 post_item 全屏绘制（采样目标颜色），
	        结束后按是否回读转入 TRANSFER_SRC（回读）或直接转入 PRESENT_SRC。
	两 pass 共用同一命令缓冲，故后处理在**同一帧内**完成（无需跨帧中间态）。
	*/
	void Frame_Context::command_record_target_present(VkCommandBuffer command_buffer, VkImage swapchain_image,
		VkImageView swapchain_view, VkImageView swapchain_depth_view, VkExtent2D swapchain_extent,
		const Frame_Render_Target& target, const Frame_Render_Item* items, uint32_t item_count,
		const Frame_Render_Item& post_item, const float clear_color[4], VkBuffer readback_buffer,
		VkQueryPool timestamp_pool)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点；
		终点在收尾处记录，故该值覆盖「目标 pass ＋ 后处理 pass」两个 pass 的 GPU 耗时。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 2);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//布局屏障（两 pass 复用同一对象，逐段改字段即可）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		/*
		pass 1 · 目标颜色布局转换：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
		目标颜色在帧间恒为着色器可读布局（可能已被采样），故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.image = target.color_image;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		//pass 1 · 颜色附件信息（清屏后保留）
		VkRenderingAttachmentInfo target_color{};
		target_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		target_color.imageView = target.color_view;
		target_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		target_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		target_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		target_color.clearValue.color.float32[0] = clear_color[0];
		target_color.clearValue.color.float32[1] = clear_color[1];
		target_color.clearValue.color.float32[2] = clear_color[2];
		target_color.clearValue.color.float32[3] = clear_color[3];

		//pass 1 · 深度附件信息（目标深度在帧间恒为深度附件布局，逐帧清屏）
		VkRenderingAttachmentInfo target_depth{};
		target_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		target_depth.imageView = target.depth_view;
		target_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		target_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		target_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		target_depth.clearValue.depthStencil.depth = 1.0f;
		target_depth.clearValue.depthStencil.stencil = 0;

		//pass 1 · 渲染信息（渲染区 = 目标范围）
		VkRenderingInfo target_rendering{};
		target_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		target_rendering.renderArea.offset = { 0, 0 };
		target_rendering.renderArea.extent = target.extent;
		target_rendering.layerCount = 1;
		target_rendering.colorAttachmentCount = 1;
		target_rendering.pColorAttachments = &target_color;
		target_rendering.pDepthAttachment = &target_depth;

		//pass 1 · 进入动态渲染并设置视口 / 裁剪（按目标范围）
		vkCmdBeginRendering(command_buffer, &target_rendering);
		VkViewport target_viewport{};
		target_viewport.x = 0.0f;
		target_viewport.y = 0.0f;
		target_viewport.width = static_cast<float>(target.extent.width);
		target_viewport.height = static_cast<float>(target.extent.height);
		target_viewport.minDepth = 0.0f;
		target_viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &target_viewport);
		VkRect2D target_scissor{};
		target_scissor.offset = { 0, 0 };
		target_scissor.extent = target.extent;
		vkCmdSetScissor(command_buffer, 0, 1, &target_scissor);
		//pass 1 · 场景绘制
		items_record(command_buffer, items, item_count);
		//pass 1 · 结束动态渲染
		vkCmdEndRendering(command_buffer);

		/*
		pass 1 → pass 2 · 目标颜色布局转换：COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
		后处理 pass 在片元阶段采样该目标，故 dstStage = FRAGMENT_SHADER、dstAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = target.color_image;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		pass 2 · 交换链图像布局转换：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
		同步由 acquire 信号量保护，故 srcStage = COLOR_ATTACHMENT_OUTPUT、srcAccess = 0（K11）。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.image = swapchain_image;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		//pass 2 · 颜色附件信息（清屏；后处理全屏覆盖，故内容由后处理决定）
		VkRenderingAttachmentInfo swap_color{};
		swap_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		swap_color.imageView = swapchain_view;
		swap_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		swap_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		swap_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		swap_color.clearValue.color.float32[0] = clear_color[0];
		swap_color.clearValue.color.float32[1] = clear_color[1];
		swap_color.clearValue.color.float32[2] = clear_color[2];
		swap_color.clearValue.color.float32[3] = clear_color[3];

		//pass 2 · 深度附件信息（交换链深度，逐帧清屏）
		VkRenderingAttachmentInfo swap_depth{};
		swap_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		swap_depth.imageView = swapchain_depth_view;
		swap_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		swap_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		swap_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		swap_depth.clearValue.depthStencil.depth = 1.0f;
		swap_depth.clearValue.depthStencil.stencil = 0;

		//pass 2 · 渲染信息（渲染区 = 交换链范围）
		VkRenderingInfo swap_rendering{};
		swap_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		swap_rendering.renderArea.offset = { 0, 0 };
		swap_rendering.renderArea.extent = swapchain_extent;
		swap_rendering.layerCount = 1;
		swap_rendering.colorAttachmentCount = 1;
		swap_rendering.pColorAttachments = &swap_color;
		swap_rendering.pDepthAttachment = &swap_depth;

		//pass 2 · 进入动态渲染并设置视口 / 裁剪（按交换链范围）
		vkCmdBeginRendering(command_buffer, &swap_rendering);
		VkViewport swap_viewport{};
		swap_viewport.x = 0.0f;
		swap_viewport.y = 0.0f;
		swap_viewport.width = static_cast<float>(swapchain_extent.width);
		swap_viewport.height = static_cast<float>(swapchain_extent.height);
		swap_viewport.minDepth = 0.0f;
		swap_viewport.maxDepth = 1.0f;
		vkCmdSetViewport(command_buffer, 0, 1, &swap_viewport);
		VkRect2D swap_scissor{};
		swap_scissor.offset = { 0, 0 };
		swap_scissor.extent = swapchain_extent;
		vkCmdSetScissor(command_buffer, 0, 1, &swap_scissor);
		//pass 2 · 全屏后处理绘制
		items_record(command_buffer, &post_item, 1);
		//pass 2 · 结束动态渲染
		vkCmdEndRendering(command_buffer);

		/*
		交换链图像收尾（与交换链路径同口径）
		若请求回读：先转 TRANSFER_SRC 拷回主机缓冲，再转 PRESENT_SRC；
		否则直接由 COLOR_ATTACHMENT 转入 PRESENT_SRC（呈现同步由信号量与 present 等待承担）。
		*/
		if (readback_buffer != VK_NULL_HANDLE)
		{
			//布局转换：颜色附件 → 传输源
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.image = swapchain_image;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			//拷贝区域（整张交换链颜色附件，RGBA8 紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { swapchain_extent.width, swapchain_extent.height, 1 };
			//把颜色附件拷入回读缓冲
			vkCmdCopyImageToBuffer(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				readback_buffer, 1, &copy_region);

			//布局转换：传输源 → 呈现布局
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		//若不回读：颜色附件直接转入呈现布局
		else
		{
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
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
