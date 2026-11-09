#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
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

	bool Pipeline_Context::post_shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//取目标后处理集（index 越界则返回空句柄）
		VkDescriptorSet target_set = post_set_get(slot, index);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target_set == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 后处理阴影绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 4）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target_set;
		write.dstBinding = 4;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	bool Pipeline_Context::post_lut_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//取目标后处理集（index 越界则返回空句柄）
		VkDescriptorSet target_set = post_set_get(slot, index);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target_set == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 后处理查找表绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 3）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target_set;
		write.dstBinding = 3;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}
}
