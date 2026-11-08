#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//并行录制规划（第二百零八轮；三条录制路径共用同一口径）
	uint32_t Frame_Context::parallel_plan(Frame_Slot& slot, uint32_t item_count, VkFormat color_format,
		VkFormat depth_format, uint32_t sample_count)
	{
		//线程池缺失 / 未启动 / 线程不足则保持单线程
		if (thread_pool == nullptr || !thread_pool->active() || thread_pool->thread_count_get() <= 1)
			return 0;
		//绘制项少于此值时不值得分段（分段开销大于并行收益）
		if (item_count < parallel_min_items)
			return 0;
		//候选段数（线程数与段数上限取小）
		uint32_t wanted = thread_pool->thread_count_get();
		if (wanted > parallel_max_segments)
			wanted = parallel_max_segments;
		//再按「每段至少 16 项」取小
		const uint32_t by_items = item_count / 16;
		if (wanted > by_items)
			wanted = by_items;
		//不足两段或该帧槽次级缓冲就绪失败则保持单线程
		if (wanted < 2 || !secondary_buffers_ensure(slot, wanted))
			return 0;
		//填充并行上下文（只对本帧录制有效，录制结束后由调用方清空）
		active_secondary = slot.secondary_buffers.data();
		active_secondary_count = wanted;
		active_color_format = color_format;
		active_depth_format = depth_format;
		active_sample_count = sample_count;
		//按段数准备统计槽（各段只写自己的一格）
		parallel_counts.assign(wanted, Recorded_Counts{});
		return wanted;
	}

	//场景 pass 并行段录制（第二百零八轮；三条录制路径共用同一实现）
	uint32_t Frame_Context::scene_parallel_record(VkCommandBuffer command_buffer, const VkRenderingInfo& rendering_info,
		const VkRenderingAttachmentInfo& color_attachment, const VkRenderingAttachmentInfo& depth_attachment,
		VkViewport viewport, VkRect2D scissor, const Frame_Render_Item* items, uint32_t item_count,
		uint32_t sample_count, const Frame_Ui_Callback* ui_callback)
	{
		//未规划并行 / 无绘制项 / 统计槽不足则交回单线程录制
		if (active_secondary_count < 2 || active_secondary == nullptr || item_count == 0
			|| parallel_counts.size() < active_secondary_count)
			return 0;
		//段数（不超过绘制项数）
		const uint32_t segment_count = active_secondary_count < item_count ? active_secondary_count : item_count;
		//逐段录制（线程池按段索引分派；每段由单一工作线程完整录制）
		thread_pool->ranges_run(segment_count, 1, [&](std::size_t begin, std::size_t end) {
			for (std::size_t segment = begin; segment < end; ++segment)
			{
				//本段项区间（按索引均分，余数自然落在后段）
				const uint32_t start = static_cast<uint32_t>(segment * item_count / segment_count);
				const uint32_t finish = static_cast<uint32_t>((segment + 1) * item_count / segment_count);
				VkCommandBuffer segment_buffer = active_secondary[segment];
				/*
				继承的渲染信息（动态渲染在次级缓冲中需声明附件格式与采样数，
				供校验层核对与该段 vkCmdBeginRendering 的兼容性）
				*/
				VkCommandBufferInheritanceRenderingInfo inheritance_rendering{};
				inheritance_rendering.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
				inheritance_rendering.colorAttachmentCount = 1;
				inheritance_rendering.pColorAttachmentFormats = &active_color_format;
				inheritance_rendering.depthAttachmentFormat = active_depth_format;
				inheritance_rendering.rasterizationSamples = static_cast<VkSampleCountFlagBits>(active_sample_count);
				VkCommandBufferInheritanceInfo inheritance{};
				inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
				inheritance.pNext = &inheritance_rendering;
				VkCommandBufferBeginInfo segment_begin{};
				segment_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				segment_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				segment_begin.pInheritanceInfo = &inheritance;
				vk_result_check(vkBeginCommandBuffer(segment_buffer, &segment_begin), "vkBeginCommandBuffer(次级)");
				//本段附件：首段沿用入参 loadOp（清屏或保留）、其余保留；解析只在末段执行
				VkRenderingAttachmentInfo segment_color = color_attachment;
				segment_color.loadOp = segment == 0 ? color_attachment.loadOp : VK_ATTACHMENT_LOAD_OP_LOAD;
				VkRenderingAttachmentInfo segment_depth = depth_attachment;
				segment_depth.loadOp = segment == 0 ? depth_attachment.loadOp : VK_ATTACHMENT_LOAD_OP_LOAD;
				if (segment + 1 != segment_count)
				{
					segment_color.resolveMode = VK_RESOLVE_MODE_NONE;
					segment_color.resolveImageView = VK_NULL_HANDLE;
					segment_depth.resolveMode = VK_RESOLVE_MODE_NONE;
					segment_depth.resolveImageView = VK_NULL_HANDLE;
				}
				VkRenderingInfo segment_rendering = rendering_info;
				segment_rendering.pColorAttachments = &segment_color;
				segment_rendering.pDepthAttachment = &segment_depth;
				vkCmdBeginRendering(segment_buffer, &segment_rendering);
				vkCmdSetViewport(segment_buffer, 0, 1, &viewport);
				vkCmdSetScissor(segment_buffer, 0, 1, &scissor);
				//本段绘制（各段只写自己的统计槽）
				parallel_counts[segment] = items_record(segment_buffer, items + start, finish - start);
				//UI 已改由调用方在场景 pass 结束后另起无深度颜色通道绘制，分段内不再回调
				vkCmdEndRendering(segment_buffer);
				vk_result_check(vkEndCommandBuffer(segment_buffer), "vkEndCommandBuffer(次级)");
			}
		});
		//一次性提交全部段（减少 API 调用次数；执行顺序不变）
		vkCmdExecuteCommands(command_buffer, segment_count, active_secondary);
		//汇总各段实际绘制量
		for (uint32_t segment = 0; segment < segment_count; ++segment)
		{
			frame_recorded.items += parallel_counts[segment].items;
			frame_recorded.instances += parallel_counts[segment].instances;
		}
		//记录本帧并行段数（观测用）
		last_parallel_segments = segment_count;
		return segment_count;
	}
}