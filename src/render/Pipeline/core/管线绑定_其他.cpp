#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	VkDescriptorSet Pipeline_Context::ui_set_get(uint32_t slot) const
	{
		//下标 = slot × UI_SET_PER_SLOT
		const std::size_t flat = static_cast<std::size_t>(slot) * UI_SET_PER_SLOT;
		//越界则返回空句柄
		if (flat >= ui_sets.size())
			return VK_NULL_HANDLE;
		return ui_sets[flat];
	}

	bool Pipeline_Context::ui_texture_bind(uint32_t slot, VkSampler sampler, VkImageView view)
	{
		//目标集
		const VkDescriptorSet target = ui_set_get(slot);
		//前置：设备、目标集、采样器与视图齐备
		if (device == VK_NULL_HANDLE || target == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: UI 纹理绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 0：字形图集）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = target;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	VkDescriptorSet Pipeline_Context::particle_set_get(uint32_t slot) const
	{
		//越界则返回空句柄
		if (slot >= particle_sets.size())
			return VK_NULL_HANDLE;
		return particle_sets[slot];
	}

	bool Pipeline_Context::particle_buffers_bind(uint32_t slot, VkBuffer ubo_buffer, VkBuffer state_buffer, VkBuffer index_buffer)
	{
		//前置：设备、目标集与三缓冲齐备
		if (device == VK_NULL_HANDLE || slot >= particle_sets.size() || particle_sets[slot] == VK_NULL_HANDLE
			|| ubo_buffer == VK_NULL_HANDLE || state_buffer == VK_NULL_HANDLE || index_buffer == VK_NULL_HANDLE)
		{
			logger.error("管线:: 粒子专用集绑定参数不完整（帧槽 {}）", slot);
			return false;
		}

		//绑定 0：场景 UBO（范围与场景集一致，第五百一十三轮 1792，第五百二十一轮 1824，第六百七十二轮 1840，第六百九十九轮 2480，第七百零一轮 3760，第七百零八轮 3824，第七百零九轮 3920，第七百一十三轮 3936）
		VkDescriptorBufferInfo ubo_info{};
		ubo_info.buffer = ubo_buffer;
		ubo_info.offset = 0;
		ubo_info.range = 3936;
		VkWriteDescriptorSet ubo_write{};
		ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		ubo_write.dstSet = particle_sets[slot];
		ubo_write.dstBinding = 0;
		ubo_write.dstArrayElement = 0;
		ubo_write.descriptorCount = 1;
		ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubo_write.pBufferInfo = &ubo_info;

		//绑定 1..4：状态 / 索引 / 状态占位 / 索引占位（四条只读存储缓冲）
		const VkBuffer storage_buffers[4] = { state_buffer, index_buffer, state_buffer, index_buffer };
		VkDescriptorBufferInfo storage_infos[4]{};
		VkWriteDescriptorSet storage_writes[4]{};
		for (uint32_t index = 0; index < 4; ++index)
		{
			storage_infos[index].buffer = storage_buffers[index];
			storage_infos[index].offset = 0;
			storage_infos[index].range = VK_WHOLE_SIZE;
			storage_writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			storage_writes[index].dstSet = particle_sets[slot];
			storage_writes[index].dstBinding = index + 1;
			storage_writes[index].dstArrayElement = 0;
			storage_writes[index].descriptorCount = 1;
			storage_writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			storage_writes[index].pBufferInfo = &storage_infos[index];
		}

		//一次更新五条绑定
		const VkWriteDescriptorSet writes[5] = { ubo_write, storage_writes[0], storage_writes[1],
			storage_writes[2], storage_writes[3] };
		vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
		return true;
	}

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

	bool Pipeline_Context::spot_shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index)
	{
		//前置：设备、目标集、采样器与视图齐备，索引在数组范围内（第五百一十三轮扩为 8 个元素）
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= 8)
		{
			logger.error("管线:: 聚光灯阴影图绑定参数不完整");
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 9，数组元素 index）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sampler_sets[slot];
		write.dstBinding = 9;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}

	bool Pipeline_Context::sky_bind(uint32_t slot, VkSampler sampler, VkImageView view)
	{
		//前置：帧槽范围、采样器与视图齐备
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 天空盒绑定参数不完整（帧槽 {}）", slot);
			return false;
		}

		//图像描述（终态为着色器可读）
		VkDescriptorImageInfo image_info{};
		image_info.sampler = sampler;
		image_info.imageView = view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//描述符写（set 1 binding 5）
		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = sampler_sets[slot];
		write.dstBinding = 5;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;

		//更新描述符集
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		return true;
	}
}
