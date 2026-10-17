#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构
	Pipeline_Context::~Pipeline_Context(void)
	{
		//释放已创建的管线资源
		destroy();
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

		//创建描述符集布局（set0 / set1）
		if (!descriptor_layouts_create())
		{
			logger.error("管线:: 描述符集布局创建失败，回退");
			destroy();
			return false;
		}
		//创建管线布局（含推送常量范围）
		if (!pipeline_layout_create())
		{
			logger.error("管线:: 管线布局创建失败，回退");
			destroy();
			return false;
		}

		/*
		描述符池与每帧槽描述符集
		池容量：UNIFORM_BUFFER × frame_overlap ＋ COMBINED_IMAGE_SAMPLER × frame_overlap；
		最大集数 = frame_overlap × 2（每帧槽各一个 set0 与 set1）。
		*/
		VkDescriptorPoolSize pool_sizes[2]{};
		pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		pool_sizes[0].descriptorCount = config.frame_overlap;
		pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_sizes[1].descriptorCount = config.frame_overlap;

		//描述符池创建信息
		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.maxSets = config.frame_overlap * 2;
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

		logger.info("管线:: 管线基础设施构建完成（在途帧 {}，颜色格式 {}，深度格式 {}）",
			config.frame_overlap, static_cast<int32_t>(config.color_format), static_cast<int32_t>(config.depth_format));
		return true;
	}

	//销毁
	void Pipeline_Context::destroy(void)
	{
		//若逻辑设备已记录
		if (device != VK_NULL_HANDLE)
		{
			//销毁图形管线
			if (pipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(device, pipeline, nullptr);
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
		}

		//清空容器与句柄
		scene_sets.clear();
		sampler_sets.clear();
		pipeline = VK_NULL_HANDLE;
		vert_module = VK_NULL_HANDLE;
		frag_module = VK_NULL_HANDLE;
		descriptor_pool = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
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

	//构建图形管线
	bool Pipeline_Context::pipeline_build(VkShaderModule vert_module, VkShaderModule frag_module)
	{
		//前置：基础设施与着色器模块必须齐备
		if (!valid() || vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE)
		{
			logger.error("管线:: 图形管线构建前置不完整");
			return false;
		}
		//已存在管线则先行销毁（幂等重建）
		if (pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
		//记住着色器模块，供格式变化时重建复用（模块所有权仍在调用方）
		this->vert_module = vert_module;
		this->frag_module = frag_module;

		//创建图形管线
		if (!graphics_pipeline_create(vert_module, frag_module))
			return false;

		logger.info("管线:: 图形管线已建立");
		return true;
	}

	//按交换链格式重建图形管线
	bool Pipeline_Context::pipeline_rebuild(VkFormat color_format, VkFormat depth_format)
	{
		//图形管线尚未建立则无需处理
		if (pipeline == VK_NULL_HANDLE)
			return true;
		//格式未变则无需重建（空操作）
		if (config.color_format == color_format && config.depth_format == depth_format)
			return true;

		logger.info("管线:: 交换链格式变化（颜色 {}，深度 {}），重建图形管线",
			static_cast<int32_t>(color_format), static_cast<int32_t>(depth_format));
		//等待设备空闲，确保旧管线不再被在途帧引用
		if (!vk_result_check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle（管线重建）"))
			return false;
		//更新配置中的格式
		config.color_format = color_format;
		config.depth_format = depth_format;
		//复用已记住的着色器模块重建
		return pipeline_build(vert_module, frag_module);
	}

	//获取图形管线
	VkPipeline Pipeline_Context::pipeline_get(void) const
	{
		return pipeline;
	}

	//获取管线布局
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

	//向第 slot 帧槽写场景 UBO
	bool Pipeline_Context::scene_bind(uint32_t slot, VkBuffer ubo_buffer)
	{
		//前置：设备、目标集、缓冲齐备
		if (device == VK_NULL_HANDLE || slot >= scene_sets.size() || scene_sets[slot] == VK_NULL_HANDLE || ubo_buffer == VK_NULL_HANDLE)
		{
			logger.error("管线:: 场景 UBO 绑定参数不完整");
			return false;
		}

		//缓冲描述（场景 UBO 恒为 64 字节的 mat4）
		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = ubo_buffer;
		buffer_info.offset = 0;
		buffer_info.range = 64;

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

		//描述符写（set 1 binding 0）
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
}
