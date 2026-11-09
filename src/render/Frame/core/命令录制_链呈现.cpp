#include "命令录制_内部.h"

namespace engine
{
	void Frame_Context::command_record_chain_present(VkCommandBuffer command_buffer, VkImage swapchain_image,
		VkImageView swapchain_view, VkImageView swapchain_depth_view, VkExtent2D swapchain_extent,
		const Frame_Render_Target* targets, uint32_t target_count,
		const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item* post_items,
		uint32_t post_count, const float clear_color[4], VkBuffer readback_buffer, VkQueryPool timestamp_pool,
		VkImage swapchain_msaa_image, VkImageView swapchain_msaa_color_view,
		VkImageView swapchain_msaa_depth_view, uint32_t swapchain_sample_count,
		const Frame_Ui_Callback* ui_callback)
	{
		//场景目标（目标组首项）
		const Frame_Render_Target& scene_target = targets[0];
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点；
		终点在收尾处记录，故该值覆盖「场景 pass ＋ 全部后处理 pass」的 GPU 耗时。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 4);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//布局屏障（逐段复用同一对象，改字段即可）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		场景 pass · 场景目标颜色布局转换：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
		目标颜色在帧间恒为着色器可读布局（可能已被采样），故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.image = scene_target.color_image;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		场景 pass · MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样附件内容逐帧丢弃，oldLayout 用 UNDEFINED；解析目标（场景目标颜色图像）已在上方转入颜色附件布局。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_color_view != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier = barrier;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.image = scene_target.msaa_color_image;
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		/*
		场景 pass · MSAA 深度附件布局转换（第一百九十轮；仅采样数 > 1）
		多重采样深度附件内容逐帧丢弃，故 oldLayout 用 UNDEFINED；
		msaa_depth_image 为空（未开启深度解析）时跳过，保持既有行为。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_depth_barrier = barrier;
			msaa_depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			msaa_depth_barrier.image = scene_target.msaa_depth_image;
			msaa_depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			msaa_depth_barrier.srcAccessMask = 0;
			msaa_depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_depth_barrier);
		}

		//场景 pass · 颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到场景目标）
		VkRenderingAttachmentInfo scene_color{};
		scene_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		scene_color.imageView = scene_target.sample_count > 1 ? scene_target.msaa_color_view : scene_target.color_view;
		scene_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		scene_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		scene_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		scene_color.clearValue.color.float32[0] = clear_color[0];
		scene_color.clearValue.color.float32[1] = clear_color[1];
		scene_color.clearValue.color.float32[2] = clear_color[2];
		scene_color.clearValue.color.float32[3] = clear_color[3];
		//MSAA：解析到场景目标单采样颜色图像
		if (scene_target.sample_count > 1)
		{
			scene_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			scene_color.resolveImageView = scene_target.color_view;
			scene_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		//场景 pass · 深度附件信息（场景目标深度在帧间恒为深度附件布局，逐帧清屏；MSAA 时用多重采样深度）
		VkRenderingAttachmentInfo scene_depth{};
		scene_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		scene_depth.imageView = scene_target.sample_count > 1 ? scene_target.msaa_depth_view : scene_target.depth_view;
		scene_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		scene_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		scene_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		scene_depth.clearValue.depthStencil.depth = 1.0f;
		scene_depth.clearValue.depthStencil.stencil = 0;
		/*
		MSAA 深度解析（第一百九十轮）
		深度类后处理（景深 / 深度描边 / 屏幕空间遮蔽）只能采样单采样深度；
		MSAA 下场景 pass 只写多重采样深度附件，故在 pass 结束时按「样本 0」解析到单采样深度图像。
		多重采样深度图与单采样深度图均就绪时才启用；否则保持既有行为。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_depth_image != VK_NULL_HANDLE
			&& scene_target.depth_image != VK_NULL_HANDLE)
		{
			scene_depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			scene_depth.resolveImageView = scene_target.depth_view;
			scene_depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		//场景 pass · 渲染信息（渲染区 = 场景目标范围）
		VkRenderingInfo scene_rendering{};
		scene_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		scene_rendering.renderArea.offset = { 0, 0 };
		scene_rendering.renderArea.extent = scene_target.extent;
		scene_rendering.layerCount = 1;
		scene_rendering.colorAttachmentCount = 1;
		scene_rendering.pColorAttachments = &scene_color;
		scene_rendering.pDepthAttachment = &scene_depth;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		//清零并行段数（并行分支按实际段数回填；第一百九十八轮）
		last_parallel_segments = 0;
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段清屏、其余保留；多重采样解析只在末段开启；
		主缓冲以 vkCmdExecuteCommands 顺序执行：GPU 执行顺序与单线程完全一致，画面与统计逐字节相同。
		链路径不绘制 UI（UI 由上层在此帧之外处理），故回调传空指针。
		*/
		VkViewport scene_viewport{};
		scene_viewport.x = 0.0f;
		scene_viewport.y = 0.0f;
		scene_viewport.width = static_cast<float>(scene_target.extent.width);
		scene_viewport.height = static_cast<float>(scene_target.extent.height);
		scene_viewport.minDepth = 0.0f;
		scene_viewport.maxDepth = 1.0f;
		VkRect2D scene_scissor{};
		scene_scissor.offset = { 0, 0 };
		scene_scissor.extent = scene_target.extent;
		if (scene_parallel_record(command_buffer, scene_rendering, scene_color, scene_depth,
			scene_viewport, scene_scissor, items, item_count, scene_target.sample_count, nullptr) == 0)
		{
			//场景 pass · 进入动态渲染并设置视口 / 裁剪（按场景目标范围）
			vkCmdBeginRendering(command_buffer, &scene_rendering);
			vkCmdSetViewport(command_buffer, 0, 1, &scene_viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scene_scissor);
			//场景 pass · 场景绘制
			frame_recorded = items_record(command_buffer, items, item_count);
			//场景 pass · 结束动态渲染
			vkCmdEndRendering(command_buffer);
		}
		//场景结束时间戳（第一百二十八轮；后处理耗时 = 查询 2 − 查询 1）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);

