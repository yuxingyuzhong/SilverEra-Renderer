#include "../局部命名空间使用.h"
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

	Pipeline_Entry* Pipeline_Context::entry_find(uint64_t handle)
	{
		//线性查找（条目数量很小，无需哈希）
		for (Pipeline_Entry& entry : pipelines)
			//句柄一致即命中
			if (entry.handle == handle)
				return &entry;
		return nullptr;
	}

	const Pipeline_Entry* Pipeline_Context::entry_find(uint64_t handle) const
	{
		//线性查找（条目数量很小，无需哈希）
		for (const Pipeline_Entry& entry : pipelines)
			//句柄一致即命中
			if (entry.handle == handle)
				return &entry;
		return nullptr;
	}

	bool Pipeline_Context::entry_pipeline_create(Pipeline_Entry& entry)
	{
		//创建结果（失败时保持空）
		VkPipeline created = VK_NULL_HANDLE;
		//若图形管线创建失败
		if (!graphics_pipeline_create(entry.vert_module, entry.frag_module, entry.blend_mode, created,
			entry.color_format, entry.cull_none, entry.sample_count, entry.input_locations,
			entry.depth_write, entry.depth_lequal, entry.alpha_to_coverage, entry.cull_front,
			entry.depth_test, entry.depth_format_override, entry.depth_format, entry.tone_id_format))
			return false;
		//写回条目
		entry.pipeline = created;
		return true;
	}

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
		池容量：UNIFORM_BUFFER × frame_overlap ＋ COMBINED_IMAGE_SAMPLER × frame_overlap × 13；
		采样器布局含 10 个 binding（0 = 当前源、1 = 场景目标 / 阴影图、2 = 场景深度、
		3 = 法线贴图、4 = 粗糙度贴图、5 = 天空盒、6 = 自发光贴图、
		7 = 环境光遮蔽贴图、8 = 金属度贴图、9 = 聚光灯阴影图数组（4 个元素）；
		第五百零七轮 binding 9 由 1 个元素扩为 4，故单集描述符数为 9 + 4 = 13），
		故采样器类描述符按十三倍计；
		最大集数 = frame_overlap × (2 + POST_SET_PER_SLOT)。
		*/
		VkDescriptorPoolSize pool_sizes[3]{};
		pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		//场景集 ＋ 粒子专用集（第三百四十一轮：每帧槽各一条 UBO 绑定）
		pool_sizes[0].descriptorCount = config.frame_overlap * 2;
		//簇数据存储缓冲（第二百八十八轮；每帧槽四条绑定）＋ 粒子专用集四条（第三百四十一轮）
		pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		pool_sizes[1].descriptorCount = config.frame_overlap * 8;
		pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		//采样器类描述符按「场景集 ＋ UI 集 ＋ 后处理集 ＋ 逐绘制项集」四部分计
		//每帧槽 18 个采样器（10 个单值 ＋ 8 个聚光灯阴影数组元素；第八百一十二轮由 17 扩为 18，含编号附件）
		pool_sizes[2].descriptorCount = config.frame_overlap
			* (1 + UI_SET_PER_SLOT + POST_SET_PER_SLOT + DRAW_SET_PER_SLOT) * 18;

		//描述符池创建信息
		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		//集数：场景 ＋ 采样器 ＋ UI ＋ 后处理 + 逐绘制项 ＋ 粒子专用（第三百四十一轮）
		pool_info.maxSets = config.frame_overlap * (3 + UI_SET_PER_SLOT + POST_SET_PER_SLOT + DRAW_SET_PER_SLOT);
		pool_info.poolSizeCount = 3;
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

		//分配 UI 专用采样器描述符集（set 1 同布局，按帧槽独立分配）
		ui_sets.resize(static_cast<std::size_t>(config.frame_overlap) * UI_SET_PER_SLOT);
		vector<VkDescriptorSetLayout> ui_layouts(ui_sets.size(), sampler_set_layout);
		VkDescriptorSetAllocateInfo ui_alloc{};
		ui_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ui_alloc.descriptorPool = descriptor_pool;
		ui_alloc.descriptorSetCount = static_cast<uint32_t>(ui_layouts.size());
		ui_alloc.pSetLayouts = ui_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &ui_alloc, ui_sets.data()), "vkAllocateDescriptorSets（UI）"))
		{
			logger.error("管线:: UI 描述符集分配失败，回退");
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

		//分配逐绘制项采样器描述符集（set 1 同布局，按「帧槽 × DRAW_SET_PER_SLOT」独立分配；第一百七十四轮）
		draw_sets.resize(static_cast<std::size_t>(config.frame_overlap) * DRAW_SET_PER_SLOT);
		vector<VkDescriptorSetLayout> draw_layouts(draw_sets.size(), sampler_set_layout);
		VkDescriptorSetAllocateInfo draw_alloc{};
		draw_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		draw_alloc.descriptorPool = descriptor_pool;
		draw_alloc.descriptorSetCount = static_cast<uint32_t>(draw_layouts.size());
		draw_alloc.pSetLayouts = draw_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &draw_alloc, draw_sets.data()), "vkAllocateDescriptorSets（逐绘制项）"))
		{
			logger.error("管线:: 逐绘制项描述符集分配失败，回退");
			destroy();
			return false;
		}

		//分配粒子专用场景集（set 0 同布局，按帧槽独立分配；第三百四十一轮）
		particle_sets.resize(config.frame_overlap);
		vector<VkDescriptorSetLayout> particle_layouts(config.frame_overlap, scene_set_layout);
		VkDescriptorSetAllocateInfo particle_alloc{};
		particle_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		particle_alloc.descriptorPool = descriptor_pool;
		particle_alloc.descriptorSetCount = config.frame_overlap;
		particle_alloc.pSetLayouts = particle_layouts.data();
		//若分配失败
		if (!vk_result_check(vkAllocateDescriptorSets(device, &particle_alloc, particle_sets.data()), "vkAllocateDescriptorSets（粒子）"))
		{
			logger.error("管线:: 粒子专用描述符集分配失败，回退");
			destroy();
			return false;
		}

		logger.info("管线:: 管线基础设施构建完成（在途帧 {}，颜色格式 {}，深度格式 {}）",
			config.frame_overlap, static_cast<int32_t>(config.color_format), static_cast<int32_t>(config.depth_format));
		return true;
	}

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
		ui_sets.clear();
		draw_sets.clear();
		particle_sets.clear();
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

	bool Pipeline_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && scene_set_layout != VK_NULL_HANDLE && sampler_set_layout != VK_NULL_HANDLE;
	}

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

	bool Pipeline_Context::pipeline_register(uint64_t& handle_out, VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode,
		VkFormat color_format, bool cull_none, uint32_t sample_count, uint32_t input_locations,
		bool depth_write, bool depth_lequal, bool alpha_to_coverage, bool cull_front,
		bool depth_test, bool depth_format_override, VkFormat depth_format, VkFormat tone_id_format)
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
		entry.cull_front = cull_front;
		entry.sample_count = sample_count;
		entry.input_locations = input_locations;
		entry.depth_write = depth_write;
		entry.depth_lequal = depth_lequal;
		entry.alpha_to_coverage = alpha_to_coverage;
		entry.depth_test = depth_test;
		entry.depth_format_override = depth_format_override;
		entry.depth_format = depth_format;
		//第二颜色附件格式（第 804 轮 · MRT 段 2）
		entry.tone_id_format = tone_id_format;
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

	uint32_t Pipeline_Context::pipeline_count(void) const
	{
		return static_cast<uint32_t>(pipelines.size());
	}

	bool Pipeline_Context::pipeline_has(uint64_t handle) const
	{
		return entry_find(handle) != nullptr;
	}

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

	VkPipeline Pipeline_Context::pipeline_get(void) const
	{
		//取当前激活条目
		const Pipeline_Entry* entry = entry_find(active_handle);
		//条目存在则返回其管线，否则返回空句柄
		return entry != nullptr ? entry->pipeline : VK_NULL_HANDLE;
	}

	Blend_Mode Pipeline_Context::pipeline_blend_mode_get(void) const
	{
		//取当前激活条目
		const Pipeline_Entry* entry = entry_find(active_handle);
		//条目存在则返回其混合档，否则按不透明处理
		return entry != nullptr ? entry->blend_mode : Blend_Mode::Opaque;
	}

	VkPipeline Pipeline_Context::pipeline_by_handle_get(uint64_t handle) const
	{
		//按句柄取条目
		const Pipeline_Entry* entry = entry_find(handle);
		//条目存在则返回其管线，否则返回空句柄
		return entry != nullptr ? entry->pipeline : VK_NULL_HANDLE;
	}

	VkPipelineLayout Pipeline_Context::layout_get(void) const
	{
		return pipeline_layout;
	}
}
