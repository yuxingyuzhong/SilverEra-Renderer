		struct Recorded_Counts
		{
			//实际绘制的项数
			uint32_t items = 0;
			//实际绘制的实例数
			uint64_t instances = 0;
		};
		/*
		设置间接录制上下文（第二百五十二轮）
		mapped：间接命令缓冲的持久映射指针（每条命令 20 字节）；buffer：提交给 vkCmdDrawIndexedIndirect 的缓冲；
		capacity：可容纳的命令条数；instance_stride：实例缓冲步长（字节，用于实例区连续性判定）。
		mapped 为空或 capacity ≤ 1 时关闭间接路径（回落逐项直接绘制，行为与关闭前完全一致）。
		*/
		void indirect_context_set(void* mapped, VkBuffer buffer, uint32_t capacity, uint32_t instance_stride);
		//本帧已写入的间接命令数（0 = 本帧未走间接路径）
		uint32_t indirect_command_count_get(void) const;
		//累计间接绘制调用数（自构建以来；单线程录制且启用间接时递增）
		uint64_t indirect_draw_count_get(void) const;
		/*
		写入指定帧槽的间接命令缓冲（第二百五十二轮；门面在初始化时调用）
		buffer 为空或 capacity ≤ 1 视为该槽不启用间接路径；mapped 为持久映射指针。
		*/
		bool slot_indirect_buffer_set(uint32_t slot_index, VkBuffer buffer, void* mapped, uint32_t capacity);

		//已推进帧数
		uint32_t frame_count_get(void) const;
		/*
		本次 render 将要使用的帧槽序号（frame_count % frame_overlap）
		供门面在调用 render 前取用「同一槽」的场景 UBO / 描述符集，保证与帧内部一致。
		*/
		uint32_t slot_index_get(void) const;
		/*
		等待本帧槽的完成栅栏
		供门面在写入「本帧槽」的资源（如场景 UBO）之前调用，避免覆盖 GPU 仍在读取的上一轮内容；
		render 内部还会再等一次，届时栅栏已发出，vkWaitForFences 立即返回，无副作用。
		返回假视为硬错误（等待失败或帧上下文未构建）。
		*/
		bool slot_wait(void);
		/*
		等待全部帧槽的完成栅栏
		供门面在「写满全部帧槽描述符集」或「重写共享采样器 / 重建共享采样器」之前调用，
		避免在命令缓冲仍在途时更新（或销毁）描述符集所引用的资源。
		*/
		bool frames_wait(void);
		//在途帧数量（= 帧槽数；供上层决定延迟销毁的释放帧号）
		uint32_t in_flight_count_get(void) const;
		/*
		帧统计（只读观测值；供门面 frame_stats_get 取用）
		last_frame_ms 为最近一帧的主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用，不含 GPU 时间戳）；
		total_draw_items / total_instances 为自构建以来的累计绘制项数与实例数。
		*/
		double last_frame_ms_get(void) const;
		/*
		最近一帧主机侧分段耗时（毫秒；第一百三十三轮）
		录制 = 命令录制调用；提交 = vkQueueSubmit2；呈现 = vkQueuePresentKHR（离屏路径恒 0）。
		三者之和不超过 last_frame_ms。
		*/
		double last_host_record_ms_get(void) const;
		double last_host_submit_ms_get(void) const;
		double last_host_present_ms_get(void) const;
		//累计主机侧耗时（毫秒；自构建以来各帧「录制 ＋ 提交 ＋ 呈现调用」之和）
		double total_frame_ms_get(void) const;
		/*
		累计主机侧分段耗时（毫秒；第一百三十四轮）
		录制 / 提交 / 呈现三段随每帧累加，与 total_frame_ms 同一口径；无边框的呈现段在离屏帧中不增长。
		*/
		double total_host_record_ms_get(void) const;
		double total_host_submit_ms_get(void) const;
		double total_host_present_ms_get(void) const;
		/*
		清零累计统计（第九十一轮）
		frames 以外的累计量（绘制项 / 实例 / 实际绘制 / 累计耗时 / GPU 样本）全部归零；
		最近一帧的即时量（last_frame_ms / last_gpu_frame_ms / last_post_stages）保持不变。
		*/
		void totals_reset(void);
		uint64_t total_draw_items_get(void) const;
		uint64_t total_instances_get(void) const;
		//累计实际绘制项数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_items_get(void) const;
		//累计实际绘制实例数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_instances_get(void) const;
		/*
		GPU 时间戳能力（设备与图形队列族均支持时为真；为假时 GPU 耗时字段恒 0 且不报错）
		供门面 gpu_timestamp_supported 取用。
		*/
		bool gpu_timestamp_supported_get(void) const;
		//最近一帧 GPU 侧耗时（毫秒；无能力或尚未读回时为 0）
		double last_gpu_frame_ms_get(void) const;
		//最近一帧场景 pass 的 GPU 耗时（毫秒；第一百二十八轮）
		double last_gpu_scene_ms_get(void) const;
		//最近一帧后处理各级合计的 GPU 耗时（毫秒；无后处理时为 0）
		double last_gpu_post_ms_get(void) const;
		//最近一次阴影 pass 的 GPU 耗时（毫秒；未启用阴影或能力不可用时为 0）
		double last_gpu_shadow_ms_get(void) const;
		/*
		读回最近一次阴影 pass 的 GPU 耗时（毫秒；第一百二十八轮）
		须在阴影 pass 提交且等尽在途帧之后调用；无能力 / 未写过 / 结果不可用时返回 0 且不改动已有值。
		*/
		double shadow_gpu_ms_read(void);
		//累计 GPU 侧耗时（毫秒；自构建以来各次成功读回的时间戳之和；无能力时恒 0）
		double total_gpu_frame_ms_get(void) const;
		/*
		累计分段 GPU 耗时（第一百三十一轮）
		场景 / 后处理随主查询池每次成功读回累加；阴影每完成一次读回累加一次（按需重绘时才会读回）。
		无能力时各项恒 0。
		*/
		double total_gpu_scene_ms_get(void) const;
		double total_gpu_post_ms_get(void) const;
		//累计阴影 pass 的 GPU 耗时（毫秒）
		double total_gpu_shadow_ms_get(void) const;
		//已读回的阴影 GPU 样本数（每完成一次阴影读回递增；自构建以来累计）
		uint64_t gpu_shadow_sample_count_get(void) const;
		//已读回 GPU 时间戳的样本数（无能力 / 该帧槽首轮未写时间戳时不增；自构建以来累计）
		uint64_t gpu_sample_count_get(void) const;
		//最近一帧实际执行的后处理级数（0 = 未走后处理链；第二十二轮新增）
		uint32_t last_post_stages_get(void) const;
		//最近一帧并行录制的段数（第一百九十八轮；0 = 单线程录制）
		uint32_t last_parallel_segments_get(void) const;
		//最近一帧实际使用的采样数（第一百零一轮；1 = 单采样）
		uint32_t last_sample_count_get(void) const;
		/*
		压入延迟销毁条目（指示「release_frame 帧及以后」可释放）
		条目中的旧资源（旧交换链 / 图像视图 / 两类信号量 / 深度图像与内存与视图）由帧模块在到期时释放；
		由交换链重建路径在移交旧资源后调用，延迟通常取「在途帧数 + 1」。
		*/
		void deferred_push(Deferred_Entry entry, uint32_t delay_frames);
