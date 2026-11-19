		//构造（不创建任何 Vulkan 对象）
		Frame_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Frame_Context(void);
		//不可拷贝、不可移动（持有 Vulkan 句柄与队列）
		Frame_Context(const Frame_Context&) = delete;
		Frame_Context& operator=(const Frame_Context&) = delete;

		/*
		构建
		创建 frame_overlap 个帧槽（命令池 / 命令缓冲 / 栅栏）。
		swapchain：已构建的交换链（帧链路取图像依赖它）。
		memory：内存模块（像素回读缓冲的创建 / 销毁依赖它）。
		*/
		bool build(VkDevice device, VkPhysicalDevice physical_device,
			uint32_t graphics_family, VkQueue graphics_queue,
			uint32_t present_family, VkQueue present_queue,
			Swapchain_Context& swapchain, Memory_Context& memory, uint32_t frame_overlap);
		//销毁（幂等；先等设备空闲，再逆序销毁帧槽）
		void destroy(void);
		/*
		推送常量是否含后处理第四组（第四百零一轮）
		由门面按设备 maxPushConstantsSize 决定：真 → 各级推送 16 个浮点（到 offset 144），
		假 → 仍推 12 个浮点（到 offset 128），保证布局上限一致的设备照常出帧。
		*/
		bool post_extra_enabled = false;
		//是否已构建完成（至少一个帧槽）
		bool valid(void) const;
		/*
		设置并行录制线程池（第一百九十七轮；可空 = 全部单线程）
		仅影响「场景目标 → 同帧后处理链」路径的场景 pass：绘制项按段切给次级命令缓冲并行录制；
		线程池未启动或绘制项过少时自动退回单线程录制（画面与统计逐字节一致）。
		*/
		void thread_pool_set(Thread_Pool* pool);
		/*
		设置并行录制的最小绘制项数（第二百零二轮；钳到 2..4096）
		绘制项少于此值时即使有工作线程也走单线程录制。
		*/
		void parallel_min_items_set(uint32_t count);
		/*
		设置并行录制的最大段数（第二百零三轮；钳到 2..32）
		*/
		void parallel_max_segments_set(uint32_t count);

		/*
		渲染一帧（取图像 → 录制 → 提交 → 呈现，可选像素回读）
		swapchain：当前交换链；rebuild_needed_out：置真表示本帧遇到过期 / 次优，需上层重建；
		items / item_count：渲染项列表（可为空，此时只清屏）；clear_color：清屏色；
		ui_callback：每帧 UI 绘制回调（可为空；在场景 pass 结束后交还上层绘制 UI）；
		readback：像素回读请求（可为空；非空且 pixels 非空时把本帧颜色附件拷回该缓冲）。
		返回假视为硬错误。
		*/
		bool render(Swapchain_Context& swapchain, const Frame_Render_Item* items, uint32_t item_count,
			bool& rebuild_needed_out, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr,
			Frame_Readback_Request* readback = nullptr,
			const Frame_Post_Scene_Callback* post_scene = nullptr);
		/*
		渲染一帧到离屏目标（不取交换链图像、不呈现）
		帧槽等待 → 重置命令池 → 录制（目标附件 ＋ 场景 pass ＋ 可选像素回读）→ 仅以栅栏提交 → 回读；
		与 render 共用同一批帧槽与帧计数轮转，故两者可交替调用而不打乱在途帧同步。
		目标颜色在帧间恒处于 SHADER_READ_ONLY_OPTIMAL，故离屏结果可直接作纹理采样。
		返回假视为硬错误。
		*/
		bool render_to_target(const Frame_Render_Target& target, const Frame_Render_Item* items,
			uint32_t item_count, const float clear_color[4], const Frame_Ui_Callback* ui_callback = nullptr,
			Frame_Readback_Request* readback = nullptr, bool shadow_pass = false,
			const Frame_Post_Scene_Callback* post_scene = nullptr,
			VkPipeline tone_id_pipeline = VK_NULL_HANDLE);
		/*
		批量渲染到多个离屏目标（第六百六十九轮；C2 单次提交）
		一次帧槽准备（栅栏等待 / 时间戳读回 / 栅栏重置 / 命令池重置）后，为每个 pass 录制一条主命令缓冲
		（同一帧槽命令池；首段复用槽主缓冲，其余按需分配），最后用一次 vkQueueSubmit2 提交全部命令缓冲。
		pass_count 段逐段目标 / 矩阵 / 清屏标志；后续段开头插入颜色 / 深度内存屏障；时间戳仅首段重置 ＋ 写起点、末段写终点。
		仅服务于阴影路径（shadow_pass = true）：不支持回读、UI 与后处理回调；items 逐段写入 push_constants，
		录制结束后保持最后一段的矩阵。任一段录制失败返回假，调用方应回落逐 pass 旧路径。
		*/
		bool render_to_targets_batch(const Frame_Render_Target* targets, const float* const* pass_matrices,
			uint32_t pass_count, Frame_Render_Item* items, uint32_t item_count,
			const float clear_color[4], const bool* clear_flags, bool shadow_pass);
		/*
		渲染一帧：先把场景渲染进离屏目标，再在同一命令缓冲内以该目标为纹理做一次全屏后处理 pass 呈现到交换链
		（同帧多 pass；与 render_to_target 的跨帧闭环不同，本路径在同一帧内完成「目标 → 后处理 → 呈现」）。
		★ 第十轮收口：本函数是 render_chain_present 的**单级特例薄封装**（后处理级数 = 1、无第二目标），
		  两条路径共用同一套录制 / 提交 / 统计实现（不再各自维护一份）。
		target 为离屏目标附件；items / item_count 为**第一个 pass（场景）**的渲染项；
		post_item 为**第二个 pass（后处理）**的单个绘制项（管线 / 布局 / 描述符集 / 全屏几何 / 实例缓冲）；
		readback 非空时回读的是**最终呈现到交换链**的颜色附件（即后处理输出）。
		要求目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源），不一致按硬错误拒绝。
		返回假视为硬错误。
		*/
		bool render_to_target_present(Swapchain_Context& swapchain, const Frame_Render_Target& target,
			const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item& post_item,
			const float clear_color[4], bool& rebuild_needed_out,
			Frame_Readback_Request* readback = nullptr, const Frame_Ui_Callback* ui_callback = nullptr);
		/*
		渲染一帧：场景渲染进离屏目标后，在同一命令缓冲内顺序执行 N 级后处理并呈现到交换链
		targets / target_count 为中间目标组：targets[0] 为场景目标，其余为额外中间目标（2..4 个）；
		第 i 级的源 = targets[i % target_count]、非末级宿 = targets[(i + 1) % target_count]，末级恒呈现到交换链；
		故 post_count ≥ 2 时须 target_count ≥ 2（单目标无法做 ping-pong）。
		中间目标在帧间恒保持 SHADER_READ_ONLY_OPTIMAL（每级写完后转回）。
		post_items 为各级后处理绘制项（其 sampler_set 须分别绑定对应源的采样集）；
		readback 非空时回读的是**最终呈现到交换链**的颜色附件。
		要求**每个**目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源），不一致按硬错误拒绝。
		返回假视为硬错误。
		*/
		bool render_chain_present(Swapchain_Context& swapchain, const Frame_Render_Target* targets,
			uint32_t target_count, const Frame_Render_Item* items, uint32_t item_count,
			const Frame_Render_Item* post_items, uint32_t post_count, const float clear_color[4],
			bool& rebuild_needed_out, Frame_Readback_Request* readback = nullptr,
			const Frame_Ui_Callback* ui_callback = nullptr,
			VkPipeline tone_id_pipeline = VK_NULL_HANDLE);
		/*
		实际绘制量（第十一轮新增）
		区分「登记量」（上层登记的渲染项数）与「实际绘制量」（真正下达绘制命令的量）：
		items_record 会跳过不完整项（管线 / 缓冲缺失、索引数或实例数为 0），故实际量可能小于登记量。
		*/
