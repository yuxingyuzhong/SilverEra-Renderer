#include "src/render/Pipeline/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	创建描述符集布局
	set 0：场景 UBO（binding 0，顶点阶段可见，逐帧更新视图投影矩阵）；
	set 1：采样器（binding 0 = 当前纹理 / 上一级输出；binding 1 = 场景目标，供「双源」后处理片元叠加）。
	binding 1 在单源片元中不被声明，故多出的描述符不影响既有管线；由绑定接口一并写入以避免未写描述符。
	*/
	bool Pipeline_Context::descriptor_layouts_create(void)
	{
		//set 0 绑定（场景 UBO）
		VkDescriptorSetLayoutBinding scene_binding{};
		scene_binding.binding = 0;
		scene_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		scene_binding.descriptorCount = 1;
		//顶点与片元阶段均可见（第二十九轮：片元着色器需读取其中的光照参数）
		scene_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		//set 0 布局创建信息
		VkDescriptorSetLayoutCreateInfo scene_info{};
		scene_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		scene_info.bindingCount = 1;
		scene_info.pBindings = &scene_binding;
		//若场景布局创建失败
		if (!vk_result_check(vkCreateDescriptorSetLayout(device, &scene_info, nullptr, &scene_set_layout), "vkCreateDescriptorSetLayout（场景）"))
			return false;

		//set 1 绑定（采样器：0 = 当前纹理 / 上一级输出，1 = 场景目标 / 阴影图，2 = 场景深度（第一百二十三轮））
		VkDescriptorSetLayoutBinding sampler_bindings[3]{};
		sampler_bindings[0].binding = 0;
		sampler_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_bindings[0].descriptorCount = 1;
		sampler_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		sampler_bindings[1].binding = 1;
		sampler_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_bindings[1].descriptorCount = 1;
		sampler_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		sampler_bindings[2].binding = 2;
		sampler_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_bindings[2].descriptorCount = 1;
		sampler_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		//set 1 布局创建信息
		VkDescriptorSetLayoutCreateInfo sampler_info{};
		sampler_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		sampler_info.bindingCount = 3;
		sampler_info.pBindings = sampler_bindings;
		//若采样器布局创建失败
		if (!vk_result_check(vkCreateDescriptorSetLayout(device, &sampler_info, nullptr, &sampler_set_layout), "vkCreateDescriptorSetLayout（采样器）"))
			return false;

		return true;
	}
}
