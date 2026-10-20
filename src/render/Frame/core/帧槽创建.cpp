#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（构建前置校验需要其 valid()）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构
	Frame_Context::~Frame_Context(void)
	{
		//释放已创建的帧槽资源
		destroy();
	}

	//构建
	bool Frame_Context::build(VkDevice device, VkPhysicalDevice physical_device,
		uint32_t graphics_family, VkQueue graphics_queue,
		uint32_t present_family, VkQueue present_queue,
		Swapchain_Context& swapchain, Memory_Context& memory, uint32_t frame_overlap)
	{
		//前置：设备句柄与两队列必须齐备
		if (device == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE || present_queue == VK_NULL_HANDLE)
		{
			logger.error("帧上下文:: 构建设备句柄或队列不完整");
			return false;
		}
		//前置：交换链必须已构建（帧链路取图像依赖它）
		if (!swapchain.valid())
		{
			logger.error("帧上下文:: 交换链尚未构建，无法建立帧上下文");
			return false;
		}
		//前置：内存模块必须已构建（像素回读缓冲依赖它）
		if (!memory.valid())
		{
			logger.error("帧上下文:: 内存上下文尚未构建，无法建立帧上下文");
			return false;
		}
		//前置：在途帧数为正
		if (frame_overlap == 0)
		{
			logger.error("帧上下文:: 在途帧数必须为正");
			return false;
		}

		//记录设备与队列
		this->device = device;
		this->physical_device = physical_device;
		this->graphics_family = graphics_family;
		this->graphics_queue = graphics_queue;
		this->present_family = present_family;
		this->present_queue = present_queue;
		this->frame_overlap = frame_overlap;
		//记录内存模块（像素回读缓冲的创建 / 销毁依赖它）
		this->memory = &memory;

		/*
		探测 GPU 时间戳能力
		三项同时满足才启用：设备支持图形与计算队列时间戳、时间戳周期有效、图形队列族有效位非零；
		任一项不满足即降级（不创建查询池，GPU 耗时字段恒 0），既不影响构建也不报错。
		*/
		timestamp_supported = false;
		timestamp_period = 0.0f;
		timestamp_valid_bits = 0;
		//若物理设备句柄可用则查询其属性与队列族属性
		if (physical_device != VK_NULL_HANDLE)
		{
			//设备属性（取时间戳周期与图形 / 计算时间戳支持标志）
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(physical_device, &properties);
			//队列族属性（取图形队列族的时间戳有效位）
			uint32_t family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
			std::vector<VkQueueFamilyProperties> families(family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
			uint32_t valid_bits = graphics_family < families.size() ? families[graphics_family].timestampValidBits : 0;
			//记录探测结果
			timestamp_period = properties.limits.timestampPeriod;
			timestamp_valid_bits = valid_bits;
			timestamp_supported = properties.limits.timestampComputeAndGraphics == VK_TRUE
				&& timestamp_period > 0.0f && valid_bits > 0;
		}
		//不支持时记一条降级日志（便于定位「GPU 耗时恒 0」）
		if (!timestamp_supported)
			logger.warn("帧槽创建:: 设备或图形队列族不支持 GPU 时间戳，GPU 耗时统计降级为 0");
		else
			logger.info("帧槽创建:: GPU 时间戳可用（周期 {} 纳秒 / tick，有效位 {}）", timestamp_period, timestamp_valid_bits);

		//创建帧槽集合
		slots.resize(frame_overlap);
		for (uint32_t index = 0; index < frame_overlap; ++index)
			//若任一帧槽创建失败
			if (!frame_slot_create(slots[index]))
			{
				logger.error("帧上下文:: 第 {} 个帧槽创建失败，回退", index);
				//逆序回收已建帧槽
				destroy();
				return false;
			}

		logger.info("帧上下文:: 帧基础设施构建完成（在途帧 {}，交换链图像 {}，图像可用信号量 {}，呈现完成信号量 {}）",
			frame_overlap, swapchain.image_count_get(), swapchain.image_available_count_get(),
			swapchain.render_finished_count_get());
		return true;
	}

	//销毁
	void Frame_Context::destroy(void)
	{
		//若逻辑设备已记录（销毁其下的帧槽对象需要它）
		if (device != VK_NULL_HANDLE)
		{
			//先等设备空闲，避免销毁仍在使用的对象（§5.4）
			vkDeviceWaitIdle(device);
			//逆序销毁全部帧槽
			for (auto& slot : slots)
				frame_slot_destroy(slot);
			//归还像素回读缓冲（须在内存模块销毁之前）
			readback_buffer_destroy();
			//立即释放残留的延迟销毁条目（须在内存模块销毁之前；此时已等设备空闲）
			deferred_release_all();
		}
		//清空帧槽与延迟销毁队列
		slots.clear();
		deferred_entries.clear();

		//复位记录
		device = VK_NULL_HANDLE;
		physical_device = VK_NULL_HANDLE;
		graphics_queue = VK_NULL_HANDLE;
		present_queue = VK_NULL_HANDLE;
		graphics_family = 0;
		present_family = 0;
		frame_overlap = 0;
		acquire_cursor = 0;
		frame_count = 0;
		last_frame_ms = 0.0;
		total_frame_ms = 0.0;
		total_draw_items = 0;
		total_instances = 0;
		total_drawn_items = 0;
		total_drawn_instances = 0;
		frame_recorded = Recorded_Counts{};
		timestamp_supported = false;
		timestamp_period = 0.0f;
		timestamp_valid_bits = 0;
		last_gpu_frame_ms = 0.0;
		total_gpu_frame_ms = 0.0;
		gpu_sample_count = 0;
		//解除对内存模块的引用
		memory = nullptr;
	}

	//是否已构建完成
	bool Frame_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && !slots.empty();
	}

	//已推进帧数
	uint32_t Frame_Context::frame_count_get(void) const
	{
		return frame_count;
	}

	//本次 render 将要使用的帧槽序号
	uint32_t Frame_Context::slot_index_get(void) const
	{
		//帧槽未构建时返回 0（规避除零）
		if (frame_overlap == 0)
			return 0;
		return frame_count % frame_overlap;
	}

	//在途帧数量
	uint32_t Frame_Context::in_flight_count_get(void) const
	{
		return frame_overlap;
	}

	//最近一帧主机侧耗时（毫秒）
	double Frame_Context::last_frame_ms_get(void) const
	{
		return last_frame_ms;
	}

	//累计主机侧耗时（毫秒）
	double Frame_Context::total_frame_ms_get(void) const
	{
		return total_frame_ms;
	}

	//累计绘制项数
	uint64_t Frame_Context::total_draw_items_get(void) const
	{
		return total_draw_items;
	}

	//累计实际绘制项数
	uint64_t Frame_Context::total_drawn_items_get(void) const
	{
		return total_drawn_items;
	}

	//累计实际绘制实例数
	uint64_t Frame_Context::total_drawn_instances_get(void) const
	{
		return total_drawn_instances;
	}

	//累计实例数
	uint64_t Frame_Context::total_instances_get(void) const
	{
		return total_instances;
	}

	//GPU 时间戳能力
	bool Frame_Context::gpu_timestamp_supported_get(void) const
	{
		return timestamp_supported;
	}

	//最近一帧 GPU 侧耗时（毫秒）
	double Frame_Context::last_gpu_frame_ms_get(void) const
	{
		return last_gpu_frame_ms;
	}

	//累计 GPU 侧耗时（毫秒）
	double Frame_Context::total_gpu_frame_ms_get(void) const
	{
		return total_gpu_frame_ms;
	}

	//已读回 GPU 时间戳的样本数
	uint64_t Frame_Context::gpu_sample_count_get(void) const
	{
		return gpu_sample_count;
	}

	//等待某帧槽前一次提交完成（先查状态，未完成再阻塞等待）
	bool Frame_Context::slot_fence_wait(Frame_Slot& slot)
	{
		//先查该帧槽栅栏当前状态
		VkResult status = vkGetFenceStatus(device, slot.in_flight_fence);
		//若查询遭遇硬错误（既非已完成也非未就绪）
		if (status != VK_SUCCESS && status != VK_NOT_READY)
			return vk_result_check(status, "vkGetFenceStatus");
		//若已完成则免去阻塞等待（多数帧命中此快速路径）
		if (status == VK_SUCCESS)
			return true;
		//未完成则阻塞等待（超时给足，waitAll 为真；K9）
		return vk_result_check(vkWaitForFences(device, 1, &slot.in_flight_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
	}

	//等待本帧槽的完成栅栏
	bool Frame_Context::slot_wait(void)
	{
		//若尚未构建完成
		if (!valid())
		{
			logger.error("帧槽创建:: 帧上下文尚未构建，无法等待帧槽");
			return false;
		}
		//取本帧槽（与 render 内部同一槽）
		Frame_Slot& slot = slots[frame_count % frame_overlap];
		//等待该帧槽前一次提交完成
		return slot_fence_wait(slot);
	}

	//等待全部帧槽的完成栅栏
	bool Frame_Context::frames_wait(void)
	{
		//若尚未构建完成
		if (!valid())
		{
			logger.error("帧槽创建:: 帧上下文尚未构建，无法等待全部帧槽");
			return false;
		}
		//逐槽等待前一次提交完成（任一失败即返回假）
		for (Frame_Slot& slot : slots)
			if (!slot_fence_wait(slot))
				return false;
		return true;
	}

	//创建单个帧槽及其对象
	bool Frame_Context::frame_slot_create(Frame_Slot& slot)
	{
		//命令池创建信息
		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.queueFamilyIndex = graphics_family;
		//若命令池创建失败
		if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &slot.command_pool), "vkCreateCommandPool"))
			return false;

		//分配主命令缓冲（每帧重置并重新录制）
		VkCommandBufferAllocateInfo allocate_info{};
		allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate_info.commandPool = slot.command_pool;
		allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate_info.commandBufferCount = 1;
		//若命令缓冲分配失败
		if (!vk_result_check(vkAllocateCommandBuffers(device, &allocate_info, &slot.command_buffer), "vkAllocateCommandBuffers"))
		{
			frame_slot_destroy(slot);
			return false;
		}

		/*
		完成栅栏创建信息
		初始即置为「已发出」状态：首帧 vkWaitForFences 才不至于无限等待。
		*/
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		//若栅栏创建失败
		if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &slot.in_flight_fence), "vkCreateFence"))
		{
			frame_slot_destroy(slot);
			return false;
		}

		/*
		GPU 时间戳查询池（每槽两个查询：帧始 / 帧末）
		仅在能力可用时创建；创建失败即降级为「本槽无 GPU 耗时」（不使整体构建失败）。
		*/
		slot.timestamp_pool = VK_NULL_HANDLE;
		slot.timestamp_written = false;
		if (timestamp_supported)
		{
			//查询池创建信息（时间戳类型，两个查询）
			VkQueryPoolCreateInfo query_info{};
			query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
			query_info.queryCount = 2;
			//创建失败仅记日志并降级（该帧槽 GPU 耗时恒 0）
			if (vkCreateQueryPool(device, &query_info, nullptr, &slot.timestamp_pool) != VK_SUCCESS)
			{
				slot.timestamp_pool = VK_NULL_HANDLE;
				logger.warn("帧槽创建:: GPU 时间戳查询池创建失败，该帧槽 GPU 耗时降级为 0");
			}
		}

		return true;
	}

	//销毁单个帧槽及其对象
	void Frame_Context::frame_slot_destroy(Frame_Slot& slot)
	{
		//若逻辑设备已记录
		if (device != VK_NULL_HANDLE)
		{
			//销毁 GPU 时间戳查询池
			if (slot.timestamp_pool != VK_NULL_HANDLE)
				vkDestroyQueryPool(device, slot.timestamp_pool, nullptr);
			//销毁完成栅栏
			if (slot.in_flight_fence != VK_NULL_HANDLE)
				vkDestroyFence(device, slot.in_flight_fence, nullptr);
			//销毁命令池（连带回收其分配的命令缓冲）
			if (slot.command_pool != VK_NULL_HANDLE)
				vkDestroyCommandPool(device, slot.command_pool, nullptr);
		}
		//复位句柄
		slot.timestamp_pool = VK_NULL_HANDLE;
		slot.timestamp_written = false;
		slot.in_flight_fence = VK_NULL_HANDLE;
		slot.command_pool = VK_NULL_HANDLE;
		slot.command_buffer = VK_NULL_HANDLE;
	}
}
