#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::chrono（主机侧帧耗时计时）

namespace engine
{
	//文件内部工具
	namespace
	{
		//重置帧槽的全部命令池（主池 ＋ 各次级池；第二百一十八轮）
		//次级池与次级命令缓冲一一对应，每段独占一池，故必须与主池一同重置
		bool frame_slot_pools_reset(VkDevice device, Frame_Slot& slot)
		{
			//重置主命令池（主命令缓冲随之可再次录制）
			if (!vk_result_check(vkResetCommandPool(device, slot.command_pool, 0), "vkResetCommandPool"))
				return false;
			//逐个重置次级命令池
			for (VkCommandPool pool : slot.secondary_pools)
			{
				if (!vk_result_check(vkResetCommandPool(device, pool, 0), "vkResetCommandPool(次级)"))
					return false;
			}
			return true;
		}
	}

	//登记态绘制统计（按登记项累计；实际绘制量由 items_record 的返回值 frame_recorded 另计）
	//累计本帧绘制统计（登记量 ＋ 实际绘制量）
	//登记量按登记项累计；实际绘制量取 items_record 的返回值 frame_recorded（跳过的不完整项不计）
	//登记态绘制统计（按登记项累计；实际绘制量由 items_record 的返回值 frame_recorded 另计）

	bool Frame_Context::render_to_target_present(Swapchain_Context& swapchain, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item& post_item,
		const float clear_color[4], bool& rebuild_needed_out, Frame_Readback_Request* readback,
		const Frame_Ui_Callback* ui_callback)
	{
		//单级后处理（源 = 场景目标、宿 = 交换链，目标组仅一项）
		return render_chain_present(swapchain, &target, 1, items, item_count,
			&post_item, 1, clear_color, rebuild_needed_out, readback, ui_callback);
	}

	bool Frame_Context::render_chain_present(Swapchain_Context& swapchain, const Frame_Render_Target* targets,
		uint32_t target_count, const Frame_Render_Item* items, uint32_t item_count,
		const Frame_Render_Item* post_items, uint32_t post_count, const float clear_color[4],
		bool& rebuild_needed_out, Frame_Readback_Request* readback, const Frame_Ui_Callback* ui_callback)
	{
		//默认无需重建
		rebuild_needed_out = false;
		//清零本帧间接命令计数（第二百五十二轮）
		frame_indirect_commands = 0;
		//前置：帧上下文、交换链、目标组与后处理级数必须齐备
		if (!valid() || !swapchain.valid() || post_count == 0 || post_items == nullptr
			|| targets == nullptr || target_count == 0)
		{
			logger.error("提交呈现:: 帧上下文 / 交换链 / 目标组 / 后处理级数不完整（后处理链）");
			return false;
		}
		//若交换链图像或信号量为空
		if (swapchain.image_count_get() == 0 || swapchain.image_available_count_get() == 0
			|| swapchain.render_finished_count_get() == 0)
		{
			logger.error("提交呈现:: 交换链图像或信号量为空（后处理链）");
			return false;
		}
		//若后处理级数 ≥ 2 但目标不足（轮转需至少两个目标）
		if (post_count >= 2 && target_count < 2)
		{
			logger.error("提交呈现:: 后处理级数 {} ≥ 2 但目标组仅 {} 个（后处理链）", post_count, target_count);
			return false;
		}
		//逐目标校验完整性（场景目标与全部额外中间目标同口径）
		for (uint32_t each = 0; each < target_count; ++each)
		{
			//当前目标附件
			const Frame_Render_Target& current = targets[each];
			//若该目标不完整
			if (current.color_image == VK_NULL_HANDLE || current.color_view == VK_NULL_HANDLE
				|| current.depth_view == VK_NULL_HANDLE || current.extent.width == 0 || current.extent.height == 0)
			{
				logger.error("提交呈现:: 第 {} 个目标尚未就绪（后处理链）", each);
				return false;
			}
		}
		//逐级校验后处理绘制项完整性
		for (uint32_t stage = 0; stage < post_count; ++stage)
		{
			//当前后处理绘制项
			const Frame_Render_Item& post_item = post_items[stage];
			//若某项不完整
			if (post_item.pipeline == VK_NULL_HANDLE || post_item.layout == VK_NULL_HANDLE
				|| post_item.vertex_buffer == VK_NULL_HANDLE || post_item.index_buffer == VK_NULL_HANDLE
				|| post_item.index_count == 0 || post_item.instance_buffer == VK_NULL_HANDLE
				|| post_item.instance_count == 0)
			{
				logger.error("提交呈现:: 后处理绘制项不完整（第 {} 级；后处理链）", stage);
				return false;
			}
		}
		/*
		目标尺寸校验（第十七轮放宽）
		后处理片元按「源 / 宿尺寸」推送做像素映射，故各目标**不再要求与交换链同范围**（支持半分辨率泛光等）；
		仅要求尺寸非零（上方已逐个校验）。
		*/
		VkExtent2D swapchain_extent_check = swapchain.extent_get();
		//若交换链范围为零（无法建立末级视口）
		if (swapchain_extent_check.width == 0 || swapchain_extent_check.height == 0)
		{
			logger.error("提交呈现:: 交换链范围为零（后处理链）");
			return false;
		}

		//取当前帧槽（与其余路径共用同一轮转）
		Frame_Slot& slot = slots[frame_count % frame_overlap];
		//载入该帧槽的间接录制上下文（第二百五十二轮）
		indirect_buffer = slot.indirect_buffer;
		indirect_mapped = slot.indirect_mapped;
		indirect_capacity = slot.indirect_capacity;

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待）
		if (!slot_fence_wait(slot))
			return false;
		//读回该帧槽上一轮 GPU 时间戳（此时上一轮已执行完；无能力 / 首轮时静默跳过）
		gpu_timestamp_read(slot);

		//取图像可用信号量（与交换链路径同口径）
		VkSemaphore image_available = swapchain.image_available_semaphore_get(acquire_cursor % swapchain.image_available_count_get());

		//取下一张交换链图像
		uint32_t image_index = 0;
		VkResult acquire = vkAcquireNextImageKHR(device, swapchain.handle_get(), UINT64_MAX,
			image_available, VK_NULL_HANDLE, &image_index);
		//若交换链过期或次优
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_SUBOPTIMAL_KHR)
		{
			//置「待重建」标志并跳过本帧（不作为硬错误）
			rebuild_needed_out = true;
			logger.debug("提交呈现:: 取图像返回 {}，标记交换链待重建（后处理链）", vk_result_name_get(acquire));
			return true;
		}
		//若取图像遇到其它硬错误
		if (!vk_result_check(acquire, "vkAcquireNextImageKHR"))
			return false;
		//取用成功后推进信号量游标
		++acquire_cursor;

