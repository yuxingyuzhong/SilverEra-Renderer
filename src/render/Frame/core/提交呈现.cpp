#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::chrono（主机侧帧耗时计时）
#include <chrono>

namespace engine
{
	//累计本帧绘制统计（绘制项数 ＋ 实例数）
	void Frame_Context::frame_stats_accumulate(const Frame_Render_Item* items, uint32_t item_count)
	{
		//累计绘制项数（按登记项计）
		total_draw_items += item_count;
		//累计实例数（各绘制项 instance_count 之和；0 表该项不完整、无实例可计）
		for (uint32_t index = 0; index < item_count; ++index)
			total_instances += items[index].instance_count;
	}

	//渲染一帧：等待栅栏 → 取图像 → 重置命令池 → 录制（可选像素回读）→ 提交并呈现
	bool Frame_Context::render(Swapchain_Context& swapchain, const Frame_Render_Item* items, uint32_t item_count,
		bool& rebuild_needed_out, const float clear_color[4], const Frame_Ui_Callback* ui_callback,
		Frame_Readback_Request* readback)
	{
		//默认无需重建
		rebuild_needed_out = false;
		//前置：帧上下文与交换链必须就绪
		if (!valid() || !swapchain.valid())
		{
			logger.error("提交呈现:: 帧上下文或交换链尚未构建");
			return false;
		}
		//若交换链图像或图像可用信号量为空
		if (swapchain.image_count_get() == 0 || swapchain.image_available_count_get() == 0
			|| swapchain.render_finished_count_get() == 0)
		{
			logger.error("提交呈现:: 交换链图像或信号量为空");
			return false;
		}

		//取当前帧槽（在途帧轮转；帧槽集合长度即 frame_overlap）
		Frame_Slot& slot = slots[frame_count % frame_overlap];

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待；K9）
		if (!slot_fence_wait(slot))
			return false;

		/*
		取图像可用信号量
		按 §5.2 归属表，图像可用信号量属「每交换链图像」，数量 = 交换链图像数（K10）；
		acquire 前图像索引未知，故以轮转游标在信号量池中顺序取用。
		由「先等帧栅栏」保证：被取用的信号量在前一次使用时已被等待消费（K13）。
		*/
		VkSemaphore image_available = swapchain.image_available_semaphore_get(acquire_cursor % swapchain.image_available_count_get());

		//取下一张交换链图像
		uint32_t image_index = 0;
		VkResult acquire = vkAcquireNextImageKHR(device, swapchain.handle_get(), UINT64_MAX,
			image_available, VK_NULL_HANDLE, &image_index);
		//若交换链过期或次优
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_SUBOPTIMAL_KHR)
		{
			//置「待重建」标志并跳过本帧（不作为硬错误；K7 / K8）
			rebuild_needed_out = true;
			logger.debug("提交呈现:: 取图像返回 {}，标记交换链待重建", vk_result_name_get(acquire));
			return true;
		}
		//若取图像遇到其它硬错误
		if (!vk_result_check(acquire, "vkAcquireNextImageKHR"))
			return false;
		//取用成功后推进信号量游标
		++acquire_cursor;

		/*
		重置本帧槽完成栅栏
		放在「取图像成功之后」而非「等待栅栏之后」：若取图像返回过期 / 次优而提前返回，
		栅栏必须保持已发出状态，否则下一帧（复用同一帧槽）vkWaitForFences 将无限等待（K9）。
		*/
		if (!vk_result_check(vkResetFences(device, 1, &slot.in_flight_fence), "vkResetFences"))
			return false;

		//重置该帧槽命令池（连带使其命令缓冲可再次录制）
		if (!vk_result_check(vkResetCommandPool(device, slot.command_pool, 0), "vkResetCommandPool"))
			return false;

