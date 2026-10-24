#include "src/render/RenderTarget/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像 / 视图创建与销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构
	Render_Target_Context::~Render_Target_Context(void)
	{
		//释放全部渲染目标与一次性资源
		destroy();
	}

	//构建（记录逻辑设备 / 图形队列与内存模块）
	bool Render_Target_Context::build(VkDevice device, uint32_t graphics_family, VkQueue graphics_queue, Memory_Context& memory)
	{
		//前置：设备 / 队列 / 内存模块必须齐备
		if (device == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE || !memory.valid())
		{
			logger.error("渲染目标:: 构建前置不完整（设备 / 队列 / 内存模块）");
			return false;
		}
		//记录句柄与内存模块
		this->device = device;
		this->graphics_family = graphics_family;
		this->graphics_queue = graphics_queue;
		this->memory = &memory;
		logger.info("渲染目标:: 上下文构建完成");
		return true;
	}

	//是否已构建完成
	bool Render_Target_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && memory != nullptr;
	}

	//确保一次性命令池与栅栏就绪（首次创建，之后复用）
	bool Render_Target_Context::transient_resources_ensure(void)
	{
		//均已就绪则直接复用
		if (transient_pool != VK_NULL_HANDLE && transient_fence != VK_NULL_HANDLE)
			return true;

		//创建一次性命令池（TRANSIENT，跨多次转换经 vkResetCommandPool 复用）
		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = graphics_family;
		//若命令池创建失败
		if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &transient_pool), "渲染目标:: 创建一次性命令池"))
		{
			transient_pool = VK_NULL_HANDLE;
			return false;
		}

		//创建完成栅栏（未触发态，供等待本次提交用）
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		//若栅栏创建失败
		if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &transient_fence), "渲染目标:: 创建一次性栅栏"))
		{
			vkDestroyCommandPool(device, transient_pool, nullptr);
			transient_pool = VK_NULL_HANDLE;
			transient_fence = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	/*
	一次性布局转换
	颜色：UNDEFINED → SHADER_READ_ONLY_OPTIMAL（dstStage = FRAGMENT_SHADER、dstAccess = SHADER_READ）；
	深度：UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL（dstStage = EARLY|LATE_FRAGMENT_TESTS、dstAccess = 深度写入）。
	两条屏障共用 srcStage = TOP_OF_PIPE、dstStage 取两者并集，一次提交完成。
	*/
	bool Render_Target_Context::target_transition(VkImage color_image, VkImage depth_image)
	{
		//重置命令池（连带回收上次分配的命令缓冲）
		if (!vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置转换命令池"))
			return false;

		//分配命令缓冲
		VkCommandBufferAllocateInfo buffer_alloc{};
		buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		buffer_alloc.commandPool = transient_pool;
		buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		buffer_alloc.commandBufferCount = 1;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		//若分配失败
		if (!vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配转换命令缓冲"))
			return false;

		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始转换录制");

		//颜色屏障（未定义 → 着色器可读）
		VkImageMemoryBarrier color_barrier{};
		color_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		color_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		color_barrier.image = color_image;
		color_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		color_barrier.srcAccessMask = 0;
		color_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		//深度屏障（未定义 → 深度附件）
		VkImageMemoryBarrier depth_barrier{};
		depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depth_barrier.image = depth_image;
		depth_barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		depth_barrier.srcAccessMask = 0;
		depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		//一次提交两条屏障（srcStage = 顶部管线；dstStage 取两者并集）
		VkImageMemoryBarrier barriers[2] = { color_barrier, depth_barrier };
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, 0, nullptr, 0, nullptr, 2, barriers);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束转换录制");

		//复位完成栅栏后再提交（上次等待完成后已发出）
		if (!vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置转换栅栏"))
			return false;

		//提交到图形队列（同步完成，故不设信号量）
		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer;
		//若提交成功则等待其完成
		if (!vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交转换命令"))
			return false;
		return vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待转换完成");
	}

	//创建渲染目标
	bool Render_Target_Context::target_create(uint64_t& handle_out, uint32_t width, uint32_t height,
		VkFormat color_format, VkFormat depth_format)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：上下文与参数校验
		if (!valid() || width == 0 || height == 0
			|| color_format == VK_FORMAT_UNDEFINED || depth_format == VK_FORMAT_UNDEFINED)
		{
			logger.error("渲染目标:: 创建参数不完整（{}×{}，颜色 {}，深度 {}）", width, height,
				static_cast<int32_t>(color_format), static_cast<int32_t>(depth_format));
			return false;
		}
		//确保一次性转换资源就绪
		if (!transient_resources_ensure())
			return false;

		//待落地记录（全部创建成功且转换完成才登记）
		Render_Target_Record record{};
		record.extent = { width, height };
		//创建颜色图像（颜色附件 ＋ 采样 ＋ 传输源）
		if (!memory->image_create(record.color_image, record.color_alloc, width, height, color_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
		{
			logger.error("渲染目标:: 颜色图像创建失败");
			return false;
		}
		//创建颜色图像视图
		if (!memory->image_view_create(record.color_view, record.color_image, color_format, VK_IMAGE_ASPECT_COLOR_BIT))
		{
			logger.error("渲染目标:: 颜色图像视图创建失败");
			record_resources_destroy(record);
			return false;
		}
		//创建深度图像（深度附件 ＋ 传输源；第二十八轮增加传输源以支持深度读回）
		if (!memory->image_create(record.depth_image, record.depth_alloc, width, height, depth_format,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
		{
			logger.error("渲染目标:: 深度图像创建失败");
			record_resources_destroy(record);
			return false;
		}
		//创建深度图像视图
		if (!memory->image_view_create(record.depth_view, record.depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
		{
			logger.error("渲染目标:: 深度图像视图创建失败");
			record_resources_destroy(record);
			return false;
		}
		//一次性布局转换（颜色 → 着色器可读；深度 → 深度附件）
		if (!target_transition(record.color_image, record.depth_image))
		{
			logger.error("渲染目标:: 初始布局转换失败");
			record_resources_destroy(record);
			return false;
		}
		//标记记录有效
		record.valid = true;

		//分配句柄：优先复用第一个空槽，否则追加
		uint64_t handle = 0;
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			//若该槽已空
			if (!records[index].valid)
			{
				records[index] = record;
				handle = static_cast<uint64_t>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			records.push_back(record);
			handle = static_cast<uint64_t>(records.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染目标:: 已创建（句柄 {}，{}×{}）", handle, width, height);
		return true;
	}

	//归还单个记录的图像与视图（颜色与深度各自先视图后图像）
	void Render_Target_Context::record_resources_destroy(Render_Target_Record& record)
	{
		//内存模块就绪时逐个归还
		if (memory != nullptr)
		{
			//深度视图 / 深度图像
			if (record.depth_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.depth_view);
			if (record.depth_image != VK_NULL_HANDLE)
				memory->image_destroy(record.depth_image, record.depth_alloc);
			//颜色视图 / 颜色图像
			if (record.color_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.color_view);
			if (record.color_image != VK_NULL_HANDLE)
				memory->image_destroy(record.color_image, record.color_alloc);
		}
		//复位句柄（保留 extent 由调用方决定是否清空）
		record.color_image = VK_NULL_HANDLE;
		record.color_alloc = VK_NULL_HANDLE;
		record.color_view = VK_NULL_HANDLE;
		record.depth_image = VK_NULL_HANDLE;
		record.depth_alloc = VK_NULL_HANDLE;
		record.depth_view = VK_NULL_HANDLE;
	}

	//销毁渲染目标
	void Render_Target_Context::target_destroy(uint64_t handle)
	{
		//句柄无效则忽略
		if (handle == 0 || handle > records.size())
			return;
		//取目标记录
		Render_Target_Record& record = records[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//归还图像与视图
		record_resources_destroy(record);
		//整条记录复位（标记槽为空）
		record = Render_Target_Record{};
		logger.info("渲染目标:: 已销毁（句柄 {}）", handle);
	}

	//分离渲染目标资源（摘出记录不销毁，归还责任移交调用方）
	bool Render_Target_Context::target_detach(uint64_t handle, Render_Target_Record& record_out)
	{
		//默认无资源摘出
		record_out = Render_Target_Record{};
		//句柄为零或越界即视为无需分离
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出被摘出的记录（阻塞颜色 / 深度图像与视图）
		record_out = record;
		//整条记录复位（标记槽为空；归还责任已移交调用方）
		record = Render_Target_Record{};
		return true;
	}

	//取渲染目标各附件与范围
	bool Render_Target_Context::target_get(uint64_t handle, VkImage& color_image_out, VkImageView& color_view_out,
		VkImageView& depth_view_out, VkExtent2D& extent_out) const
	{
		//出参先复位
		color_image_out = VK_NULL_HANDLE;
		color_view_out = VK_NULL_HANDLE;
		depth_view_out = VK_NULL_HANDLE;
		extent_out = { 0, 0 };
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出各附件与范围
		color_image_out = record.color_image;
		color_view_out = record.color_view;
		depth_view_out = record.depth_view;
		extent_out = record.extent;
		return true;
	}

	//取渲染目标尺寸（只读；句柄无效返回假且出参归零）
	bool Render_Target_Context::target_extent_get(uint64_t handle, VkExtent2D& extent_out) const
	{
		//出参先复位
		extent_out = { 0, 0 };
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出范围
		extent_out = record.extent;
		return true;
	}

	//句柄是否有效
	bool Render_Target_Context::target_valid(uint64_t handle) const
	{
		//越界或空槽均视为无效
		if (handle == 0 || handle > records.size())
			return false;
		return records[handle - 1].valid;
	}

	//读回整幅颜色图像
	bool Render_Target_Context::target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//先取范围，整幅即「原点 ＋ 完整尺寸」
		if (handle == 0 || handle > records.size())
			return false;
		//若目标已空
		if (!records[handle - 1].valid)
			return false;
		//转交实现（整幅）
		return target_readback_impl(handle, 0, 0, records[handle - 1].extent.width,
			records[handle - 1].extent.height, pixels, capacity, width_out, height_out);
	}

	//读回指定子矩形
	bool Render_Target_Context::target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 子区域尺寸校验
		if (handle == 0 || handle > records.size() || region_width == 0 || region_height == 0)
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则拒绝
		if (!record.valid)
			return false;
		//子区域须完整落在目标范围内（含右 / 下边界）
		if (x >= record.extent.width || y >= record.extent.height
			|| region_width > record.extent.width - x || region_height > record.extent.height - y)
		{
			logger.error("渲染目标:: 读回子区域越界（{}×{} 目标，区域 {}×{} @ {},{}）",
				record.extent.width, record.extent.height, region_width, region_height, x, y);
			return false;
		}
		//转交实现（子区域）
		return target_readback_impl(handle, x, y, region_width, region_height, pixels, capacity,
			width_out, height_out);
	}

	/*
	读回深度图像（32 位原始值；第二十八轮新增）
	流程：DEPTH_ATTACHMENT → TRANSFER_SRC（DEPTH 方面）→ 拷贝到主机缓冲 → 转回 DEPTH_ATTACHMENT。
	调用方须已保证无在途帧引用该目标（门面先 frames_wait）。
	*/
	bool Render_Target_Context::target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 出参校验
		if (!valid() || values == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空或无深度图像则拒绝
		if (!record.valid || record.depth_image == VK_NULL_HANDLE)
			return false;
		//容量（元素个数）校验
		VkDeviceSize value_count = static_cast<VkDeviceSize>(record.extent.width) * record.extent.height;
		if (value_count > capacity)
		{
			logger.error("渲染目标:: 深度读回容量不足（需要 {} 个元素，给出 {}）", value_count, capacity);
			return false;
		}
		//字节数（每像素 32 位）
		VkDeviceSize byte_size = value_count * 4;
		//确保一次性命令资源就绪
		if (!transient_resources_ensure())
			return false;

		//主机可见暂存缓冲
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!memory->buffer_create(staging, staging_alloc, byte_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
		{
			logger.error("渲染目标:: 深度读回暂存缓冲创建失败");
			return false;
		}

		//重置命令池并分配命令缓冲
		bool recorded = vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置深度读回命令池");
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (recorded)
		{
			VkCommandBufferAllocateInfo buffer_alloc{};
			buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc.commandPool = transient_pool;
			buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc.commandBufferCount = 1;
			recorded = vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配深度读回命令缓冲");
		}
		//开始录制
		if (recorded)
		{
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			recorded = vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始深度读回录制");
		}

		if (recorded)
		{
			//深度附件 → 传输源
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = record.depth_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//深度图像 → 暂存缓冲（整幅、紧排）
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { record.extent.width, record.extent.height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, record.depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

			//传输源 → 深度附件（恢复帧间不变量）
			VkImageMemoryBarrier to_depth = to_transfer;
			to_depth.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			to_depth.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			to_depth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &to_depth);

			//结束录制
			recorded = vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束深度读回录制");
		}

		//提交并等待
		if (recorded)
			recorded = vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置深度读回栅栏");
		if (recorded)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			recorded = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交深度读回命令");
		}
		if (recorded)
			recorded = vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待深度读回完成");
		//读出原始值
		if (recorded)
			recorded = memory->buffer_read(staging, staging_alloc, values, byte_size, 0);
		//归还暂存缓冲
		memory->buffer_destroy(staging, staging_alloc);

		//写出尺寸
		if (recorded)
		{
			if (width_out != nullptr)
				*width_out = record.extent.width;
			if (height_out != nullptr)
				*height_out = record.extent.height;
		}
		return recorded;
	}

	/*
	读回实现（整幅 / 子区域共用；区域已由调用方校验）
	流程：主色 SHADER_READ_ONLY → TRANSFER_SRC → 拷贝到主机可见暂存缓冲 → 转回 SHADER_READ_ONLY
	     → 等栅栏 → 从暂存缓冲读出像素 → 销毁暂存缓冲。
	同步：调用方须已保证无在途帧引用该目标（门面先 frames_wait），本函数只用一次性命令池同步提交。
	*/
	bool Render_Target_Context::target_readback_impl(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 / 像素出参 / 容量校验
		if (!valid() || pixels == nullptr || handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则拒绝
		if (!record.valid)
			return false;
		//子区域字节数（RGBA8 紧排）
		VkDeviceSize byte_size = static_cast<VkDeviceSize>(region_width) * region_height * 4;
		//若容量不足
		if (byte_size > capacity)
		{
			logger.error("渲染目标:: 读回容量不足（需要 {} 字节，给出 {}）", byte_size, capacity);
			return false;
		}
		//确保一次性命令资源就绪
		if (!transient_resources_ensure())
			return false;

		//创建主机可见暂存缓冲（传输目标用途）
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = VK_NULL_HANDLE;
		if (!memory->buffer_create(staging, staging_alloc, byte_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
		{
			logger.error("渲染目标:: 读回暂存缓冲创建失败");
			return false;
		}

		//重置命令池并分配命令缓冲
		bool recorded = vk_result_check(vkResetCommandPool(device, transient_pool, 0), "渲染目标:: 重置读回命令池");
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (recorded)
		{
			VkCommandBufferAllocateInfo buffer_alloc{};
			buffer_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			buffer_alloc.commandPool = transient_pool;
			buffer_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			buffer_alloc.commandBufferCount = 1;
			recorded = vk_result_check(vkAllocateCommandBuffers(device, &buffer_alloc, &command_buffer), "渲染目标:: 分配读回命令缓冲");
		}
		//开始录制
		if (recorded)
		{
			VkCommandBufferBeginInfo begin_info{};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			recorded = vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "渲染目标:: 开始读回录制");
		}

		if (recorded)
		{
			//进入传输源布局（着色器读 → 传输读）
			VkImageMemoryBarrier to_transfer{};
			to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			to_transfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			to_transfer.image = record.color_image;
			to_transfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			to_transfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);

			//拷贝指定子区域到暂存缓冲（紧排、无行填充）
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
			region.imageExtent = { region_width, region_height, 1 };
			vkCmdCopyImageToBuffer(command_buffer, record.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

			//转回着色器可读（后续帧继续采样该目标）
			VkImageMemoryBarrier to_sample = to_transfer;
			to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			to_sample.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_sample);

			//结束录制
			recorded = vk_result_check(vkEndCommandBuffer(command_buffer), "渲染目标:: 结束读回录制");
		}

		//提交并等待（同步完成，不依赖帧模块）
		if (recorded)
		{
			recorded = vk_result_check(vkResetFences(device, 1, &transient_fence), "渲染目标:: 重置读回栅栏");
		}
		if (recorded)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			recorded = vk_result_check(vkQueueSubmit(graphics_queue, 1, &submit_info, transient_fence), "渲染目标:: 提交读回命令");
		}
		if (recorded)
		{
			recorded = vk_result_check(vkWaitForFences(device, 1, &transient_fence, VK_TRUE, UINT64_MAX), "渲染目标:: 等待读回完成");
		}

		//从暂存缓冲读出像素
		if (recorded)
		{
			recorded = memory->buffer_read(staging, staging_alloc, pixels, byte_size, 0);
		}
		//归还暂存缓冲
		memory->buffer_destroy(staging, staging_alloc);

		//成功后写出子区域尺寸
		if (recorded)
		{
			if (width_out != nullptr)
				*width_out = region_width;
			if (height_out != nullptr)
				*height_out = region_height;
		}
		return recorded;
	}

	//销毁（幂等）
	void Render_Target_Context::destroy(void)
	{
		//逐条归还渲染目标
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			//跳过空槽
			if (!records[index].valid)
				continue;
			record_resources_destroy(records[index]);
			records[index] = Render_Target_Record{};
		}
		records.clear();

		//归还一次性资源
		if (device != VK_NULL_HANDLE)
		{
			//先栅栏后命令池
			if (transient_fence != VK_NULL_HANDLE)
				vkDestroyFence(device, transient_fence, nullptr);
			if (transient_pool != VK_NULL_HANDLE)
				vkDestroyCommandPool(device, transient_pool, nullptr);
		}
		transient_fence = VK_NULL_HANDLE;
		transient_pool = VK_NULL_HANDLE;
		memory = nullptr;
		graphics_queue = VK_NULL_HANDLE;
		device = VK_NULL_HANDLE;
		graphics_family = 0;
	}
}