		//重置本帧槽完成栅栏（放在取图像成功之后，理由同交换链路径）
		if (!vk_result_check(vkResetFences(device, 1, &slot.in_flight_fence), "vkResetFences"))
			return false;

		//重置该帧槽全部命令池（主池 ＋ 次级池；连带使其命令缓冲可再次录制）
		if (!frame_slot_pools_reset(device, slot))
			return false;

		//像素回读准备（可选；回读来源为最终呈现到交换链的颜色附件，故按交换链范围校验）
		VkBuffer readback_target = VK_NULL_HANDLE;
		//本帧是否请求回读（请求存在且目标缓冲非空）
		bool readback_active = readback != nullptr && readback->pixels != nullptr;
		//若本帧请求回读
		if (readback_active)
		{
			//前置：交换链图像须具备传输源用法
			if (!swapchain.color_transfer_src_get())
			{
				logger.error("提交呈现:: 交换链图像不具备传输源用法，像素回读不可用（后处理链）");
				return false;
			}
			//本帧像素字节数（RGBA8 紧排，按交换链范围）
			VkExtent2D readback_extent = swapchain.extent_get();
			VkDeviceSize readback_bytes = static_cast<VkDeviceSize>(readback_extent.width) * readback_extent.height * 4;
			//若回读目标容量不足
			if (readback->capacity < readback_bytes)
			{
				logger.error("提交呈现:: 回读目标容量不足（需 {} 字节，有 {} 字节；后处理链）", readback_bytes, readback->capacity);
				return false;
			}
			//确保回读缓冲就绪并把句柄交给录制
			if (!readback_buffer_ensure(readback_bytes))
				return false;
			readback_target = readback_buffer;
		}

		//本帧计时起点（主机侧录制 ＋ 提交 ＋ 呈现调用）
		std::chrono::steady_clock::time_point frame_time_begin = std::chrono::steady_clock::now();
		/*
		场景 pass 的并行录制规划（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		链路径的采样数取场景目标（后处理各级宿为其轮转目标）；其余条件与直通 / 离屏路径一致。
		*/
		parallel_plan(slot, item_count, swapchain.format_get(), swapchain.depth_format_get(),
			targets[0].sample_count > 1 ? targets[0].sample_count : 1);
		//录制命令缓冲（场景 pass ＋ N 级后处理 pass ＋ 可选回读 ＋ 呈现前布局转换）
		command_record_chain_present(slot.command_buffer, swapchain.image_get(image_index),
			swapchain.image_view_get(image_index), swapchain.depth_view_get(image_index), swapchain.extent_get(),
			targets, target_count, items, item_count, post_items, post_count, clear_color,
			readback_target, slot.timestamp_pool,
			swapchain.msaa_color_image_get(image_index), swapchain.msaa_color_view_get(image_index),
			swapchain.msaa_depth_view_get(image_index), swapchain.sample_count_get(), ui_callback);
		//并行上下文只对本帧录制有效（清空后下一帧重新判定）
		active_secondary = nullptr;
		active_secondary_count = 0;
		//录制阶段耗时（第一百三十三轮；含全部后处理级）
		last_host_record_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();

