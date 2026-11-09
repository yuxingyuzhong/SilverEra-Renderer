#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	VkDescriptorSet Pipeline_Context::draw_set_get(uint32_t slot, uint32_t index) const
	{
		//下标 = slot × DRAW_SET_PER_SLOT + index
		const std::size_t flat = static_cast<std::size_t>(slot) * DRAW_SET_PER_SLOT + index;
		//若越界
		if (slot >= config.frame_overlap || index >= DRAW_SET_PER_SLOT || flat >= draw_sets.size())
			return VK_NULL_HANDLE;
		return draw_sets[flat];
	}

	bool Pipeline_Context::draw_set_bind(uint32_t slot, uint32_t index, VkSampler sampler,
		VkImageView albedo_view, VkImageView normal_view, VkImageView roughness_view,
		VkImageView emissive_view, VkImageView ao_view, VkImageView metallic_view,
		VkImageView shadow_view, VkImageView sky_view, VkImageView spot_shadow_view)
	{
		//目标集
		VkDescriptorSet target = draw_set_get(slot, index);
		//前置：设备、目标集、采样器与各视图齐备
		if (device == VK_NULL_HANDLE || target == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE
			|| albedo_view == VK_NULL_HANDLE || normal_view == VK_NULL_HANDLE || roughness_view == VK_NULL_HANDLE
			|| emissive_view == VK_NULL_HANDLE || ao_view == VK_NULL_HANDLE || metallic_view == VK_NULL_HANDLE
			|| shadow_view == VK_NULL_HANDLE || sky_view == VK_NULL_HANDLE || spot_shadow_view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 逐绘制项采样集写入参数不完整");
			return false;
		}

		//十七条绑定的图像描述（binding 2 以反照率视图占位；binding 9 的 8 个数组元素共用同一视图）
		const VkImageView views[17] = { albedo_view, shadow_view, albedo_view, normal_view, roughness_view,
			sky_view, emissive_view, ao_view, metallic_view, spot_shadow_view, spot_shadow_view,
			spot_shadow_view, spot_shadow_view, spot_shadow_view, spot_shadow_view, spot_shadow_view,
			spot_shadow_view };
		VkDescriptorImageInfo infos[17]{};
		VkWriteDescriptorSet writes[10]{};
		for (uint32_t binding = 0; binding < 17; ++binding)
		{
			infos[binding].sampler = sampler;
			infos[binding].imageView = views[binding];
			infos[binding].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		//binding 0..8：单值写
		for (uint32_t binding = 0; binding < 9; ++binding)
		{
			writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[binding].dstSet = target;
			writes[binding].dstBinding = binding;
			writes[binding].dstArrayElement = 0;
			writes[binding].descriptorCount = 1;
			writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[binding].pImageInfo = &infos[binding];
		}
		//binding 9：8 个数组元素一次写入
		writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[9].dstSet = target;
		writes[9].dstBinding = 9;
		writes[9].dstArrayElement = 0;
		writes[9].descriptorCount = 8;
		writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[9].pImageInfo = &infos[9];
		//一次提交全部绑定
		vkUpdateDescriptorSets(device, 10, writes, 0, nullptr);
		return true;
	}

	bool Pipeline_Context::draw_sets_bind_batch(uint32_t slot, const Draw_Set_Views* views, uint32_t count)
	{
		//无条目即空操作
		if (count == 0)
			return true;
		//前置：设备、视图数组与容量必须齐备
		if (device == VK_NULL_HANDLE || views == nullptr || count > DRAW_SET_PER_SLOT || slot >= sampler_sets.size())
		{
			logger.error("管线:: 逐绘制项采样集批量写入参数不完整");
			return false;
		}
		//每条目的描述符数（17）与写记录数（9 条单值 ＋ 1 条数组写 = 10）
		const uint32_t descriptor_count = 17;
		const uint32_t record_count = 10;
		//复用缓冲（容量不足时扩容一次，之后不再分配）
		draw_batch_infos.resize(static_cast<std::size_t>(count) * descriptor_count);
		draw_batch_writes.resize(static_cast<std::size_t>(count) * record_count);
		for (uint32_t index = 0; index < count; ++index)
		{
			const Draw_Set_Views& view = views[index];
			//目标集（与逐项调用同一批集）
			VkDescriptorSet target = draw_set_get(slot, index);
			//任一条目视图 / 采样器缺失则整体失败（调用方已兜底默认白纹理，不应发生）
			if (target == VK_NULL_HANDLE || view.sampler == VK_NULL_HANDLE || view.albedo == VK_NULL_HANDLE
				|| view.normal == VK_NULL_HANDLE || view.roughness == VK_NULL_HANDLE || view.emissive == VK_NULL_HANDLE
				|| view.ao == VK_NULL_HANDLE || view.metallic == VK_NULL_HANDLE || view.shadow == VK_NULL_HANDLE
				|| view.sky == VK_NULL_HANDLE
				|| view.spot_shadow[0] == VK_NULL_HANDLE || view.spot_shadow[1] == VK_NULL_HANDLE
				|| view.spot_shadow[2] == VK_NULL_HANDLE || view.spot_shadow[3] == VK_NULL_HANDLE
				|| view.spot_shadow[4] == VK_NULL_HANDLE || view.spot_shadow[5] == VK_NULL_HANDLE
				|| view.spot_shadow[6] == VK_NULL_HANDLE || view.spot_shadow[7] == VK_NULL_HANDLE)
			{
				logger.error("管线:: 逐绘制项采样集批量写入的第 {} 条参数不完整", index);
				return false;
			}
			//十七条描述符的图像描述（binding 2 = 场景深度，未提供时以反照率视图占位，场景着色器不采样它）
			const VkImageView depth_view = view.depth != VK_NULL_HANDLE ? view.depth : view.albedo;
			const VkSampler depth_sampler = view.depth_sampler != VK_NULL_HANDLE ? view.depth_sampler : view.sampler;
			const VkImageView images[descriptor_count] = { view.albedo, view.shadow, depth_view, view.normal,
				view.roughness, view.sky, view.emissive, view.ao, view.metallic,
				view.spot_shadow[0], view.spot_shadow[1], view.spot_shadow[2], view.spot_shadow[3],
				view.spot_shadow[4], view.spot_shadow[5], view.spot_shadow[6], view.spot_shadow[7] };
			//图像描述（每项 17 条）
			const std::size_t info_base = static_cast<std::size_t>(index) * descriptor_count;
			for (uint32_t descriptor = 0; descriptor < descriptor_count; ++descriptor)
			{
				draw_batch_infos[info_base + descriptor].sampler = descriptor == 2 ? depth_sampler : view.sampler;
				draw_batch_infos[info_base + descriptor].imageView = images[descriptor];
				draw_batch_infos[info_base + descriptor].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			//写记录（每项 10 条：binding 0..8 单值、binding 9 数组）
			const std::size_t record_base = static_cast<std::size_t>(index) * record_count;
			for (uint32_t binding = 0; binding < 9; ++binding)
			{
				draw_batch_writes[record_base + binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				draw_batch_writes[record_base + binding].dstSet = target;
				draw_batch_writes[record_base + binding].dstBinding = binding;
				draw_batch_writes[record_base + binding].dstArrayElement = 0;
				draw_batch_writes[record_base + binding].descriptorCount = 1;
				draw_batch_writes[record_base + binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				draw_batch_writes[record_base + binding].pImageInfo = &draw_batch_infos[info_base + binding];
			}
			//binding 9：8 个数组元素一次写入
			draw_batch_writes[record_base + 9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			draw_batch_writes[record_base + 9].dstSet = target;
			draw_batch_writes[record_base + 9].dstBinding = 9;
			draw_batch_writes[record_base + 9].dstArrayElement = 0;
			draw_batch_writes[record_base + 9].descriptorCount = 8;
			draw_batch_writes[record_base + 9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			draw_batch_writes[record_base + 9].pImageInfo = &draw_batch_infos[info_base + 9];
		}
		//一次提交全部条目
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(draw_batch_writes.size()), draw_batch_writes.data(), 0, nullptr);
		return true;
	}

	bool Pipeline_Context::material_textures_bind(uint32_t slot, VkSampler sampler,
		VkImageView normal_view, VkImageView roughness_view, VkImageView emissive_view,
		VkImageView ao_view, VkImageView metallic_view)
	{
		//前置：帧槽范围、采样器与五个视图齐备
		if (device == VK_NULL_HANDLE || slot >= sampler_sets.size() || sampler_sets[slot] == VK_NULL_HANDLE
			|| sampler == VK_NULL_HANDLE
			|| normal_view == VK_NULL_HANDLE || roughness_view == VK_NULL_HANDLE
			|| emissive_view == VK_NULL_HANDLE || ao_view == VK_NULL_HANDLE || metallic_view == VK_NULL_HANDLE)
		{
			logger.error("管线:: 材质贴图批量绑定参数不完整（帧槽 {}）", slot);
			return false;
		}

		//五个绑定编号（与场景片元声明一致）
		const uint32_t bindings[5] = { 3, 4, 6, 7, 8 };
		//五个视图（与绑定编号一一对应）
		const VkImageView views[5] = { normal_view, roughness_view, emissive_view, ao_view, metallic_view };
		//图像描述与描述符写（共用同一采样器与终态布局）
		VkDescriptorImageInfo image_infos[5]{};
		VkWriteDescriptorSet writes[5]{};
		for (std::size_t index = 0; index < 5; ++index)
		{
			image_infos[index].sampler = sampler;
			image_infos[index].imageView = views[index];
			image_infos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[index].dstSet = sampler_sets[slot];
			writes[index].dstBinding = bindings[index];
			writes[index].dstArrayElement = 0;
			writes[index].descriptorCount = 1;
			writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[index].pImageInfo = &image_infos[index];
		}

		//一次更新五个描述符
		vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
		return true;
	}
}
