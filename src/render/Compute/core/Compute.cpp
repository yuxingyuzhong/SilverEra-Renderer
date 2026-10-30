//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//本模块内部类型引入（含 Compute.h）
#include "src/render/Compute/局部命名空间使用.h"
//std::memcpy（自检回读）
#include <cstring>

//游戏引擎命名空间
namespace engine
{
	namespace
	{
		//在满足类型位的候选内存中查找第一种具备指定属性的内存类型（未找到返回 UINT32_MAX）
		uint32_t memory_type_find(VkPhysicalDevice physical_device, uint32_t type_bits, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memory_properties{};
			vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
			for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index)
			{
				bool type_supported = (type_bits & (1u << index)) != 0;
				bool property_matched = (memory_properties.memoryTypes[index].propertyFlags & properties) == properties;
				//满足类型位与属性要求即选中
				if (type_supported && property_matched)
					return index;
			}
			return UINT32_MAX;
		}
	}

	//析构（兜底销毁）
	Compute_Context::~Compute_Context(void)
	{
		destroy();
	}

	//构建（探测计算队列族并创建布局 / 池 / 描述符集 / 一次性提交资源）
	bool Compute_Context::build(VkPhysicalDevice physical_device, VkDevice device, uint32_t graphics_family, VkQueue graphics_queue,
		const Compute_Config& config)
	{
		//设备或配置非法
		if (device == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE
			|| config.frame_overlap == 0 || config.storage_buffer_count == 0 || config.push_size == 0)
			return false;
		this->physical = physical_device;
		this->device = device;
		this->family = graphics_family;
		this->queue = graphics_queue;
		this->config = config;

		//查询队列族属性
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
		//队列族索引越界
		if (family_count <= graphics_family)
		{
			supported_flag = false;
			return true;
		}
		std::vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
		/*
		队列族能力判定
		计算与图形复用同一队列族（Vulkan 1.3 桌面显卡普遍支持）；若该族不含 COMPUTE 位则整体不支持，
		由 supported() 对外报告，初始化不因此失败（见规划 §2.5）。
		*/
		if ((families[graphics_family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
		{
			supported_flag = false;
			return true;
		}
		supported_flag = true;

		//创建布局 / 池 / 描述符集 / 一次性提交资源（任一步失败即整体回滚）
		if (!descriptor_layout_create() || !pipeline_layout_create())
		{
			destroy();
			return false;
		}
		//描述符池（每帧槽 count 条存储缓冲）
		VkDescriptorPoolSize pool_size{};
		pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		pool_size.descriptorCount = config.frame_overlap * config.storage_buffer_count;
		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.maxSets = config.frame_overlap;
		pool_info.poolSizeCount = 1;
		pool_info.pPoolSizes = &pool_size;
		//创建描述符池
		if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS)
		{
			destroy();
			return false;
		}
		//按帧槽分配描述符集
		sets.resize(config.frame_overlap, VK_NULL_HANDLE);
		for (uint32_t slot = 0; slot < config.frame_overlap; ++slot)
		{
			VkDescriptorSetAllocateInfo allocate_info{};
			allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocate_info.descriptorPool = descriptor_pool;
			allocate_info.descriptorSetCount = 1;
			allocate_info.pSetLayouts = &set_layout;
			//分配失败即回滚
			if (vkAllocateDescriptorSets(device, &allocate_info, &sets[slot]) != VK_SUCCESS)
			{
				destroy();
				return false;
			}
		}
		//一次性提交命令池（瞬态）
		VkCommandPoolCreateInfo command_pool_info{};
		command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		command_pool_info.queueFamilyIndex = graphics_family;
		//创建命令池
		if (vkCreateCommandPool(device, &command_pool_info, nullptr, &single_pool) != VK_SUCCESS)
		{
			destroy();
			return false;
		}
		//一次性提交围栏（初始即信号态，避免首次等待死锁）
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		//创建围栏
		if (vkCreateFence(device, &fence_info, nullptr, &single_fence) != VK_SUCCESS)
		{
			destroy();
			return false;
		}
		active_slot = 0;
		dispatch_count = 0;
		return true;
	}

	//销毁（幂等）
	void Compute_Context::destroy(void)
	{
		//先销毁管线（依赖管线布局）
		for (Entry& entry : pipelines)
		{
			//销毁计算管线
			if (entry.pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
				vkDestroyPipeline(device, entry.pipeline, nullptr);
			entry.pipeline = VK_NULL_HANDLE;
		}
		pipelines.clear();
		//销毁描述符池（连带释放集合）
		if (descriptor_pool != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
		descriptor_pool = VK_NULL_HANDLE;
		sets.clear();
		//销毁围栏与命令池
		if (single_fence != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroyFence(device, single_fence, nullptr);
		single_fence = VK_NULL_HANDLE;
		if (single_pool != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, single_pool, nullptr);
		single_pool = VK_NULL_HANDLE;
		//销毁管线布局与描述符集布局
		if (pipeline_layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
		pipeline_layout = VK_NULL_HANDLE;
		if (set_layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
		set_layout = VK_NULL_HANDLE;
		//复位其余状态
		next_handle = 1;
		active_handle = 0;
		active_slot = 0;
		dispatch_count = 0;
		supported_flag = false;
		physical = VK_NULL_HANDLE;
		device = VK_NULL_HANDLE;
		queue = VK_NULL_HANDLE;
		family = 0;
	}

	//是否已构建完成
	bool Compute_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && set_layout != VK_NULL_HANDLE && pipeline_layout != VK_NULL_HANDLE;
	}

	//本机是否支持计算路径
	bool Compute_Context::supported(void) const
	{
		return supported_flag && valid();
	}

	//计算描述符布局创建（set 0：storage_buffer_count 条存储缓冲，可见性仅计算阶段）
	bool Compute_Context::descriptor_layout_create(void)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings(config.storage_buffer_count);
		for (uint32_t index = 0; index < config.storage_buffer_count; ++index)
		{
			bindings[index].binding = index;
			bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[index].descriptorCount = 1;
			bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			bindings[index].pImmutableSamplers = nullptr;
		}
		VkDescriptorSetLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
		layout_info.pBindings = bindings.data();
		//创建描述符集布局
		return vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout) == VK_SUCCESS;
	}

	//计算管线布局创建（推送常量 128 字节，与图形同尺寸）
	bool Compute_Context::pipeline_layout_create(void)
	{
		VkPushConstantRange push_range{};
		push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		push_range.offset = 0;
		push_range.size = config.push_size;
		VkPipelineLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = &set_layout;
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &push_range;
		//创建管线布局
		return vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) == VK_SUCCESS;
	}

	//计算管线创建（入口固定 main）
	bool Compute_Context::compute_pipeline_create(VkShaderModule module, VkPipeline& pipeline_out)
	{
		VkPipelineShaderStageCreateInfo stage_info{};
		stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage_info.module = module;
		stage_info.pName = "main";
		VkComputePipelineCreateInfo pipeline_info{};
		pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline_info.stage = stage_info;
		pipeline_info.layout = pipeline_layout;
		//创建计算管线
		return vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_out) == VK_SUCCESS;
	}

	//登记计算管线（句柄自 1 起）
	bool Compute_Context::pipeline_register(uint64_t& handle_out, VkShaderModule module)
	{
		//未构建或设备不支持
		if (!supported() || module == VK_NULL_HANDLE)
			return false;
		VkPipeline pipeline = VK_NULL_HANDLE;
		//创建失败则不登记（句柄保持 0）
		if (!compute_pipeline_create(module, pipeline))
			return false;
		Entry entry;
		entry.handle = next_handle;
		entry.pipeline = pipeline;
		//分配并写回句柄
		handle_out = next_handle;
		++next_handle;
		//默认激活首个登记的管线
		if (active_handle == 0)
			active_handle = entry.handle;
		pipelines.push_back(entry);
		return true;
	}

	//按句柄查找条目
	Compute_Context::Entry* Compute_Context::entry_find(uint64_t handle)
	{
		for (Entry& entry : pipelines)
		{
			//命中即返回
			if (entry.handle == handle)
				return &entry;
		}
		return nullptr;
	}

	//切换当前激活计算管线
	bool Compute_Context::pipeline_activate(uint64_t handle)
	{
		//句柄不存在则保持激活不变
		if (entry_find(handle) == nullptr)
			return false;
		active_handle = handle;
		return true;
	}

	//注销计算管线（销毁管线并移出条目）
	void Compute_Context::pipeline_release(uint64_t handle)
	{
		for (std::size_t index = 0; index < pipelines.size(); ++index)
		{
			//命中即销毁并移除
			if (pipelines[index].handle == handle)
			{
				if (pipelines[index].pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
					vkDestroyPipeline(device, pipelines[index].pipeline, nullptr);
				//激活句柄被注销则回落为首个剩余条目
				if (active_handle == handle)
					active_handle = pipelines.empty() ? 0 : pipelines.front().handle;
				pipelines.erase(pipelines.begin() + static_cast<std::ptrdiff_t>(index));
				return;
			}
		}
	}

	//当前激活计算管线
	VkPipeline Compute_Context::pipeline_get(void) const
	{
		for (const Entry& entry : pipelines)
		{
			//命中激活句柄
			if (entry.handle == active_handle)
				return entry.pipeline;
		}
		return VK_NULL_HANDLE;
	}

	//已登记计算管线数量
	uint32_t Compute_Context::pipeline_count(void) const
	{
		return static_cast<uint32_t>(pipelines.size());
	}

	//计算管线布局
	VkPipelineLayout Compute_Context::layout_get(void) const
	{
		return pipeline_layout;
	}

	//取第 slot 帧槽描述符集
	VkDescriptorSet Compute_Context::set_get(uint32_t slot) const
	{
		//槽越界返回空
		if (slot >= sets.size())
			return VK_NULL_HANDLE;
		return sets[slot];
	}

	//写存储缓冲到指定帧槽的指定 binding
	bool Compute_Context::storage_bind(uint32_t slot, uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
	{
		//参数越界或未构建
		if (!valid() || slot >= sets.size() || binding >= config.storage_buffer_count
			|| buffer == VK_NULL_HANDLE || size == 0)
			return false;
		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = buffer;
		buffer_info.offset = offset;
		buffer_info.range = size;
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sets[slot];
		write.dstBinding = binding;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &buffer_info;
		//一次性写入该绑定
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//指定后续调度使用的帧槽
	bool Compute_Context::slot_set(uint32_t slot)
	{
		//槽越界
		if (slot >= sets.size())
			return false;
		active_slot = slot;
		return true;
	}

	//当前帧槽
	uint32_t Compute_Context::slot_get(void) const
	{
		return active_slot;
	}

	//单次提交（一次性命令缓冲 ＋ 围栏等待，超时 5 秒）
	bool Compute_Context::single_submit(const std::function<bool(VkCommandBuffer)>& record)
	{
		//未构建或录制回调为空
		if (!valid() || !record)
			return false;
		//复位围栏与命令池（若上次提交异常残留）
		if (vkResetFences(device, 1, &single_fence) != VK_SUCCESS)
			return false;
		if (vkResetCommandPool(device, single_pool, 0) != VK_SUCCESS)
			return false;
		//分配一次性命令缓冲
		VkCommandBufferAllocateInfo allocate_info{};
		allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate_info.commandPool = single_pool;
		allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate_info.commandBufferCount = 1;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(device, &allocate_info, &command_buffer) != VK_SUCCESS)
			return false;
		//开始录制
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		bool ok = vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS;
		if (ok)
			ok = record(command_buffer);
		if (ok)
			ok = vkEndCommandBuffer(command_buffer) == VK_SUCCESS;
		//提交并等待
		if (ok)
		{
			VkSubmitInfo submit_info{};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &command_buffer;
			ok = vkQueueSubmit(queue, 1, &submit_info, single_fence) == VK_SUCCESS;
		}
		if (ok)
		{
			VkResult wait_result = vkWaitForFences(device, 1, &single_fence, VK_TRUE, 5000000000ull);
			ok = wait_result == VK_SUCCESS;
		}
		//释放命令缓冲（命令池由 destroy 统一销毁）
		vkFreeCommandBuffers(device, single_pool, 1, &command_buffer);
		return ok;
	}

	//调度（激活指定管线并下发工作项）
	bool Compute_Context::compute_dispatch(VkCommandBuffer command_buffer, uint64_t handle, uint32_t group_x, uint32_t group_y, uint32_t group_z,
		const void* push_data, uint32_t push_size)
	{
		//查找条目
		Entry* entry = entry_find(handle);
		//条目不存在或命令缓冲为空
		if (entry == nullptr || entry->pipeline == VK_NULL_HANDLE || command_buffer == VK_NULL_HANDLE)
			return false;
		//推送常量尺寸越界
		if (push_data != nullptr && push_size > config.push_size)
			return false;
		//绑定管线与描述符集
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, entry->pipeline);
		VkDescriptorSet descriptor_set = set_get(active_slot);
		//描述符集缺失
		if (descriptor_set == VK_NULL_HANDLE)
			return false;
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
		//写入推送常量（可选）
		if (push_data != nullptr && push_size > 0)
			vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push_data);
		//下发工作项
		vkCmdDispatch(command_buffer, group_x, group_y, group_z);
		++dispatch_count;
		return true;
	}

	//累计调度次数
	uint64_t Compute_Context::dispatch_count_get(void) const
	{
		return dispatch_count;
	}

	//当前统计快照
	Compute_Stats Compute_Context::stats_get(void) const
	{
		Compute_Stats stats;
		stats.dispatch_count = dispatch_count;
		//计时在第二百四十五轮接入时间戳前恒为 -1
		stats.elapsed_ms = -1.0f;
		return stats;
	}

	//按元素数调度（组数向上取整）
	bool Compute_Context::dispatch_ceil(VkCommandBuffer command_buffer, uint64_t handle, uint32_t elements, uint32_t local_size,
		const void* push_data, uint32_t push_size)
	{
		//空元素数表示无需调度
		if (elements == 0)
			return true;
		//换算组数
		uint32_t groups = compute_group_count(elements, local_size);
		//组数为 0（local_size 非法）
		if (groups == 0)
			return false;
		return compute_dispatch(command_buffer, handle, groups, 1, 1, push_data, push_size);
	}

	/*
	屏障
	按用途展开为阶段/访问组合；全部使用 VkMemoryBarrier2（同步 2），不引入全局等待。
	*/
	bool Compute_Context::barrier(VkCommandBuffer command_buffer, Compute_Barrier purpose)
	{
		//命令缓冲为空
		if (command_buffer == VK_NULL_HANDLE)
			return false;
		VkMemoryBarrier2 barrier_info{};
		barrier_info.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		switch (purpose)
		{
		case Compute_Barrier::ComputeToHost:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
			break;
		case Compute_Barrier::HostToCompute:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
			break;
		case Compute_Barrier::ComputeToCompute:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
			break;
		case Compute_Barrier::ComputeToGraphics:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
			break;
		case Compute_Barrier::GraphicsToCompute:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		//第二百四十四轮：计算写 → 传输读（设备本地结果拷到 staging）
		case Compute_Barrier::ComputeToTransfer:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		//第二百四十四轮：传输写 → 主机读（staging 拷贝完成）
		case Compute_Barrier::TransferToHost:
			barrier_info.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
			barrier_info.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier_info.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
			barrier_info.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
			break;
		default:
			return false;
		}
		VkDependencyInfo dependency{};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers = &barrier_info;
		//下发屏障
		vkCmdPipelineBarrier2(command_buffer, &dependency);
		return true;
	}

	/*
	内置健康检查
	链路：建缓冲（主机可见）→ 绑描述符 → 推送魔数 → 调度 → 计算→主机屏障 → 回读首元素。
	缓冲 64 个 float、1 个线程组（local 64），故一次调度即可覆盖全部元素。
	*/
	bool Compute_Context::self_check_run(uint32_t magic, uint32_t& value_out)
	{
		//不支持或无管线
		if (!supported() || pipelines.empty())
			return false;
		//所需字节数与管线句柄
		constexpr uint32_t ELEMENT_COUNT = 64;
		const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(ELEMENT_COUNT) * sizeof(float);
		const uint64_t handle = pipelines.front().handle;
		//创建主机可见存储缓冲
		VkBufferCreateInfo buffer_info{};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = buffer_size;
		buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkBuffer buffer = VK_NULL_HANDLE;
		//创建缓冲
		if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS)
			return false;
		//查询内存需求
		VkMemoryRequirements requirements{};
		vkGetBufferMemoryRequirements(device, buffer, &requirements);
		//选择主机可见且一致的内存类型
		const uint32_t type_index = memory_type_find(physical, requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		//无合适内存类型
		if (type_index == UINT32_MAX)
		{
			vkDestroyBuffer(device, buffer, nullptr);
			return false;
		}
		VkMemoryAllocateInfo allocate_info{};
		allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate_info.allocationSize = requirements.size;
		allocate_info.memoryTypeIndex = type_index;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		//分配内存
		if (vkAllocateMemory(device, &allocate_info, nullptr, &memory) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, buffer, nullptr);
			return false;
		}
		//绑定内存
		vkBindBufferMemory(device, buffer, memory, 0);
		//映射到主机
		void* mapped = nullptr;
		if (vkMapMemory(device, memory, 0, buffer_size, 0, &mapped) != VK_SUCCESS)
		{
			vkFreeMemory(device, memory, nullptr);
			vkDestroyBuffer(device, buffer, nullptr);
			return false;
		}
		//先把缓冲清零（便于验证写入确实发生）
		std::memset(mapped, 0, static_cast<std::size_t>(buffer_size));
		//绑定到当前帧槽的 binding 0
		const uint32_t previous_slot = active_slot;
		bool ok = slot_set(0) && storage_bind(0, 0, buffer, 0, buffer_size);
		//推送常量：魔数 ＋ 元素数
		struct Push_Write
		{
			float magic = 0.0f;
			uint32_t count = 0;
		};
		Push_Write push{};
		std::memcpy(&push.magic, &magic, sizeof(float));
		push.count = ELEMENT_COUNT;
		if (ok)
		{
			//单次提交：调度 ＋ 计算→主机屏障
			ok = single_submit([&](VkCommandBuffer command_buffer) -> bool
			{
				//绑定并调度
				if (!compute_dispatch(command_buffer, handle, 1, 1, 1, &push, sizeof(push)))
					return false;
				//计算写入 → 主机读
				return barrier(command_buffer, Compute_Barrier::ComputeToHost);
			});
		}
		//回读首元素
		if (ok)
		{
			float read_back = 0.0f;
			std::memcpy(&read_back, mapped, sizeof(float));
			std::memcpy(&value_out, &read_back, sizeof(uint32_t));
		}
		//还原帧槽
		slot_set(previous_slot);
		//释放映射与资源
		vkUnmapMemory(device, memory);
		vkFreeMemory(device, memory, nullptr);
		vkDestroyBuffer(device, buffer, nullptr);
		return ok;
	}
}