		//取本帧呈现完成信号量（按交换链图像持有）
		VkSemaphore render_finished = swapchain.render_finished_semaphore_get(image_index);

		//提交并呈现（与交换链路径共用同一提交 / 呈现实现；两段耗时由实现回填）
		double submit_ms = 0.0;
		double present_ms = 0.0;
		if (!submit_present(slot, image_index, image_available, render_finished, swapchain, rebuild_needed_out,
			submit_ms, present_ms))
			return false;
		//提交 / 呈现阶段耗时（第一百三十三轮）
		last_host_submit_ms = submit_ms;
		last_host_present_ms = present_ms;
		//本帧主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用）
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计本帧主机侧耗时
		total_frame_ms += last_frame_ms;
		//累计主机侧分段（第一百三十四轮）
		total_host_record_ms += last_host_record_ms;
		total_host_submit_ms += last_host_submit_ms;
		total_host_present_ms += last_host_present_ms;
		//累计本帧绘制统计（场景 pass 项 ＋ 各级后处理 pass 的单个绘制项）
		frame_stats_accumulate(items, item_count);
		//本帧实际执行的后处理级数（链路径即 post_count）；记录场景目标的采样数
		last_post_stages = post_count;
		last_sample_count = targets[0].sample_count;
		for (uint32_t stage = 0; stage < post_count; ++stage)
		{
			total_draw_items += 1;
			total_instances += post_items[stage].instance_count;
		}

		//像素读出（可选；渲染与拷贝录在同一命令缓冲，等本帧槽栅栏即数据就绪）
		if (readback_active && !readback_read(slot, *readback, swapchain.extent_get()))
			return false;

		//标记该帧槽已写入时间戳（此后帧首即可读回本轮结果）
		slot.timestamp_written = true;
		//推进帧计数（帧槽轮转与延迟销毁均以它为判据）
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}

	bool Frame_Context::submit_present(Frame_Slot& slot, uint32_t image_index, VkSemaphore image_available,
		VkSemaphore render_finished, Swapchain_Context& swapchain, bool& rebuild_needed_out,
		double& submit_ms_out, double& present_ms_out)
	{
		//等待信号量提交信息（图像可用，等待发生在颜色附件输出阶段）
		VkSemaphoreSubmitInfo wait_info{};
		wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		wait_info.semaphore = image_available;
		wait_info.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		//命令缓冲提交信息
		VkCommandBufferSubmitInfo command_info{};
		command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		command_info.commandBuffer = slot.command_buffer;

		//信号量提交信息（呈现完成，供 present 等待；按交换链图像持有）
		VkSemaphoreSubmitInfo signal_info{};
		signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal_info.semaphore = render_finished;
		signal_info.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		//提交信息（等待图像可用 → 执行命令 → 信号呈现完成）
		VkSubmitInfo2 submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit_info.waitSemaphoreInfoCount = 1;
		submit_info.pWaitSemaphoreInfos = &wait_info;
		submit_info.commandBufferInfoCount = 1;
		submit_info.pCommandBufferInfos = &command_info;
		submit_info.signalSemaphoreInfoCount = 1;
		submit_info.pSignalSemaphoreInfos = &signal_info;

		//提交到图形队列（帧栅栏作为完成信号）；提交阶段耗时（第一百三十三轮）
		std::chrono::steady_clock::time_point submit_time_begin = std::chrono::steady_clock::now();
		VkResult submit_result = vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.in_flight_fence);
		submit_ms_out = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit_time_begin).count();
		//若提交失败
		if (!vk_result_check(submit_result, "vkQueueSubmit2"))
			return false;

		//交换链句柄需取地址，故用局部量承接
		VkSwapchainKHR swapchain_handle = swapchain.handle_get();
		//呈现信息（等待本图像的呈现完成信号量）
		VkPresentInfoKHR present_info{};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &render_finished;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &swapchain_handle;
		present_info.pImageIndices = &image_index;

		//呈现到呈现队列；呈现阶段耗时（第一百三十三轮）
		std::chrono::steady_clock::time_point present_time_begin = std::chrono::steady_clock::now();
		VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
		present_ms_out = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - present_time_begin).count();
		//若交换链过期或次优
		if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
		{
			//置「待重建」标志（不作为硬错误）
			rebuild_needed_out = true;
			logger.debug("提交呈现:: 呈现返回 {}，标记交换链待重建", vk_result_name_get(present_result));
			return true;
		}
		return vk_result_check(present_result, "vkQueuePresentKHR");
	}
}