		/*
		像素回读准备（可选）
		非空请求即先让回读缓冲就绪：先校验交换链图像具备传输源用法与目标容量，
		再按「宽 × 高 × 4」确保缓冲；随后录制时把颜色附件拷入该缓冲。
		*/
		VkBuffer readback_target = VK_NULL_HANDLE;
		//本帧是否请求回读（请求存在且目标缓冲非空）
		bool readback_active = readback != nullptr && readback->pixels != nullptr;
		//若本帧请求回读
		if (readback_active)
		{
			//前置：交换链图像须具备传输源用法
			if (!swapchain.color_transfer_src_get())
			{
				logger.error("提交呈现:: 交换链图像不具备传输源用法，像素回读不可用");
				return false;
			}
			//本帧像素字节数（RGBA8 紧排）
			VkExtent2D readback_extent = swapchain.extent_get();
			VkDeviceSize readback_bytes = static_cast<VkDeviceSize>(readback_extent.width) * readback_extent.height * 4;
			//若回读目标容量不足
			if (readback->capacity < readback_bytes)
			{
				logger.error("提交呈现:: 回读目标容量不足（需 {} 字节，有 {} 字节）", readback_bytes, readback->capacity);
				return false;
			}
			//确保回读缓冲就绪并把句柄交给录制
			if (!readback_buffer_ensure(readback_bytes))
				return false;
			readback_target = readback_buffer;
		}

		//本帧计时起点（主机侧录制 ＋ 提交 ＋ 呈现调用）
		std::chrono::steady_clock::time_point frame_time_begin = std::chrono::steady_clock::now();
		//录制命令缓冲（布局转换 + 清屏 + 逐项绘制 + UI 阶段 + 可选像素回读 + 呈现前布局转换）
		command_record(slot.command_buffer, swapchain.image_get(image_index),
			swapchain.image_view_get(image_index), swapchain.depth_view_get(image_index),
			swapchain.extent_get(), items, item_count, clear_color, ui_callback, readback_target);

		/*
		取本帧呈现完成信号量
		按 §5.2 归属表，呈现完成信号量属「每交换链图像」，数量 = 交换链图像数；
		按本次 acquire 得到的 image_index 取用：同一信号量只会在同一张交换链图像
		被再次获取时才复用，而那时上一次 present 必然已消费它（帧栅栏只保证
		提交完成，不保证 present 已取走信号量）。
		*/
		VkSemaphore render_finished = swapchain.render_finished_semaphore_get(image_index);

		//提交并呈现
		if (!submit_present(slot, image_index, image_available, render_finished, swapchain, rebuild_needed_out))
			return false;
		//本帧主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用）
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计本帧绘制统计
		frame_stats_accumulate(items, item_count);

		/*
		像素读出（可选）
		渲染与拷贝录在同一命令缓冲，故等本帧槽栅栏即可保证像素已落入回读缓冲；
		读出后回填宽 / 高 / 字节数供调用方取用。
		*/
		if (readback_active && !readback_read(slot, *readback, swapchain.extent_get()))
			return false;

