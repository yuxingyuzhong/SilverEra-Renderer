#include "src/render/Pipeline/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构
	Pipeline_Context::~Pipeline_Context(void)
	{
		//释放已创建的管线资源
		destroy();
	}

	//按句柄查找条目（可写）
	Pipeline_Entry* Pipeline_Context::entry_find(uint64_t handle)
	{
		//线性查找（条目数量很小，无需哈希）
		for (Pipeline_Entry& entry : pipelines)
			//句柄一致即命中
			if (entry.handle == handle)
				return &entry;
		return nullptr;
	}

	//按句柄查找条目（只读）
	const Pipeline_Entry* Pipeline_Context::entry_find(uint64_t handle) const
	{
		//线性查找（条目数量很小，无需哈希）
		for (const Pipeline_Entry& entry : pipelines)
			//句柄一致即命中
			if (entry.handle == handle)
				return &entry;
		return nullptr;
	}

	//按当前配置格式与条目自身着色器 / 混合档创建图形管线（条目可自带颜色格式覆盖）
	bool Pipeline_Context::entry_pipeline_create(Pipeline_Entry& entry)
	{
		//创建结果（失败时保持空）
		VkPipeline created = VK_NULL_HANDLE;
		//若图形管线创建失败
		if (!graphics_pipeline_create(entry.vert_module, entry.frag_module, entry.blend_mode, created,
			entry.color_format, entry.cull_none, entry.sample_count, entry.input_locations))
			return false;
		//写回条目
		entry.pipeline = created;
		return true;
	}

	//构建
	bool Pipeline_Context::build(VkDevice device, const Pipeline_Config& config)
	{
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("管线:: 构建时逻辑设备为空");
			return false;
		}
		//前置：在途帧数为正
		if (config.frame_overlap == 0)
		{
			logger.error("管线:: 在途帧数必须为正");
			return false;
		}

		//记录设备与配置
		this->device = device;
		this->config = config;

		//创建空管线缓存（第一百零六轮；外部数据由 cache_set 在创建管线前注入）
		VkPipelineCacheCreateInfo cache_info{};
		cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		if (!vk_result_check(vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache"))
		{
			logger.error("管线:: 管线缓存创建失败，回退");
			destroy();
			return false;
		}

		//创建描述符集布局（set0 / set1）
		if (!descriptor_layouts_create())
		{
			logger.error("管线:: 描述符集布局创建失败，回退");
			destroy();
			return false;
		}
		//创建管线布局（含推送常量范围；全管线共享）
		if (!pipeline_layout_create())
		{
			logger.error("管线:: 管线布局创建失败，回退");
			destroy();
			return false;
		}

		/*
		描述符池与每帧槽描述符集
		池容量：UNIFORM_BUFFER × frame_overlap ＋ COMBINED_IMAGE_SAMPLER × frame_overlap × 5；
		采样器布局已含 5 个 binding（0 = 当前源、1 = 场景目标 / 阴影图、2 = 场景深度、
		3 = 法线贴图、4 = 粗糙度贴图；第一百二十四轮由 3 增为 5），故采样器类描述符按五倍计；
		最大集数 = frame_overlap × (2 + POST_SET_PER_SLOT)。
		*/
		VkDescriptorPoolSize pool_sizes[2]{};
		pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		pool_sizes[0].descriptorCount = config.frame_overlap;
		pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_sizes[1].descriptorCount = config.frame_overlap * (1 + POST_SET_PER_SLOT) * 5;

		//描述符池创建信息
		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.maxSets = config.frame_overlap * (2 + POST_SET_PER_SLOT);
		pool_info.poolSizeCount = 2;
		pool_info.pPoolSizes = pool_sizes;
		//若池创建失败
		if (!vk_result_check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool), "vkCreateDescriptorPool"))
		{
			logger.error("管线:: 描述符池创建失败，回退");
			destroy();
			return false;
		}

		//分配场景描述符集（set 0，按帧槽）
		scene_sets.resize(config.frame_overlap);
		vector<VkDescriptorSetLayout> scene_layouts(config.frame_overlap, scene_set_layout);
		VkDescriptorSetAllocateInfo scene_alloc{};
		scene_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		scene_alloc.descriptorPool = descriptor_pool;
		scene_alloc.descriptorSetCount = config.frame_overlap;
		scene_alloc.pSetLayouts = scene_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &scene_alloc, scene_sets.data()), "vkAllocateDescriptorSets（场景）"))
		{
			logger.error("管线:: 场景描述符集分配失败，回退");
			destroy();
			return false;
		}

		//分配采样器描述符集（set 1，按帧槽）
		sampler_sets.resize(config.frame_overlap);
		vector<VkDescriptorSetLayout> sampler_layouts(config.frame_overlap, sampler_set_layout);
		VkDescriptorSetAllocateInfo sampler_alloc{};
		sampler_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		sampler_alloc.descriptorPool = descriptor_pool;
		sampler_alloc.descriptorSetCount = config.frame_overlap;
		sampler_alloc.pSetLayouts = sampler_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &sampler_alloc, sampler_sets.data()), "vkAllocateDescriptorSets（采样器）"))
		{
			logger.error("管线:: 采样器描述符集分配失败，回退");
			destroy();
			return false;
		}

		//分配后处理采样器描述符集（set 1 同布局，按「帧槽 × POST_SET_PER_SLOT」独立分配）
		post_sets.resize(config.frame_overlap * POST_SET_PER_SLOT);
		vector<VkDescriptorSetLayout> post_layouts(config.frame_overlap * POST_SET_PER_SLOT, sampler_set_layout);
		VkDescriptorSetAllocateInfo post_alloc{};
		post_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		post_alloc.descriptorPool = descriptor_pool;
		post_alloc.descriptorSetCount = config.frame_overlap * POST_SET_PER_SLOT;
		post_alloc.pSetLayouts = post_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &post_alloc, post_sets.data()), "vkAllocateDescriptorSets（后处理）"))
		{
			logger.error("管线:: 后处理描述符集分配失败，回退");
			destroy();
			return false;
		}

		logger.info("管线:: 管线基础设施构建完成（在途帧 {}，颜色格式 {}，深度格式 {}）",
			config.frame_overlap, static_cast<int32_t>(config.color_format), static_cast<int32_t>(config.depth_format));
		return true;
	}

	//销毁（幂等；销毁全部图形管线与基础设施）
	void Pipeline_Context::destroy(void)
	{
		//若逻辑设备已记录
		if (device != VK_NULL_HANDLE)
		{
			//销毁全部图形管线（默认与具名一并）
			for (Pipeline_Entry& entry : pipelines)
			{
				//若该条目管线有效
				if (entry.pipeline != VK_NULL_HANDLE)
					vkDestroyPipeline(device, entry.pipeline, nullptr);
			}
			//销毁描述符池（连带回收其分配的描述符集）
			if (descriptor_pool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
			//销毁管线布局
			if (pipeline_layout != VK_NULL_HANDLE)
				vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
			//销毁采样器描述符集布局
			if (sampler_set_layout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, sampler_set_layout, nullptr);
			//销毁场景描述符集布局
			if (scene_set_layout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, scene_set_layout, nullptr);
			//销毁管线缓存（第一百零六轮；须在全部图形管线之后）
			if (pipeline_cache != VK_NULL_HANDLE)
				vkDestroyPipelineCache(device, pipeline_cache, nullptr);
		}

		//清空容器与句柄
		pipelines.clear();
		scene_sets.clear();
		sampler_sets.clear();
		post_sets.clear();
		next_handle = 1;
		active_handle = 0;
		descriptor_pool = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		pipeline_cache = VK_NULL_HANDLE;
		sampler_set_layout = VK_NULL_HANDLE;
		scene_set_layout = VK_NULL_HANDLE;
		device = VK_NULL_HANDLE;
		config = Pipeline_Config{};
	}

	//是否已构建完成
	bool Pipeline_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && scene_set_layout != VK_NULL_HANDLE && sampler_set_layout != VK_NULL_HANDLE;
	}

	//登记 / 替换并激活句柄 0「默认场景管线」
	bool Pipeline_Context::pipeline_build(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode,
		uint32_t input_locations)
	{
		//前置：基础设施与着色器模块必须齐备
		if (!valid() || vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE)
		{
			logger.error("管线:: 图形管线构建前置不完整");
			return false;
		}
		//查找句柄 0 默认条目
		Pipeline_Entry* entry = entry_find(0);
		//若尚未登记则新建一条（句柄 0）
		if (entry == nullptr)
		{
			pipelines.push_back(Pipeline_Entry{});
			entry = &pipelines.back();
			entry->handle = 0;
		}
		//已有管线则先行销毁（幂等重建）
		if (entry->pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, entry->pipeline, nullptr);
			entry->pipeline = VK_NULL_HANDLE;
		}
		//记住着色器模块（所有权仍在调用方）与混合档
		entry->vert_module = vert_module;
		entry->frag_module = frag_module;
		entry->blend_mode = blend_mode;
		entry->input_locations = input_locations;
		//创建图形管线
		if (!entry_pipeline_create(*entry))
			return false;
		//激活默认管线
		active_handle = 0;

		logger.info("管线:: 默认场景管线已建立（混合档 {}）", static_cast<int32_t>(blend_mode));
		return true;
	}

	//登记具名图形管线
	bool Pipeline_Context::pipeline_register(uint64_t& handle_out, VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode,
		VkFormat color_format, bool cull_none, uint32_t sample_count, uint32_t input_locations)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：基础设施与着色器模块必须齐备
		if (!valid() || vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE)
		{
			logger.error("管线:: 具名管线登记前置不完整");
			return false;
		}
		//先按当前格式把图形管线建好（失败则整体拒绝，不登记任何条目）
		Pipeline_Entry entry{};
		entry.handle = next_handle;
		entry.vert_module = vert_module;
		entry.frag_module = frag_module;
		entry.blend_mode = blend_mode;
		entry.color_format = color_format;
		entry.cull_none = cull_none;
		entry.sample_count = sample_count;
		entry.input_locations = input_locations;
		//若创建失败
		if (!entry_pipeline_create(entry))
		{
			logger.error("管线:: 具名管线创建失败");
			return false;
		}
		//登记条目并推进句柄
		pipelines.push_back(entry);
		++next_handle;
		handle_out = entry.handle;

		logger.info("管线:: 具名管线已登记（句柄 {}，混合档 {}）", entry.handle, static_cast<int32_t>(blend_mode));
		return true;
	}

	//切换当前激活管线
	bool Pipeline_Context::pipeline_activate(uint64_t handle)
	{
		//句柄未登记则拒绝（激活保持不变）
		if (entry_find(handle) == nullptr)
		{
			logger.error("管线:: 激活失败：句柄 {} 未登记", handle);
			return false;
		}
		active_handle = handle;
		return true;
	}

	//注销具名管线
	void Pipeline_Context::pipeline_release(uint64_t handle)
	{
		//句柄 0 为默认管线，禁止注销
		if (handle == 0)
			return;
		//查找条目
		for (vector<Pipeline_Entry>::iterator it = pipelines.begin(); it != pipelines.end(); ++it)
		{
			//跳过非目标条目
			if (it->handle != handle)
				continue;
			//销毁其图形管线
			if (it->pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
				vkDestroyPipeline(device, it->pipeline, nullptr);
			//移除条目
			pipelines.erase(it);
			//若释放的正是当前激活管线则回落默认
			if (active_handle == handle)
				active_handle = 0;
			logger.info("管线:: 具名管线已注销（句柄 {}）", handle);
			return;
		}
	}

	//按新颜色 / 深度格式与采样数重建全部已登记图形管线
	bool Pipeline_Context::pipeline_rebuild(VkFormat color_format, VkFormat depth_format, uint32_t sample_count)
	{
		//无任何条目则无需处理
		if (pipelines.empty())
			return true;
		//采样数兜底为 1
		if (sample_count == 0)
			sample_count = 1;
		//格式与采样数均未变则无需重建（空操作）
		if (config.color_format == color_format && config.depth_format == depth_format
			&& config.sample_count == sample_count)
			return true;

		logger.info("管线:: 全部图形管线重建（颜色 {}，深度 {}，采样数 {}）",
			static_cast<int32_t>(color_format), static_cast<int32_t>(depth_format), sample_count);
		//等待设备空闲，确保旧管线不再被在途帧引用
		if (!vk_result_check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle（管线重建）"))
			return false;
		//更新配置中的格式与采样数
		config.color_format = color_format;
		config.depth_format = depth_format;
		config.sample_count = sample_count;
		//逐条目按新格式重建（着色器与各自混合档保留）
		for (Pipeline_Entry& entry : pipelines)
		{
			//销毁旧管线
			if (entry.pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(device, entry.pipeline, nullptr);
				entry.pipeline = VK_NULL_HANDLE;
			}
			//按新格式重建
			if (!entry_pipeline_create(entry))
				return false;
		}
		return true;
	}

	//重建句柄 0 默认管线的混合档
	bool Pipeline_Context::pipeline_default_blend_set(Blend_Mode blend_mode)
	{
		//查找默认条目
		Pipeline_Entry* entry = entry_find(0);
		//无默认管线则空操作
		if (entry == nullptr)
			return true;
		//同档且管线有效则空操作
		if (entry->blend_mode == blend_mode && entry->pipeline != VK_NULL_HANDLE)
			return true;

		//等待设备空闲，确保旧管线不再被在途帧引用
		if (!vk_result_check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle（混合档切换）"))
			return false;
		//销毁旧管线
		if (entry->pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, entry->pipeline, nullptr);
			entry->pipeline = VK_NULL_HANDLE;
		}
		//按新混合档重建
		entry->blend_mode = blend_mode;
		if (!entry_pipeline_create(*entry))
			return false;

		logger.info("管线:: 默认场景管线混合档已切换为 {}", static_cast<int32_t>(blend_mode));
		return true;
	}

	//已登记图形管线数量（第一百一十三轮）
	uint32_t Pipeline_Context::pipeline_count(void) const
	{
		return static_cast<uint32_t>(pipelines.size());
	}

	//句柄是否已登记
	bool Pipeline_Context::pipeline_has(uint64_t handle) const
	{
		return entry_find(handle) != nullptr;
	}

	//获取指定句柄图形管线实际烘焙的采样数（第九十八轮）
	uint32_t Pipeline_Context::pipeline_sample_count(uint64_t handle) const
	{
		//取目标条目
		const Pipeline_Entry* entry = entry_find(handle);
		//未登记按单采样处理
		if (entry == nullptr)
			return 1;
		//条目固定采样数优先，否则跟随配置
		uint32_t samples = entry->sample_count != 0 ? entry->sample_count : config.sample_count;
		return samples == 0 ? 1 : samples;
	}

	//用外部字节数据重建管线缓存（第一百零六轮）
	bool Pipeline_Context::cache_set(const void* data, std::size_t size)
	{
		//前置：逻辑设备必须就绪
		if (device == VK_NULL_HANDLE)
		{
			logger.error("管线:: 设置管线缓存时逻辑设备为空");
			return false;
		}
		//销毁旧缓存（已创建的图形管线不受影响）
		if (pipeline_cache != VK_NULL_HANDLE)
		{
			vkDestroyPipelineCache(device, pipeline_cache, nullptr);
			pipeline_cache = VK_NULL_HANDLE;
		}
		//以外部数据重建缓存
		VkPipelineCacheCreateInfo cache_info{};
		cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		cache_info.initialDataSize = (data != nullptr && size != 0) ? size : 0;
		cache_info.pInitialData = cache_info.initialDataSize != 0 ? data : nullptr;
		if (!vk_result_check(vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache（外部数据）"))
		{
			//失败时重建空缓存，保证后续管线创建仍可用
			cache_info.initialDataSize = 0;
			cache_info.pInitialData = nullptr;
			vk_result_check(vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache（空）");
			return false;
		}
		logger.info("管线:: 管线缓存已载入（{} 字节）", static_cast<uint32_t>(size));
		return true;
	}

	//取回管线缓存字节（第一百零六轮）
	bool Pipeline_Context::cache_get(std::vector<uint8_t>& bytes_out) const
	{
		//出参先清空
		bytes_out.clear();
		//前置：缓存与设备必须就绪
		if (device == VK_NULL_HANDLE || pipeline_cache == VK_NULL_HANDLE)
			return false;
		//先取所需字节数
		std::size_t size = 0;
		if (!vk_result_check(vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr), "vkGetPipelineCacheData（长度）"))
			return false;
		//空缓存无可保存内容
		if (size == 0)
			return false;
		//取回内容
		bytes_out.resize(size);
		if (!vk_result_check(vkGetPipelineCacheData(device, pipeline_cache, &size, bytes_out.data()), "vkGetPipelineCacheData"))
		{
			bytes_out.clear();
			return false;
		}
		//按实际长度收敛
		bytes_out.resize(size);
		return true;
	}

	//获取当前激活图形管线
	VkPipeline Pipeline_Context::pipeline_get(void) const
	{
		//取当前激活条目
		const Pipeline_Entry* entry = entry_find(active_handle);
		//条目存在则返回其管线，否则返回空句柄
		return entry != nullptr ? entry->pipeline : VK_NULL_HANDLE;
	}

	//获取指定句柄的图形管线（不改变当前激活管线）
	VkPipeline Pipeline_Context::pipeline_by_handle_get(uint64_t handle) const
	{
		//按句柄取条目
		const Pipeline_Entry* entry = entry_find(handle);
		//条目存在则返回其管线，否则返回空句柄
		return entry != nullptr ? entry->pipeline : VK_NULL_HANDLE;
	}

	//获取管线布局（全管线共享）
	VkPipelineLayout Pipeline_Context::layout_get(void) const
	{
		return pipeline_layout;
	}

	//获取第 slot 帧槽的场景 UBO 描述符集
	VkDescriptorSet Pipeline_Context::scene_set_get(uint32_t slot) const
	{
		//越界则返回空句柄
		if (slot >= scene_sets.size())
			return VK_NULL_HANDLE;
		return scene_sets[slot];
	}

	//获取第 slot 帧槽的采样器描述符集
	VkDescriptorSet Pipeline_Context::sampler_set_get(uint32_t slot) const
	{
		//越界则返回空句柄
		if (slot >= sampler_sets.size())
			return VK_NULL_HANDLE;
		return sampler_sets[slot];
	}

	//获取第 slot 帧槽指定 index 的后处理采样器描述符集
	VkDescriptorSet Pipeline_Context::post_set_get(uint32_t slot, uint32_t index) const
	{
		//index 越界则返回空句柄
		if (index >= POST_SET_PER_SLOT)
			return VK_NULL_HANDLE;
		//帧槽 × 每槽集数 + index 越界则返回空句柄
		std::size_t offset = static_cast<std::size_t>(slot) * POST_SET_PER_SLOT + index;
		if (offset >= post_sets.size())
			return VK_NULL_HANDLE;
		return post_sets[offset];
	}

	//向第 slot 帧槽写场景 UBO
	bool Pipeline_Context::scene_bind(uint32_t slot, VkBuffer ubo_buffer)
	{
		//前置：设备、目标集、缓冲齐备
		if (device == VK_NULL_HANDLE || slot >= scene_sets.size() || scene_sets[slot] == VK_NULL_HANDLE || ubo_buffer == VK_NULL_HANDLE)
		{
			logger.error("管线:: 场景 UBO 绑定参数不完整");
			return false;
		}

		//缓冲描述（场景 UBO 1072 字节：… ＋ 阴影参数与强度 ＋ PBR-lite 参数 ＋ 远区级联矩阵与级联参数 ＋ 相机近远平面；第一百二十三轮由 1056 扩为 1072）
		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = ubo_buffer;
		buffer_info.offset = 0;
		buffer_info.range = 1072;

		//描述符写（set 0 binding 0）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = scene_sets[slot];
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &buffer_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽写采样器
	bool Pipeline_Context::texture_bind(uint32_t slot, VkSampler sampler, VkImageView view)
	{
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 采样器绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 0；binding 1 专用于阴影图，见 shadow_bind）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sampler_sets[slot];
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽的场景采样器集写阴影图（set 1 binding 1）
	bool Pipeline_Context::shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view)
	{
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 阴影图绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 1）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sampler_sets[slot];
		write.dstBinding = 1;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽指定 index 的后处理采样器集写采样器
	bool Pipeline_Context::post_texture_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//取目标后处理集（index 越界则返回空句柄）
		VkDescriptorSet target_set = post_set_get(slot, index);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target_set == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 后处理采样器绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 0）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target_set;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽指定 index 的后处理采样器集写「第二源」（binding 1，通常为场景目标）
	bool Pipeline_Context::post_scene_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//取目标后处理集（index 越界则返回空句柄）
		VkDescriptorSet target_set = post_set_get(slot, index);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target_set == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 后处理第二源绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 1）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target_set;
		write.dstBinding = 1;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽的场景采样器集写材质贴图（set 1 binding 3 / 4；第一百二十四轮）
	bool Pipeline_Context::material_texture_bind(uint32_t slot, VkSampler sampler,
		VkImageView normal_view, VkImageView roughness_view)
	{
		//前置：帧槽范围、采样器与两个视图齐备
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || normal_view == VK_NULL_HANDLE || roughness_view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 材质贴图绑定参数不完整（帧槽 {}）", slot);
			return false;
		}

		//图像描述（两项共用同一采样器与终态布局）
		VkDescriptorImageInfo image_infos[2]{};
		for (std::size_t index = 0; index < 2; ++index)
		{
			image_infos[index].sampler = sampler;
			image_infos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		image_infos[0].imageView = normal_view;
		image_infos[1].imageView = roughness_view;

		//描述符写（set 1 binding 3 与 4）
		VkWriteDescriptorSet writes[2]{};
		for (std::size_t index = 0; index < 2; ++index)
		{
			writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[index].dstSet = sampler_sets[slot];
			writes[index].dstBinding = static_cast<uint32_t>(3 + index);
			writes[index].dstArrayElement = 0;
			writes[index].descriptorCount = 1;
			writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[index].pImageInfo = &image_infos[index];
		}

		//更新描述符集
		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
		return true;
	}

	//向第 slot 帧槽指定 index 的后处理采样器集写「场景深度」（binding 2；第一百二十三轮）
	bool Pipeline_Context::post_depth_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//取目标后处理集（index 越界则返回空句柄）
		VkDescriptorSet target_set = post_set_get(slot, index);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target_set == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 后处理深度绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 2）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target_set;
		write.dstBinding = 2;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}
}
