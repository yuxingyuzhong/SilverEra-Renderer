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

	bool Frame_Context::render(Swapchain_Context& swapchain, const Frame_Render_Item* items, uint32_t item_count,
		bool& rebuild_needed_out, const float clear_color[4], const Frame_Ui_Callback* ui_callback,
		Frame_Readback_Request* readback, const Frame_Post_Scene_Callback* post_scene)
	{
		//默认无需重建
		rebuild_needed_out = false;
		//清零本帧间接命令计数（第二百五十二轮）
		frame_indirect_commands = 0;
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
		//载入该帧槽的间接录制上下文（第二百五十二轮）
		indirect_buffer = slot.indirect_buffer;
		indirect_mapped = slot.indirect_mapped;
		indirect_capacity = slot.indirect_capacity;

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待；K9）
		if (!slot_fence_wait(slot))
			return false;
		//读回该帧槽上一轮 GPU 时间戳（此时上一轮已执行完；无能力 / 首轮时静默跳过）
		gpu_timestamp_read(slot);

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

		//重置该帧槽全部命令池（主池 ＋ 次级池；连带使其命令缓冲可再次录制）
		if (!frame_slot_pools_reset(device, slot))
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
		/*
		场景 pass 的并行录制规划（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		链 / 直通 / 离屏三条路径同口径：线程数、绘制项、段数上限、次级缓冲任一不满足即保持单线程（返回 0）。
		*/
		parallel_plan(slot, item_count, swapchain.format_get(), swapchain.depth_format_get(),
			swapchain.sample_count_get());
		//录制命令缓冲（布局转换 + 清屏 + 逐项绘制 + UI 阶段 + 可选像素回读 + 呈现前布局转换）
		command_record(slot.command_buffer, swapchain.image_get(image_index),
			swapchain.image_view_get(image_index), swapchain.depth_image_get(image_index),
			swapchain.depth_view_get(image_index),
			swapchain.extent_get(), items, item_count, clear_color, ui_callback, readback_target,
			slot.timestamp_pool,
			swapchain.msaa_color_image_get(image_index), swapchain.msaa_color_view_get(image_index),
			swapchain.msaa_depth_view_get(image_index), swapchain.msaa_depth_image_get(image_index),
			swapchain.sample_count_get(), post_scene);
		//并行上下文只对本帧录制有效
		active_secondary = nullptr;
		active_secondary_count = 0;
		//录制阶段耗时（第一百三十三轮）
		last_host_record_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();

		/*
		取本帧呈现完成信号量
		按 §5.2 归属表，呈现完成信号量属「每交换链图像」，数量 = 交换链图像数；
		按本次 acquire 得到的 image_index 取用：同一信号量只会在同一张交换链图像
		被再次获取时才复用，而那时上一次 present 必然已消费它（帧栅栏只保证
		提交完成，不保证 present 已取走信号量）。
		*/
		VkSemaphore render_finished = swapchain.render_finished_semaphore_get(image_index);

		//提交并呈现（提交 / 呈现两段耗时由实现回填）
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
		//累计本帧绘制统计
		frame_stats_accumulate(items, item_count);
		//交换链直通路径无后处理级；记录本帧采样数
		last_post_stages = 0;
		last_sample_count = swapchain.sample_count_get();

		/*
		像素读出（可选）
		渲染与拷贝录在同一命令缓冲，故等本帧槽栅栏即可保证像素已落入回读缓冲；
		读出后回填宽 / 高 / 字节数供调用方取用。
		*/
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

	bool Frame_Context::render_to_target(const Frame_Render_Target& target, const Frame_Render_Item* items,
		uint32_t item_count, const float clear_color[4], const Frame_Ui_Callback* ui_callback,
		Frame_Readback_Request* readback, bool shadow_pass, const Frame_Post_Scene_Callback* post_scene)
	{
		//前置：帧上下文与目标附件必须齐备（含深度视图，与同帧多 pass 路径同口径）
		if (!valid() || target.color_image == VK_NULL_HANDLE || target.color_view == VK_NULL_HANDLE
			|| target.depth_view == VK_NULL_HANDLE || target.extent.width == 0 || target.extent.height == 0)
		{
			logger.error("提交呈现:: 帧上下文或离屏目标尚未就绪");
			return false;
		}

		//清零本帧间接命令计数（第二百五十二轮）
		frame_indirect_commands = 0;

		//取当前帧槽（与交换链路径共用同一轮转）
		const uint32_t slot_index = frame_count % frame_overlap;
		Frame_Slot& slot = slots[slot_index];
		/*
		载入该帧槽的间接录制上下文（第二百五十二轮）
		阴影 pass 与主场景共用同一帧槽缓冲，若两者都写会互相覆盖，故阴影 pass 强制回落逐项绘制。
		*/
		indirect_buffer = shadow_pass ? VK_NULL_HANDLE : slot.indirect_buffer;
		indirect_mapped = shadow_pass ? nullptr : slot.indirect_mapped;
		indirect_capacity = shadow_pass ? 0 : slot.indirect_capacity;

		//等待该帧槽前一次提交完成（先查状态，未完成再阻塞等待）
		if (!slot_fence_wait(slot))
			return false;
		//读回该帧槽上一轮 GPU 时间戳（此时上一轮已执行完；无能力 / 首轮时静默跳过）
		gpu_timestamp_read(slot);

		//重置该帧槽完成栅栏（提交前必须处于未发出态）
		if (!vk_result_check(vkResetFences(device, 1, &slot.in_flight_fence), "vkResetFences"))
			return false;

		//重置该帧槽全部命令池（主池 ＋ 次级池；连带使其命令缓冲可再次录制）
		if (!frame_slot_pools_reset(device, slot))
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
		/*
		场景 pass 的并行录制规划（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		格式与采样数取目标结构体字段（第二百一十轮起阴影 pass 亦补齐目标格式，故不再排除阴影）；
		链 / 直通 / 离屏 / 阴影四条路径同口径：线程数、绘制项、段数上限、次级缓冲任一不满足即保持单线程（返回 0）。
		*/
		parallel_plan(slot, item_count, target.color_format, target.depth_format, target.sample_count);
		//录制命令缓冲（目标附件 + 清屏 + 逐项绘制 + UI 阶段 + 可选像素回读 + 回到着色器可读布局）
		//阴影 pass 使用独立的两查询池（第一百二十八轮），其余离屏路径沿用主四查询池
		command_record_target(slot.command_buffer, target, items, item_count, clear_color, ui_callback,
			readback_target, shadow_pass ? slot.shadow_timestamp_pool : slot.timestamp_pool, shadow_pass,
			post_scene);
		//并行上下文只对本帧录制有效
		active_secondary = nullptr;
		active_secondary_count = 0;
		//录制阶段耗时（第一百三十三轮）
		last_host_record_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();

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

		//提交到图形队列（帧栅栏作为完成信号）；提交阶段耗时（第一百三十三轮）
		std::chrono::steady_clock::time_point submit_time_begin = std::chrono::steady_clock::now();
		if (!vk_result_check(vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.in_flight_fence), "vkQueueSubmit2（离屏）"))
			return false;
		last_host_submit_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit_time_begin).count();
		//离屏路径不呈现
		last_host_present_ms = 0.0;
		//本帧主机侧耗时（毫秒；录制 ＋ 提交）
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计本帧主机侧耗时
		total_frame_ms += last_frame_ms;
		//累计主机侧分段（第一百三十四轮）
		total_host_record_ms += last_host_record_ms;
		total_host_submit_ms += last_host_submit_ms;
		total_host_present_ms += last_host_present_ms;
		//累计本帧绘制统计
		frame_stats_accumulate(items, item_count);
		//离屏路径无后处理级；记录目标采样数
		last_post_stages = 0;
		last_sample_count = target.sample_count;

		//像素读出（可选；渲染与拷贝同批提交，等帧槽栅栏即数据就绪）
		if (readback_active && !readback_read(slot, *readback, target.extent))
			return false;

		//标记该帧槽已写入时间戳（阴影 pass 记独立池与槽号，其余记主四查询池）
		if (shadow_pass)
		{
			slot.shadow_timestamp_written = true;
			last_shadow_slot = slot_index;
		}
		else
			slot.timestamp_written = true;
		//推进帧计数（帧槽轮转与延迟销毁均以它为判据）
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}

	bool Frame_Context::render_to_targets_batch(const Frame_Render_Target* targets, const float* const* pass_matrices,
		uint32_t pass_count, Frame_Render_Item* items, uint32_t item_count,
		const float clear_color[4], const bool* clear_flags, bool shadow_pass)
	{
		//前置：段数、目标 / 矩阵 / 清屏标志与渲染项必须齐备；本入口仅服务阴影 pass
		if (!valid() || targets == nullptr || pass_matrices == nullptr || clear_flags == nullptr
			|| pass_count == 0 || (item_count != 0 && items == nullptr) || !shadow_pass)
		{
			logger.error("提交呈现:: 批量阴影入口参数非法（段数 {}，项数 {}，阴影 {}）", pass_count, item_count, shadow_pass);
			return false;
		}

		//清零本帧间接命令计数（批量路径恒逐项绘制）
		frame_indirect_commands = 0;

		//取当前帧槽（与交换链路径共用同一轮转）
		const uint32_t slot_index = frame_count % frame_overlap;
		Frame_Slot& slot = slots[slot_index];
		//批量路径恒逐项绘制，不载入间接录制上下文
		indirect_buffer = VK_NULL_HANDLE;
		indirect_mapped = nullptr;
		indirect_capacity = 0;

		//等待该帧槽前一次提交完成 ＋ 读回上一轮 GPU 时间戳
		if (!slot_fence_wait(slot))
			return false;
		gpu_timestamp_read(slot);
		//重置完成栅栏与全部命令池（后续分配的主缓冲随之可录制）
		if (!vk_result_check(vkResetFences(device, 1, &slot.in_flight_fence), "vkResetFences"))
			return false;
		if (!frame_slot_pools_reset(device, slot))
			return false;

		/*
		命令缓冲集合（第六百七十一轮改为帧槽预分配复用）
		首段复用帧槽主缓冲；其余段使用本槽 batch_buffers（首次需要时按缺额分配，之后逐帧复用）；
		命令池重置不影响缓冲句柄有效性，故无需每帧重新分配。
		*/
		const uint32_t extra_segments = pass_count > 1 ? pass_count - 1 : 0;
		//若预分配缓冲不足则按缺额一次性追加分配
		if (slot.batch_buffers.size() < extra_segments)
		{
			const uint32_t grow = extra_segments - static_cast<uint32_t>(slot.batch_buffers.size());
			std::vector<VkCommandBuffer> allocated(grow, VK_NULL_HANDLE);
			VkCommandBufferAllocateInfo allocate_info{};
			allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocate_info.commandPool = slot.command_pool;
			allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocate_info.commandBufferCount = grow;
			//若分配失败
			if (!vk_result_check(vkAllocateCommandBuffers(device, &allocate_info, allocated.data()),
				"vkAllocateCommandBuffers(批量阴影预分配)"))
			{
				logger.error("提交呈现:: 批量阴影命令缓冲预分配失败（新增 {} 个）", grow);
				return false;
			}
			slot.batch_buffers.insert(slot.batch_buffers.end(), allocated.begin(), allocated.end());
		}
		//装填本帧命令表（复用常驻 vector）
		batch_commands.resize(pass_count);
		batch_commands[0] = slot.command_buffer;
		for (uint32_t pass = 1; pass < pass_count; ++pass)
			batch_commands[pass] = slot.batch_buffers[pass - 1];

		//本帧计时起点（主机侧录制 ＋ 提交）
		std::chrono::steady_clock::time_point frame_time_begin = std::chrono::steady_clock::now();
		//逐段录制（首段重置查询池并写起点时间戳、末段写终点时间戳，中间段两者都不写）
		for (uint32_t pass = 0; pass < pass_count; ++pass)
		{
			const Frame_Render_Target& target = targets[pass];
			//目标附件必须齐备
			if (target.color_image == VK_NULL_HANDLE || target.color_view == VK_NULL_HANDLE
				|| target.depth_view == VK_NULL_HANDLE || target.extent.width == 0 || target.extent.height == 0)
			{
				logger.error("提交呈现:: 批量阴影目标附件不完整（段 {}）", pass);
				return false;
			}
			//本段矩阵写入全部绘制项的推送常量
			for (uint32_t index = 0; index < item_count; ++index)
				items[index].push_constants = pass_matrices[pass];
			//本段录制（后续段加前导屏障；清屏标志逐段决定）
			command_record_target(batch_commands[pass], target, items, item_count,
				clear_flags[pass] ? clear_color : nullptr, nullptr, VK_NULL_HANDLE,
				slot.shadow_timestamp_pool, true, nullptr,
				pass == 0, pass == 0, pass + 1 == pass_count, pass != 0);
			//本段绘制统计（与逐 pass 旧路径同口径：每段累计一次）
			frame_stats_accumulate(items, item_count);
		}
		//录制阶段耗时（第一百三十三轮）
		last_host_record_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();

		/*
		单次提交全部命令缓冲
		同一提交内的命令缓冲按顺序开始执行；各段目标独立、时间戳仅首末段写入，故无跨段同步需求。
		提交信息表复用常驻 vector，避免逐帧堆分配。
		*/
		batch_submit_infos.resize(pass_count);
		for (uint32_t pass = 0; pass < pass_count; ++pass)
		{
			batch_submit_infos[pass].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
			batch_submit_infos[pass].commandBuffer = batch_commands[pass];
		}
		VkSubmitInfo2 submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit_info.commandBufferInfoCount = pass_count;
		submit_info.pCommandBufferInfos = batch_submit_infos.data();

		//提交到图形队列（帧栅栏作为完成信号）
		std::chrono::steady_clock::time_point submit_time_begin = std::chrono::steady_clock::now();
		if (!vk_result_check(vkQueueSubmit2(graphics_queue, 1, &submit_info, slot.in_flight_fence),
			"vkQueueSubmit2（批量阴影）"))
			return false;
		last_host_submit_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submit_time_begin).count();
		//离屏路径不呈现
		last_host_present_ms = 0.0;
		last_frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_time_begin).count();
		//累计主机侧耗时
		total_frame_ms += last_frame_ms;
		total_host_record_ms += last_host_record_ms;
		total_host_submit_ms += last_host_submit_ms;
		total_host_present_ms += last_host_present_ms;
		//无后处理级；记录目标采样数
		last_post_stages = 0;
		last_sample_count = targets[0].sample_count;

		//标记该帧槽已写入阴影时间戳并推进帧计数
		slot.shadow_timestamp_written = true;
		last_shadow_slot = slot_index;
		++frame_count;
		//释放到期的延迟销毁条目
		deferred_flush();
		return true;
	}
}
