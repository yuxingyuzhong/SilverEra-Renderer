		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//呈现队列族索引
		uint32_t present_family = 0;
		//呈现队列
		VkQueue present_queue = VK_NULL_HANDLE;
		//在途帧数
		uint32_t frame_overlap = 0;
		//帧槽集合（数量 = frame_overlap）
		std::vector<Frame_Slot> slots;
		//图像可用信号量轮转游标
		uint32_t acquire_cursor = 0;
		//已推进帧数（同时用作帧槽轮转依据与延迟销毁的释放判据）
		uint32_t frame_count = 0;
		//最近一帧主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用）
		double last_frame_ms = 0.0;
		//最近一帧主机侧分段耗时（第一百三十三轮；录制 / 提交 / 呈现）
		double last_host_record_ms = 0.0;
		double last_host_submit_ms = 0.0;
		double last_host_present_ms = 0.0;
		//累计主机侧耗时（毫秒；自构建以来）
		double total_frame_ms = 0.0;
		//累计主机侧分段耗时（第一百三十四轮）
		double total_host_record_ms = 0.0;
		double total_host_submit_ms = 0.0;
		double total_host_present_ms = 0.0;
		//累计绘制项数（自构建以来）
		uint64_t total_draw_items = 0;
		//累计实例数（按登记项计；含被跳过的不完整项）
		uint64_t total_instances = 0;
		//累计实际绘制项数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_items = 0;
		//累计实际绘制实例数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_instances = 0;
		//本帧已录制期间累计的实际绘制量（每次 render 起始处清零，录制时由 items_record 递增）
		Recorded_Counts frame_recorded{};
		/*
		并行录制上下文（第一百九十七轮；第二百零八轮起统一由 parallel_plan 填充）
		段数为 0 表示单线程录制；次级缓冲须与主缓冲同属该帧槽（由 secondary_buffers_ensure 分配）。
		*/
		Thread_Pool* thread_pool = nullptr;
		VkCommandBuffer* active_secondary = nullptr;
		uint32_t active_secondary_count = 0;
		VkFormat active_color_format = VK_FORMAT_UNDEFINED;
		VkFormat active_depth_format = VK_FORMAT_UNDEFINED;
		uint32_t active_sample_count = 1;
		//并行段统计（按段索引收集，录制结束后汇总到 frame_recorded）
		std::vector<Recorded_Counts> parallel_counts;
		/*
		批量阴影主机缓冲（第六百七十一轮；C2 收尾）
		batch_commands = 本段使用的主命令缓冲表（主缓冲 ＋ 帧槽预分配缓冲）；
		batch_submit_infos = 逐段 VkCommandBufferSubmitInfo；两者按段数 resize 后覆盖写入，避免逐帧堆分配。
		*/
		std::vector<VkCommandBuffer> batch_commands;
		std::vector<VkCommandBufferSubmitInfo> batch_submit_infos;
		//并行录制的最小绘制项数（第二百零二轮；默认 32）
		uint32_t parallel_min_items = 32;
		//并行录制的最大段数（第二百零三轮；默认 8）
		uint32_t parallel_max_segments = 8;
		//最近一帧并行录制的段数（第一百九十八轮；0 = 单线程录制）
		uint32_t last_parallel_segments = 0;
		//GPU 时间戳是否可用（设备支持图形与计算时间戳 ＋ 周期有效 ＋ 图形队列族有效位非零）
		bool timestamp_supported = false;
		//时间戳周期（纳秒 / tick；用于把 tick 差换算为时间）
		float timestamp_period = 0.0f;
		//图形队列族时间戳有效位（小于 64 时读回后须掩去高位）
		uint32_t timestamp_valid_bits = 0;
		//最近一帧 GPU 侧耗时（毫秒；无能力或尚未读回时为 0）
		double last_gpu_frame_ms = 0.0;
		//最近一帧场景 pass 耗时（毫秒；第一百二十八轮）
		double last_gpu_scene_ms = 0.0;
		//最近一帧后处理耗时（毫秒；无后处理时为 0）
		double last_gpu_post_ms = 0.0;
		//最近一次阴影 pass 耗时（毫秒）
		double last_gpu_shadow_ms = 0.0;
		//最近一次阴影 pass 使用的帧槽（供阴影时间戳读回定位）
		uint32_t last_shadow_slot = 0;
		//累计 GPU 侧耗时（毫秒；自构建以来各次成功读回之和）
		double total_gpu_frame_ms = 0.0;
		//累计分段 GPU 耗时（第一百三十一轮）
		double total_gpu_scene_ms = 0.0;
		double total_gpu_post_ms = 0.0;
		double total_gpu_shadow_ms = 0.0;
		//已读回的阴影 GPU 样本数
		uint64_t gpu_shadow_sample_count = 0;
		//已读回 GPU 时间戳的样本数（自构建以来）
		uint64_t gpu_sample_count = 0;
		//最近一帧实际执行的后处理级数（非链路径清零；第二十二轮新增）
		uint32_t last_post_stages = 0;
		//最近一帧实际使用的采样数（第一百零一轮；1 = 单采样）
		uint32_t last_sample_count = 1;
		//延迟销毁队列
		std::vector<Deferred_Entry> deferred_entries;
		//内存模块（像素回读缓冲的创建 / 销毁依赖它）
		Memory_Context* memory = nullptr;
		//像素回读缓冲（主机可见；按需创建，交换链尺寸变化时重建）
		VkBuffer readback_buffer = VK_NULL_HANDLE;
		//像素回读缓冲内存
		VmaAllocation readback_alloc = VK_NULL_HANDLE;
		//像素回读缓冲当前字节数
		VkDeviceSize readback_size = 0;
		/*
		间接录制上下文（第二百五十二轮）
		启用时为「主机可见间接命令缓冲」的持久映射指针；就近连续且状态一致的渲染项合并为一条命令，
		以 vkCmdDrawIndexedIndirect 下达；关闭、容量不足或并行分段录制时回落逐项直接绘制。
		*/
		void* indirect_mapped = nullptr;
		VkBuffer indirect_buffer = VK_NULL_HANDLE;
		uint32_t indirect_capacity = 0;
		uint32_t indirect_instance_stride = 80;
		//本帧已写入的间接命令数（每次 render 起始处清零）
		uint32_t frame_indirect_commands = 0;
		//累计间接绘制调用数（自构建以来）
		uint64_t total_indirect_draws = 0;
