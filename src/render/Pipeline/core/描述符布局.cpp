#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	创建描述符集布局
	set 0：场景 UBO（binding 0，顶点阶段可见，逐帧更新视图投影矩阵）；
	set 1：采样器（binding 0，片元阶段可见，反照率贴图）。
	*/
	bool Pipeline_Context::descriptor_layouts_create(void)
	{
		//set 0 绑定（场景 UBO）
		VkDescriptorSetLayoutBinding scene_binding{};
		scene_binding.binding = 0;
		scene_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		scene_binding.descriptorCount = 1;
		scene_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		//set 0 布局创建信息
		VkDescriptorSetLayoutCreateInfo scene_info{};
		scene_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		scene_info.bindingCount = 1;
		scene_info.pBindings = &scene_binding;
		//若场景布局创建失败
		if (!vk_result_check(vkCreateDescriptorSetLayout(device, &scene_info, nullptr, &scene_set_layout), "vkCreateDescriptorSetLayout（场景）"))
			return false;

		//set 1 绑定（采样器）
		VkDescriptorSetLayoutBinding sampler_binding{};
		sampler_binding.binding = 0;
		sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_binding.descriptorCount = 1;
		sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		//set 1 布局创建信息
		VkDescriptorSetLayoutCreateInfo sampler_info{};
		sampler_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		sampler_info.bindingCount = 1;
		sampler_info.pBindings = &sampler_binding;
		//若采样器布局创建失败
		if (!vk_result_check(vkCreateDescriptorSetLayout(device, &sampler_info, nullptr, &sampler_set_layout), "vkCreateDescriptorSetLayout（采样器）"))
			return false;

		return true;
	}
}
