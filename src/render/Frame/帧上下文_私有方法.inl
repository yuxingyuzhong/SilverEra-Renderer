		/*
		等待某帧槽前一次提交完成（实现见 core/帧槽创建.cpp）
		快速路径：先查栅栏状态，已完成则免去一次阻塞等待调用；
		未完成或查询出错才落到 vkWaitForFences / 错误返回。
		*/
		bool slot_fence_wait(Frame_Slot& slot);
		//创建单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		bool frame_slot_create(Frame_Slot& slot);
		//销毁单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		void frame_slot_destroy(Frame_Slot& slot);
		/*
		确保该帧槽的次级命令缓冲数量（第一百九十七轮；实现见 core/帧槽创建.cpp）
		不足时经该槽命令池以 SECONDARY 级别追加分配；失败返回假并保持已有数量。
		*/
		bool secondary_buffers_ensure(Frame_Slot& slot, uint32_t count);
		/*
		录制命令缓冲（实现见 core/命令录制.cpp）
		颜色 / 深度附件布局转换为动态渲染，随后按渲染项依次绑定并 vkCmdDrawIndexed；
		场景 pass 结束后若给定 UI 回调则交还上层绘制 UI；
		若给定回读缓冲则在呈现转换前转入 TRANSFER_SRC 并把颜色附件拷入该缓冲（再经呈现前布局转换收尾）。
		*/
		void command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
			VkImage depth_image, VkImageView depth_view, VkExtent2D extent,
			const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr, VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE,
			VkImage msaa_image = VK_NULL_HANDLE, VkImageView msaa_color_view = VK_NULL_HANDLE,
			VkImageView msaa_depth_view = VK_NULL_HANDLE, VkImage msaa_depth_image = VK_NULL_HANDLE,
			uint32_t sample_count = 1,
			const Frame_Post_Scene_Callback* post_scene = nullptr);
		/*
		录制「渲染到离屏目标」的命令缓冲（实现见 core/命令录制.cpp）
		颜色进入布局为 SHADER_READ_ONLY_OPTIMAL，故先转入 COLOR_ATTACHMENT_OPTIMAL；
		场景 pass 结束后按是否回读转入 TRANSFER_SRC（回读）或直接转回 SHADER_READ_ONLY，使目标帧间可采样。
		*/
		void command_record_target(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
			const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr, VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE, bool shadow_pass = false,
			const Frame_Post_Scene_Callback* post_scene = nullptr,
			bool reset_query = true, bool write_begin_timestamp = true, bool write_end_timestamp = true,
			bool leading_barrier = false);
		/*
		录制「场景 pass ＋ N 级后处理 pass（多目标轮转）＋ UI ＋ 呈现」的命令缓冲（实现见 core/命令录制.cpp）
		场景 pass：targets[0] 颜色 SHADER_READ_ONLY → COLOR_ATTACHMENT，绘制 items 后转回 SHADER_READ_ONLY；
		后处理各级：源（targets[i % target_count]）恒 SHADER_READ_ONLY → 以 post_items[i] 全屏绘制到宿
		（末级为交换链，否则 targets[(i + 1) % target_count]，写完后转回 SHADER_READ_ONLY）；
		末级结束后若给定 UI 回调，则在交换链图像上再开一次「保留内容」的 UI 通道（UI 不经后处理）；
		交换链图像收尾按是否回读转入 TRANSFER_SRC + PRESENT_SRC 或直接转 PRESENT_SRC（与既有路径同口径）。
		*/
		void command_record_chain_present(VkCommandBuffer command_buffer, VkImage swapchain_image,
			VkImageView swapchain_view, VkImageView swapchain_depth_view, VkExtent2D swapchain_extent,
			const Frame_Render_Target* targets, uint32_t target_count,
			const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item* post_items,
			uint32_t post_count, const float clear_color[4], VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE,
			VkImage swapchain_msaa_image = VK_NULL_HANDLE, VkImageView swapchain_msaa_color_view = VK_NULL_HANDLE,
			VkImageView swapchain_msaa_depth_view = VK_NULL_HANDLE, uint32_t swapchain_sample_count = 1,
			const Frame_Ui_Callback* ui_callback = nullptr);
		/*
		逐渲染项绘制（状态缓存的管线 / 描述符集绑定 ＋ vkCmdDrawIndexed；实现见 core/命令录制.cpp）
		由 command_record（交换链路径）与 command_record_target（离屏路径）共用。
		*/
		Recorded_Counts items_record(VkCommandBuffer command_buffer, const Frame_Render_Item* items, uint32_t item_count);
		/*
		UI 阶段录制（实现见 core/命令录制.cpp；第二百一十四轮从两处内联代码抽取）
		MSAA 解析完成后以「加载后保留」的单采样通道绘制 UI：先做同布局依赖屏障，
		再开仅含颜色附件的动态渲染，恢复视口 / 裁剪后调用上层回调。
		交换链直通 / 离屏目标在后处理前调用；链路径在**末级后处理之后**调用（UI 不吃后处理）。
		*/
		void ui_pass_record(VkCommandBuffer command_buffer, VkImage color_image, VkImageView color_view,
			VkExtent2D extent, VkViewport viewport, VkRect2D scissor, const Frame_Ui_Callback* ui_callback);
		/*
		并行录制规划（实现见 core/并行录制.cpp；第二百零八轮统一口径）
		条件：线程池已启动（> 1 线程）、绘制项 ≥ parallel_min_items、
		段数 = min(线程数, parallel_max_segments, 项数 / 16) ≥ 2、该帧槽次级缓冲就绪；
		满足时填充并行上下文并返回段数，否则返回 0（调用方走单线程录制）。
		链 / 直通 / 离屏三条录制路径共用本方法，避免段数口径分叉。
		*/
		uint32_t parallel_plan(Frame_Slot& slot, uint32_t item_count, VkFormat color_format,
			VkFormat depth_format, uint32_t sample_count);
		/*
		场景 pass 并行段录制（实现见 core/并行录制.cpp；第二百零八轮统一口径）
		active_secondary_count ≥ 2 且存在绘制项时，把绘制项按「项数均分」切成若干段，
		每段由一个次级命令缓冲**完整包含**一个动态渲染实例（首段沿用入参 loadOp、其余保留；
		多重采样解析只在末段开启）；单采样时 UI 回调放在末段（与单线程位置一致）；
		主缓冲一次 vkCmdExecuteCommands 顺序执行各段（GPU 执行顺序与单线程一致）。
		成功返回实际段数，并把各段绘制量汇总进 frame_recorded、回填 last_parallel_segments；
		返回 0 表示未并行（调用方须自行录制单线程路径）。
		*/
		uint32_t scene_parallel_record(VkCommandBuffer command_buffer, const VkRenderingInfo& rendering_info,
			const VkRenderingAttachmentInfo& color_attachment, const VkRenderingAttachmentInfo& depth_attachment,
			VkViewport viewport, VkRect2D scissor, const Frame_Render_Item* items, uint32_t item_count,
			uint32_t sample_count, const Frame_Ui_Callback* ui_callback);
		//提交并呈现（实现见 core/提交呈现.cpp；render_finished 为按交换链图像持有的呈现完成信号量）
		/*
		提交并呈现
		submit_ms_out / present_ms_out 回填「vkQueueSubmit2」与「vkQueuePresentKHR」调用的主机侧耗时
		（毫秒；第一百三十三轮），供门面主机侧分段观测。
		*/
		bool submit_present(Frame_Slot& slot, uint32_t image_index, VkSemaphore image_available,
			VkSemaphore render_finished, Swapchain_Context& swapchain, bool& rebuild_needed_out,
			double& submit_ms_out, double& present_ms_out);
		/*
		累计本帧绘制统计（实现见 core/提交呈现.cpp）
		绘制项数按登记项计；实例数取各绘制项 instance_count 之和（0 表该项不完整、无实例可计）。
		*/
		void frame_stats_accumulate(const Frame_Render_Item* items, uint32_t item_count);
		/*
		读回某帧槽上一轮 GPU 时间戳并换算毫秒（实现见 core/提交呈现.cpp）
		调用时机：该槽栅栏等待完成之后、本帧录制之前（此时上一轮已执行完，结果可读）。
		无查询池 / 尚未写过 / 结果不可用时静默跳过，不改动 last_gpu_frame_ms。
		*/
		void gpu_timestamp_read(Frame_Slot& slot);
		//释放到期的延迟销毁条目（实现见 core/延迟销毁.cpp）
		void deferred_flush(void);
		//释放单个延迟条目的资源（实现见 core/延迟销毁.cpp）
		void deferred_entry_release(Deferred_Entry& entry);
		/*
		立即释放全部延迟销毁条目（销毁期调用；实现见 core/延迟销毁.cpp）
		关闭时已等设备空闲，无需再按 release_frame 判据，直接释放全部残留条目，
		避免旧交换链附属资源（尤其深度图像与其内存）泄漏到内存模块销毁之后。
		*/
		void deferred_release_all(void);
		/*
		确保回读缓冲就绪（实现见 core/像素回读.cpp）
		尚未创建或字节数不足时按需创建 / 重建（主机可见，用途 TRANSFER_DST）；
		返回假视为硬错误。
		*/
		bool readback_buffer_ensure(VkDeviceSize size);
		//销毁回读缓冲（实现见 core/像素回读.cpp）
		void readback_buffer_destroy(void);
		/*
		帧后读出像素（实现见 core/像素回读.cpp）
		等本帧槽栅栏（拷贝与渲染同在一个命令缓冲，栅栏发出即数据就绪）→ 主机读回 →
		回填宽 / 高 / 字节数。
		*/
		bool readback_read(Frame_Slot& slot, Frame_Readback_Request& request, VkExtent2D extent);