		//推进帧计数（帧槽轮转与延迟销毁均以它为判据）
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}

	/*
	渲染一帧到离屏目标
	帧槽等待 → 重置命令池 → 录制（目标附件 + 场景 pass + 可选像素回读）→ 仅以完成栅栏提交 → 回读。
	不取交换链图像、不呈现（离屏结果经「以目标为纹理」的正常绘制路径显示，即后处理闭环）；
	与 render 共用同一批帧槽与帧计数轮转，故两者可交替调用。
	*/
	bool Frame_Context::render_to_target(const Frame_Render_Target& target, const Frame_Render_Item* items,
		uint32_t item_count, const float clear_color[4], const Frame_Ui_Callback* ui_callback,
		Frame_Readback_Request* readback)
	{
		//前置：帧上下文与目标附件必须齐备
		if (!valid() || target.color_image == VK_NULL_HANDLE || target.color_view == VK_NULL_HANDLE
			|| target.extent.width == 0 || target.extent.height == 0)
		{
			logger.error("提交呈现:: 帧上下文或离屏目标尚未就绪");
			return false;
		}

		//取当前帧槽（与交换链路径共用同一轮转）
		Frame_Slot& slot = slots[frame_count % frame_overlap];

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待）
		if (!slot_fence_wait(slot))
			return false;

		//重置该帧槽完成栅栏（提交前必须处于未发出态）
		if (!vk_result_check(vkResetFences(device, 1, &slot.in_flight_fence), "vkResetFences"))
			return false;

		//重置该帧槽命令池（连带使其命令缓冲可再次录制）
		if (!vk_result_check(vkResetCommandPool(device, slot.command_pool, 0), "vkResetCommandPool"))
			return false;

		/*
		像素回读准备（可选；回读来源为目标颜色附件）
		按目标范围校验容量并确保回读缓冲就绪。
		*/
		VkBuffer readback_target = VK_NULL_HANDLE;
		//本帧是否请求回读（请求存在且目标缓冲非空）
		bool readback_active = readback != nullptr && readback->pixels != nullptr;
		//若本帧请求回读
		if (readback_active)
		{
			//目标像素字节数（RGBA8 紧排）
			VkDeviceSize readback_bytes = static_cast<VkDeviceSize>(target.extent.width) * target.extent.height * 4;
			//若回读目标容量不足
			if (readback->capacity < readback_bytes)
			{
				logger.error("提交呈现:: 离屏回读目标容量不足（需 {} 字节，有 {} 字节）", readback_bytes, readback->capacity);
				return false;
			}
			//确保回读缓冲就绪并把句柄交给录制
			if (!readback_buffer_ensure(readback_bytes))
				return false;
			readback_target = readback_buffer;
		}

		//本帧计时起点（主机侧录制 ＋ 提交）
		std::chrono::steady_clock::time_point frame_time_begin = std::chrono::steady_clock::now();
		//录制命令缓冲（目标附件 + 清屏 + 逐项绘制 + UI 阶段 + 可选像素回读 + 回到着色器可读布局）
		command_record_target(slot.command_buffer, target, items, item_count, clear_color, ui_callback, readback_target);

		/*
		仅以完成栅栏提交
		离屏路径不取交换链图像、不呈现，故无信号量同步；命令缓冲完成后帧槽栅栏发出。
		*/
		VkCommandBufferSubmitInfo command_info{};
		command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		command_info.commandBuffer = slot.command_buffer;

		VkSubmitInfo2 submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit_info.commandBufferInfoCount = 1;
		submit_info.pCommandBufferInfos = &command_info;

		//提交到图形队列（帧栅栏作为完成信号）
		if (!vk_result_check(vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.in_flight_fence), "vkQueueSubmit2（离屏）"))
			return false;
		//本帧主机侧耗时（毫秒；录制 ＋ 提交）
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计本帧绘制统计
		frame_stats_accumulate(items, item_count);

		//像素读出（可选；渲染与拷贝同批提交，等帧槽栅栏即数据就绪）
		if (readback_active && !readback_read(slot, *readback, target.extent))
			return false;

		//推进帧计数（帧槽轮转与延迟销毁均以它为判据）
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}

	/*
	渲染一帧：先把场景渲染进离屏目标，再在同一命令缓冲内以该目标为纹理做一次全屏后处理 pass 呈现到交换链
	（同帧多 pass）。取图像 / 提交 / 呈现 / 像素回读与交换链路径同口径，仅录制内容不同。
	*/
	bool Frame_Context::render_to_target_present(Swapchain_Context& swapchain, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item& post_item,
		const float clear_color[4], bool& rebuild_needed_out, Frame_Readback_Request* readback)
	{
		//默认无需重建
		rebuild_needed_out = false;
		//前置：帧上下文、交换链与离屏目标必须齐备
		if (!valid() || !swapchain.valid())
		{
			logger.error("提交呈现:: 帧上下文或交换链尚未构建（同帧多 pass）");
			return false;
		}
		//若交换链图像或信号量为空
		if (swapchain.image_count_get() == 0 || swapchain.image_available_count_get() == 0
			|| swapchain.render_finished_count_get() == 0)
		{
			logger.error("提交呈现:: 交换链图像或信号量为空（同帧多 pass）");
			return false;
		}
		//若离屏目标附件不完整
		if (target.color_image == VK_NULL_HANDLE || target.color_view == VK_NULL_HANDLE
			|| target.depth_view == VK_NULL_HANDLE || target.extent.width == 0 || target.extent.height == 0)
		{
			logger.error("提交呈现:: 离屏目标尚未就绪（同帧多 pass）");
			return false;
		}
		//若后处理绘制项不完整
		if (post_item.pipeline == VK_NULL_HANDLE || post_item.layout == VK_NULL_HANDLE
			|| post_item.vertex_buffer == VK_NULL_HANDLE || post_item.index_buffer == VK_NULL_HANDLE
			|| post_item.index_count == 0 || post_item.instance_buffer == VK_NULL_HANDLE
			|| post_item.instance_count == 0)
		{
			logger.error("提交呈现:: 后处理绘制项不完整（同帧多 pass）");
			return false;
		}

		//取当前帧槽（与其余路径共用同一轮转）
		Frame_Slot& slot = slots[frame_count % frame_overlap];

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待）
		if (!slot_fence_wait(slot))
			return false;

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
			logger.debug("提交呈现:: 取图像返回 {}，标记交换链待重建（同帧多 pass）", vk_result_name_get(acquire));
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

		//重置该帧槽命令池（连带使其命令缓冲可再次录制）
		if (!vk_result_check(vkResetCommandPool(device, slot.command_pool, 0), "vkResetCommandPool"))
			return false;

		/*
		像素回读准备（可选；回读来源为最终呈现到交换链的颜色附件，故按交换链范围校验）
		*/
		VkBuffer readback_target = VK_NULL_HANDLE;
		//本帧是否请求回读（请求存在且目标缓冲非空）
		bool readback_active = readback != nullptr && readback->pixels != nullptr;
		//若本帧请求回读
		if (readback_active)
		{
			//前置：交换链图像须具备传输源用法
			if (!swapchain.color_transfer_src_get())
			{
				logger.error("提交呈现:: 交换链图像不具备传输源用法，像素回读不可用（同帧多 pass）");
				return false;
			}
			//本帧像素字节数（RGBA8 紧排，按交换链范围）
			VkExtent2D readback_extent = swapchain.extent_get();
			VkDeviceSize readback_bytes = static_cast<VkDeviceSize>(readback_extent.width) * readback_extent.height * 4;
			//若回读目标容量不足
			if (readback->capacity < readback_bytes)
			{
				logger.error("提交呈现:: 回读目标容量不足（需 {} 字节，有 {} 字节；同帧多 pass）", readback_bytes, readback->capacity);
				return false;
			}
			//确保回读缓冲就绪并把句柄交给录制
			if (!readback_buffer_ensure(readback_bytes))
				return false;
			readback_target = readback_buffer;
		}

		//本帧计时起点（主机侧录制 ＋ 提交 ＋ 呈现调用）
		std::chrono::steady_clock::time_point frame_time_begin = std::chrono::steady_clock::now();
		//录制命令缓冲（目标 pass ＋ 后处理 pass ＋ 可选回读 ＋ 呈现前布局转换）
		command_record_target_present(slot.command_buffer, swapchain.image_get(image_index),
			swapchain.image_view_get(image_index), swapchain.depth_view_get(image_index), swapchain.extent_get(),
			target, items, item_count, post_item, clear_color, readback_target);

		//取本帧呈现完成信号量（按交换链图像持有）
		VkSemaphore render_finished = swapchain.render_finished_semaphore_get(image_index);

		//提交并呈现（与交换链路径共用同一提交 / 呈现实现）
		if (!submit_present(slot, image_index, image_available, render_finished, swapchain, rebuild_needed_out))
			return false;
		//本帧主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用）
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计本帧绘制统计（场景 pass 项 ＋ 后处理 pass 的单个绘制项）
		frame_stats_accumulate(items, item_count);
		total_draw_items += 1;
		total_instances += post_item.instance_count;

		//像素读出（可选；渲染与拷贝录在同一命令缓冲，等本帧槽栅栏即数据就绪）
		if (readback_active && !readback_read(slot, *readback, swapchain.extent_get()))
			return false;

		//推进帧计数（帧槽轮转与延迟销毁均以它为判据）
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}

	//提交并呈现
	bool Frame_Context::submit_present(Frame_Slot& slot, uint32_t image_index, VkSemaphore image_available,
		VkSemaphore render_finished, Swapchain_Context& swapchain, bool& rebuild_needed_out)
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

		//提交到图形队列（帧栅栏作为完成信号）
		if (!vk_result_check(vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.in_flight_fence), "vkQueueSubmit2"))
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

		//呈现到呈现队列
		VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
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
