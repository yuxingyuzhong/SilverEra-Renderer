#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	VkDescriptorSet Pipeline_Context::scene_set_get(uint32_t slot) const
	{
		//越界则返回空句柄
		if (slot >= scene_sets.size())
			return VK_NULL_HANDLE;
		return scene_sets[slot];
	}

	VkDescriptorSet Pipeline_Context::sampler_set_get(uint32_t slot) const
	{
		//越界则返回空句柄
		if (slot >= sampler_sets.size())
			return VK_NULL_HANDLE;
		return sampler_sets[slot];
	}

	bool Pipeline_Context::scene_bind(uint32_t slot, VkBuffer ubo_buffer)
	{
		//前置：设备、目标集、缓冲齐备
		if (device == VK_NULL_HANDLE || slot >= scene_sets.size() || scene_sets[slot] == VK_NULL_HANDLE || ubo_buffer == VK_NULL_HANDLE)
		{
			logger.error("管线:: 场景 UBO 绑定参数不完整");
			return false;
		}

		//缓冲描述（场景 UBO 3952 字节：… ＋ 前向高 24 槽 ＋ 逐瓦片质量缩放 ＋ 环境光照 ＋ Fresnel 口径；第五百一十三轮 1792，第五百二十一轮 1824，第六百七十二轮 1840，第六百九十九轮 2480，第七百零一轮 3760，第七百零八轮 3824，第七百零九轮 3920，第七百一十三轮 3936，第七百一十八轮 3952）
		VkDescriptorBufferInfo buffer_info{};
		buffer_info.buffer = ubo_buffer;
		buffer_info.offset = 0;
		buffer_info.range = 3952;

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

	bool Pipeline_Context::scene_cluster_bind(uint32_t slot, VkBuffer light_buffer, VkBuffer counts_buffer,
		VkBuffer offsets_buffer, VkBuffer list_buffer)
	{
		//前置：设备、目标集与四缓冲齐备
		if (device == VK_NULL_HANDLE || slot >= scene_sets.size() || scene_sets[slot] == VK_NULL_HANDLE
			|| light_buffer == VK_NULL_HANDLE || counts_buffer == VK_NULL_HANDLE
			|| offsets_buffer == VK_NULL_HANDLE || list_buffer == VK_NULL_HANDLE)
		{
			logger.error("管线:: 簇数据存储缓冲绑定参数不完整（帧槽 {}）", slot);
			return false;
		}

		//四条绑定的缓冲描述与写记录（binding 1..4 依次对应四个缓冲）
		const VkBuffer buffers[4] = { light_buffer, counts_buffer, offsets_buffer, list_buffer };
		VkDescriptorBufferInfo buffer_infos[4]{};
		VkWriteDescriptorSet writes[4]{};
		for (uint32_t index = 0; index < 4; ++index)
		{
			buffer_infos[index].buffer = buffers[index];
			buffer_infos[index].offset = 0;
			buffer_infos[index].range = VK_WHOLE_SIZE;
			writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[index].dstSet = scene_sets[slot];
			writes[index].dstBinding = index + 1;
			writes[index].dstArrayElement = 0;
			writes[index].descriptorCount = 1;
			writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writes[index].pBufferInfo = &buffer_infos[index];
		}

		//一次更新四条绑定
		vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
		return true;
	}
}