		/*
		场景 pass → 首级后处理 · 场景目标颜色布局转换：COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
		首级后处理在片元阶段采样该目标，故 dstStage = FRAGMENT_SHADER、dstAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = scene_target.color_image;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		场景 pass → 后处理 · 场景深度布局转换（第一百二十三轮）
		深度采样类阶段（景深）在片元阶段读取深度图，故由深度附件转为着色器可读；
		深度图像句柄为空（未取深度）时跳过，保持既有行为。
		*/
		if (scene_target.depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier depth_barrier{};
			depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			depth_barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.image = scene_target.depth_image;
			depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depth_barrier.subresourceRange.baseMipLevel = 0;
			depth_barrier.subresourceRange.levelCount = 1;
			depth_barrier.subresourceRange.baseArrayLayer = 0;
			depth_barrier.subresourceRange.layerCount = 1;
			depth_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			depth_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			/*
			srcStage 同时覆盖「深度附件写」与「解析写」（第一百九十轮）：
			MSAA 深度解析在动态渲染结束时执行，其写入归入颜色附件输出阶段。
			*/
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &depth_barrier);
		}

		/*
		后处理各级（多目标轮转）
		第 stage 级：源 = targets[stage % target_count]（恒为 SHADER_READ_ONLY，可直接采样）；
		           宿 = 末级交换链图像 / 非末级 targets[(stage + 1) % target_count]。
		*/
		for (uint32_t stage = 0; stage < post_count; ++stage)
		{
			//本级是否为末级（宿为交换链）
			bool last_stage = (stage + 1 == post_count);
			//本级宿（非末级时的中间目标：源的下一个目标，环形轮转）
			const Frame_Render_Target& dest = targets[(stage + 1) % target_count];

			//宿布局转换：末级为交换链图像（UNDEFINED → 颜色附件），否则为中间目标（SHADER_READ_ONLY → 颜色附件）
			if (last_stage)
			{
				/*
				交换链图像：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
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
			}
			else
			{
				/*
				中间目标：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
				该目标可能刚被上一级采样（WAR）或本就处于着色器可读布局，故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
				*/
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.image = dest.color_image;
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);
			}

			/*
			本级宿 MSAA 参数（第九十六轮）
			末级宿为交换链（用交换链 MSAA 附件）；非末级宿为中间目标（用目标自带 MSAA 附件）。
			*/
			VkImage stage_msaa_image = last_stage ? swapchain_msaa_image : dest.msaa_color_image;
			VkImageView stage_msaa_color = last_stage ? swapchain_msaa_color_view : dest.msaa_color_view;
			VkImageView stage_msaa_depth = last_stage ? swapchain_msaa_depth_view : dest.msaa_depth_view;
			/*
			本级采样数取该级管线实际烘焙值（第九十八轮）
			后处理为全屏 pass，单采样与多重采样结果等价；内置后处理管线固定单采样，
			故通常直接写单采样宿，省去多重采样附件的写入与解析带宽。
			*/
			uint32_t stage_sample_count = post_items[stage].sample_count;
			if (stage_sample_count == 0)
				stage_sample_count = 1;
			//本级宿 MSAA 颜色附件布局转换（内容逐帧丢弃，oldLayout 用 UNDEFINED）
			if (stage_sample_count > 1 && stage_msaa_image != VK_NULL_HANDLE)
			{
				VkImageMemoryBarrier msaa_barrier = barrier;
				msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				msaa_barrier.image = stage_msaa_image;
				msaa_barrier.srcAccessMask = 0;
				msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
			}

			//本级宿范围（末级取交换链范围，否则取中间目标范围）
			VkExtent2D stage_extent = last_stage ? swapchain_extent : dest.extent;
			//本级源目标（第 stage 级读第 stage % 目标数 个中间目标）
			const Frame_Render_Target& source = targets[stage % target_count];
			/*
			本级「源 / 宿尺寸 ＋ 泛光参数 ＋ 描边色 ＋ 逐级 extra」推送（offset 80）
			前 4 个为源宽 / 源高 / 宿宽 / 宿高（后处理片元据此映射像素坐标）；
			其后 4 个为泛光阈值 / 软膝 / 强度 / 保留（由门面写在绘制项里）；
			再后 4 个为描边色（rgb ＋ 保留；第一百八十一轮，默认黑 = 压暗轮廓）；
			最后 4 个为逐级 extra（第四百零一轮；设备上限不足 144 字节时整组不推送）。
			*/
			float stage_uniform[16] = {
				static_cast<float>(source.extent.width), static_cast<float>(source.extent.height),
				static_cast<float>(stage_extent.width), static_cast<float>(stage_extent.height),
				post_items[stage].post_params[0], post_items[stage].post_params[1],
				post_items[stage].post_params[2], post_items[stage].post_params[3],
				post_items[stage].post_color[0], post_items[stage].post_color[1],
				post_items[stage].post_color[2], post_items[stage].post_color[3],
				post_items[stage].post_extra[0], post_items[stage].post_extra[1],
				post_items[stage].post_extra[2], post_items[stage].post_extra[3],
			};
			//本级绘制项（复制后补推送数据；不改调用方数组）
			Frame_Render_Item stage_item = post_items[stage];
			stage_item.push_constants = stage_uniform;
			stage_item.push_constant_size = post_extra_enabled ? static_cast<uint32_t>(sizeof(stage_uniform))
				: static_cast<uint32_t>(12 * sizeof(float));
			stage_item.push_constant_offset = 80;

			//本级颜色附件信息（清屏；全屏后处理覆盖，故内容由后处理决定；MSAA 时解析到宿单采样颜色）
			VkRenderingAttachmentInfo stage_color{};
			stage_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			stage_color.imageView = stage_sample_count > 1 ? stage_msaa_color
				: (last_stage ? swapchain_view : dest.color_view);
			stage_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			stage_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			stage_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stage_color.clearValue.color.float32[0] = clear_color[0];
			stage_color.clearValue.color.float32[1] = clear_color[1];
			stage_color.clearValue.color.float32[2] = clear_color[2];
			stage_color.clearValue.color.float32[3] = clear_color[3];
			//MSAA：解析到宿单采样颜色视图
			if (stage_sample_count > 1)
			{
				stage_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				stage_color.resolveImageView = last_stage ? swapchain_view : dest.color_view;
				stage_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}

			//本级深度附件信息（末级取交换链深度，否则取中间目标深度；逐帧清屏；MSAA 时用多重采样深度）
			VkRenderingAttachmentInfo stage_depth{};
			stage_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			stage_depth.imageView = stage_sample_count > 1 ? stage_msaa_depth
				: (last_stage ? swapchain_depth_view : dest.depth_view);
			stage_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			stage_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			stage_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stage_depth.clearValue.depthStencil.depth = 1.0f;
			stage_depth.clearValue.depthStencil.stencil = 0;

			//本级渲染信息（渲染区 = 本级宿范围）
			VkRenderingInfo stage_rendering{};
			stage_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			stage_rendering.renderArea.offset = { 0, 0 };
			stage_rendering.renderArea.extent = stage_extent;
			stage_rendering.layerCount = 1;
			stage_rendering.colorAttachmentCount = 1;
			stage_rendering.pColorAttachments = &stage_color;
			stage_rendering.pDepthAttachment = &stage_depth;

			//本级 · 进入动态渲染并设置视口 / 裁剪（按本级宿范围）
			vkCmdBeginRendering(command_buffer, &stage_rendering);
			VkViewport stage_viewport{};
			stage_viewport.x = 0.0f;
			stage_viewport.y = 0.0f;
			stage_viewport.width = static_cast<float>(stage_extent.width);
			stage_viewport.height = static_cast<float>(stage_extent.height);
			stage_viewport.minDepth = 0.0f;
			stage_viewport.maxDepth = 1.0f;
			vkCmdSetViewport(command_buffer, 0, 1, &stage_viewport);
			VkRect2D stage_scissor{};
			stage_scissor.offset = { 0, 0 };
			stage_scissor.extent = stage_extent;
			vkCmdSetScissor(command_buffer, 0, 1, &stage_scissor);
			//本级 · 全屏后处理绘制（单绘制项，含源 / 宿尺寸推送）
			Recorded_Counts stage_counts = items_record(command_buffer, &stage_item, 1);
			frame_recorded.items += stage_counts.items;
			frame_recorded.instances += stage_counts.instances;
			//本级 · 结束动态渲染
			vkCmdEndRendering(command_buffer);

			/*
			末级后处理结束后绘制 UI（UI 不吃后处理）
			交换链图像此刻已由末级写入（直写或 MSAA 解析），布局为 COLOR_ATTACHMENT_OPTIMAL；
			以「加载后保留」的 UI 通道叠加，故 UI 不再进入任何后处理级；
			未给回调时整段跳过，画面与引入前逐字节一致。
			*/
			if (last_stage && ui_callback != nullptr && ui_callback->fn != nullptr)
			{
				//UI 视口 / 裁剪按交换链范围
				VkViewport ui_viewport{};
				ui_viewport.x = 0.0f;
				ui_viewport.y = 0.0f;
				ui_viewport.width = static_cast<float>(swapchain_extent.width);
				ui_viewport.height = static_cast<float>(swapchain_extent.height);
				ui_viewport.minDepth = 0.0f;
				ui_viewport.maxDepth = 1.0f;
				VkRect2D ui_scissor{};
				ui_scissor.offset = { 0, 0 };
				ui_scissor.extent = swapchain_extent;
				ui_pass_record(command_buffer, swapchain_image, swapchain_view, swapchain_extent,
					ui_viewport, ui_scissor, ui_callback);
			}

			//末级（交换链）：按是否回读收尾为 TRANSFER_SRC → PRESENT_SRC 或直接 COLOR_ATTACHMENT → PRESENT_SRC
			if (last_stage)
			{
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
					barrier.image = swapchain_image;
					barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					barrier.dstAccessMask = 0;
					vkCmdPipelineBarrier(command_buffer,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						0, 0, nullptr, 0, nullptr, 1, &barrier);
				}
			}
			//非末级（中间目标）：颜色附件 → 着色器可读，使下一级可采样且帧间保持该布局
			else
			{
				barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.image = dest.color_image;
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);
			}
		}

		/*
		后处理链收尾 · 场景深度布局回转（第一百二十三轮）
		本帧采样结束后把深度转回深度附件布局，供下一帧以 DEPTH_ATTACHMENT_OPTIMAL 重新挂载；
		深度图像句柄为空时跳过（既有行为不变）。
		*/
		//后处理结束时间戳（第一百二十八轮；含全部后处理级与各级收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		if (scene_target.depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier depth_barrier{};
			depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			depth_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.image = scene_target.depth_image;
			depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depth_barrier.subresourceRange.baseMipLevel = 0;
			depth_barrier.subresourceRange.levelCount = 1;
			depth_barrier.subresourceRange.baseArrayLayer = 0;
			depth_barrier.subresourceRange.layerCount = 1;
			depth_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
				| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &depth_barrier);
		}

		//GPU 时间戳终点（可选；管线底部，含此前全部命令与收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
