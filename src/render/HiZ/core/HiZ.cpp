#include "src/render/HiZ/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像创建 / 销毁依赖它）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	namespace
	{
		/*
		按尺寸换算金字塔级数
		自 1 级起逐级折半（宽 / 高各自不小于 1），直至 1×1 或达到 HIZ_LEVEL_MAX。
		*/
		uint32_t level_count_from(uint32_t width, uint32_t height)
		{
			//当前级尺寸（自 0 级起折半）
			uint32_t current_width = width;
			uint32_t current_height = height;
			uint32_t levels = 1;
			//宽或高仍大于 1 且未达上限
			while ((current_width > 1 || current_height > 1) && levels < HIZ_LEVEL_MAX)
			{
				//宽高各自折半（不小于 1）
				current_width = current_width > 1 ? current_width / 2 : 1;
				current_height = current_height > 1 ? current_height / 2 : 1;
				++levels;
			}
			return levels;
		}
	}

	//析构（兜底销毁）
	HiZ_Context::~HiZ_Context(void)
	{
		destroy();
	}

	//构建
	bool HiZ_Context::build(VkDevice device, Memory_Context& memory, uint32_t frame_overlap,
		uint32_t width, uint32_t height, VkFormat depth_format)
	{
		//前置：设备 / 内存模块 / 槽数 / 尺寸 / 格式齐备
		if (device == VK_NULL_HANDLE || !memory.valid() || frame_overlap == 0
			|| width == 0 || height == 0 || depth_format == VK_FORMAT_UNDEFINED)
		{
			logger.error("HiZ:: 构建参数不完整（槽数 {}，尺寸 {}×{}）", frame_overlap, width, height);
			return false;
		}
		this->device = device;
		this->memory = &memory;
		this->depth_format = depth_format;
		this->width = width;
		this->height = height;
		this->levels = level_count_from(width, height);
		this->copy_count = 0;
		//逐槽创建资源（任一失败整体回滚）
		slots.resize(frame_overlap);
		for (uint32_t slot = 0; slot < frame_overlap; ++slot)
		{
			//创建该槽的历史与金字塔
			if (!slot_create(slots[slot]))
			{
				logger.error("HiZ:: 帧槽 {} 资源创建失败", slot);
				slots_destroy();
				return false;
			}
		}
		logger.info("HiZ:: 资源已建立（{}×{}，级数 {}，槽数 {}，深度格式 {}）",
			width, height, levels, frame_overlap, static_cast<int32_t>(depth_format));
		return true;
	}

	//销毁（幂等）
	void HiZ_Context::destroy(void)
	{
		slots_destroy();
		depth_format = VK_FORMAT_UNDEFINED;
		width = 0;
		height = 0;
		levels = 0;
		copy_count = 0;
		memory = nullptr;
		device = VK_NULL_HANDLE;
	}

	//是否已构建完成
	bool HiZ_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && memory != nullptr && !slots.empty()
			&& width > 0 && height > 0 && levels > 0;
	}

	//尺寸 / 格式设置（变化时重建）
	bool HiZ_Context::extent_set(uint32_t width, uint32_t height, VkFormat depth_format)
	{
		//前置：上下文已构建
		if (!valid())
			return false;
		//尺寸与格式完全一致则无需重建
		if (width == this->width && height == this->height && depth_format == this->depth_format)
			return true;
		//原槽数（销毁后仍需按此重建）
		uint32_t overlap = static_cast<uint32_t>(slots.size());
		//重建前先销毁旧槽（新尺寸可能更小）
		slots_destroy();
		this->depth_format = depth_format;
		this->width = width;
		this->height = height;
		this->levels = level_count_from(width, height);
		//按原槽数重建
		slots.resize(overlap);
		for (uint32_t slot = 0; slot < overlap; ++slot)
		{
			//重建失败即整体回滚
			if (!slot_create(slots[slot]))
			{
				logger.error("HiZ:: 尺寸变更后帧槽 {} 重建失败", slot);
				slots_destroy();
				return false;
			}
		}
		logger.info("HiZ:: 资源已重建（{}×{}，级数 {}）", width, height, levels);
		return true;
	}

	//当前宽度
	uint32_t HiZ_Context::width_get(void) const
	{
		return width;
	}

	//当前高度
	uint32_t HiZ_Context::height_get(void) const
	{
		return height;
	}

	//当前深度格式
	VkFormat HiZ_Context::depth_format_get(void) const
	{
		return depth_format;
	}

	//金字塔级数
	uint32_t HiZ_Context::level_count(void) const
	{
		return levels;
	}

	//第 level 级尺寸
	bool HiZ_Context::level_extent_get(uint32_t level, uint32_t& width_out, uint32_t& height_out) const
	{
		//出参先归零
		width_out = 0;
		height_out = 0;
		//越界或未构建
		if (!valid() || level >= levels)
			return false;
		//逐级折半（不小于 1）
		width_out = (std::max)(1u, width >> level);
		height_out = (std::max)(1u, height >> level);
		return true;
	}

	//帧槽数量
	uint32_t HiZ_Context::slot_count(void) const
	{
		return static_cast<uint32_t>(slots.size());
	}

	//深度历史图像
	VkImage HiZ_Context::depth_history_image(uint32_t slot) const
	{
		//槽越界
		if (slot >= slots.size())
			return VK_NULL_HANDLE;
		return slots[slot].history_image;
	}

	//深度历史视图
	VkImageView HiZ_Context::depth_history_view(uint32_t slot) const
	{
		//槽越界
		if (slot >= slots.size())
			return VK_NULL_HANDLE;
		return slots[slot].history_view;
	}

	//金字塔图像
	VkImage HiZ_Context::pyramid_image(uint32_t slot) const
	{
		//槽越界
		if (slot >= slots.size())
			return VK_NULL_HANDLE;
		return slots[slot].pyramid_image;
	}

	//金字塔逐级视图
	VkImageView HiZ_Context::pyramid_level_view(uint32_t slot, uint32_t level) const
	{
		//槽或级越界
		if (slot >= slots.size() || level >= slots[slot].level_views.size())
			return VK_NULL_HANDLE;
		return slots[slot].level_views[level];
	}

	//该槽是否已有有效历史
	bool HiZ_Context::history_valid(uint32_t slot) const
	{
		//槽越界视为无历史
		if (slot >= slots.size())
			return false;
		return slots[slot].history_valid;
	}

	//作废该槽历史
	void HiZ_Context::history_invalidate(uint32_t slot)
	{
		//槽越界即忽略
		if (slot >= slots.size())
			return;
		slots[slot].history_valid = false;
	}

	/*
	录制「场景深度 → 深度历史」的拷贝
	源深度在布局转换前须已完成写入并处于 source_layout_before；拷贝结束后恢复到 source_layout_after，
	供后续通道继续作深度附件挂载。历史纹理每次整幅覆盖，故其旧布局统一取 UNDEFINED（内容被丢弃）。
	*/
	bool HiZ_Context::history_copy_record(VkCommandBuffer command_buffer, uint32_t slot, VkImage source_image,
		VkImageLayout source_layout_before, VkImageLayout source_layout_after)
	{
		//前置：命令缓冲 / 源图像 / 槽位合法
		if (command_buffer == VK_NULL_HANDLE || source_image == VK_NULL_HANDLE
			|| slot >= slots.size() || !valid())
			return false;
		Slot& target = slots[slot];
		/*
		源与历史格式须一致（vkCmdCopyImage 不允许跨格式）
		历史纹理按构建时的深度格式创建，调用方须传入同格式的源深度（不一致时由调用方先行跳过本辅助）。
		*/
		/*
		源布局转 TRANSFER_SRC
		源访问为深度附件写入（上一 pass 已结束），故 srcStage 取 late fragment tests。
		*/
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			barrier.oldLayout = source_layout_before;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = source_image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			//提交源图像布局转换
			vkCmdPipelineBarrier2(command_buffer, &dependency);
		}
		/*
		历史图像转 TRANSFER_DST
		上一帧的采样读入 sync；旧布局 UNDEFINED 表示内容丢弃（本帧整幅覆盖）。
		*/
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = target.history_image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			//提交历史图像布局转换
			vkCmdPipelineBarrier2(command_buffer, &dependency);
		}
		//整幅拷贝深度面（第 0 级、单层）
		VkImageCopy region{};
		region.srcSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
		region.dstSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
		region.extent = { width, height, 1 };
		vkCmdCopyImage(command_buffer, source_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target.history_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		/*
		历史图像转 SHADER_READ_ONLY
		供 254 轮的金字塔生成 compute 与后续遮挡查询采样读取。
		*/
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = target.history_image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			//提交历史图像布局转换
			vkCmdPipelineBarrier2(command_buffer, &dependency);
		}
		//源图像恢复调用方布局（供后续 pass 继续作为深度附件挂载）
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = source_layout_after;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = source_image;
			barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			//提交源图像恢复转换
			vkCmdPipelineBarrier2(command_buffer, &dependency);
		}
		//标记历史有效并累计次数
		target.history_valid = true;
		++copy_count;
		return true;
	}

	//累计深度拷贝次数
	uint64_t HiZ_Context::history_copy_count(void) const
	{
		return copy_count;
	}

	//创建单个槽的全部资源
	bool HiZ_Context::slot_create(Slot& slot)
	{
		/*
		深度历史图像
		与场景深度同格式、单采样、单级；用途含 TRANSFER_DST（逐帧拷贝）与 SAMPLED（后续采样）。
		*/
		if (!memory->image_create(slot.history_image, slot.history_allocation, width, height,
			depth_format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
			return false;
		//历史视图（深度面）
		if (!memory->image_view_create(slot.history_view, slot.history_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
		{
			slot_destroy(slot);
			return false;
		}
		/*
		金字塔图像
		单通道浮点、完整 mip 链；用途含 STORAGE（逐级写入）与 SAMPLED（遮挡查询采样）。
		*/
		if (!memory->image_create(slot.pyramid_image, slot.pyramid_allocation, width, height,
			HIZ_PYRAMID_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, levels))
		{
			slot_destroy(slot);
			return false;
		}
		//逐级视图（baseMipLevel = 级号，级数 1）
		slot.level_views.resize(levels, VK_NULL_HANDLE);
		for (uint32_t level = 0; level < levels; ++level)
		{
			//创建该级视图
			if (!memory->image_view_create(slot.level_views[level], slot.pyramid_image, HIZ_PYRAMID_FORMAT,
				VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D, 1, level))
			{
				slot_destroy(slot);
				return false;
			}
		}
		slot.history_valid = false;
		return true;
	}

	//销毁单个槽的全部资源（幂等）
	void HiZ_Context::slot_destroy(Slot& slot)
	{
		//逐级视图
		for (VkImageView& view : slot.level_views)
		{
			if (view != VK_NULL_HANDLE)
				memory->image_view_destroy(view);
			view = VK_NULL_HANDLE;
		}
		slot.level_views.clear();
		//历史视图与金字塔图像 / 历史图像
		if (slot.history_view != VK_NULL_HANDLE)
			memory->image_view_destroy(slot.history_view);
		slot.history_view = VK_NULL_HANDLE;
		if (slot.pyramid_image != VK_NULL_HANDLE)
			memory->image_destroy(slot.pyramid_image, slot.pyramid_allocation);
		slot.pyramid_image = VK_NULL_HANDLE;
		slot.pyramid_allocation = VK_NULL_HANDLE;
		if (slot.history_image != VK_NULL_HANDLE)
			memory->image_destroy(slot.history_image, slot.history_allocation);
		slot.history_image = VK_NULL_HANDLE;
		slot.history_allocation = VK_NULL_HANDLE;
		slot.history_valid = false;
	}

	//销毁全部槽并清空尺寸状态
	void HiZ_Context::slots_destroy(void)
	{
		//内存模块已失效时只清句柄（避免访问已销毁的分配器）
		if (memory == nullptr || !memory->valid())
		{
			slots.clear();
			return;
		}
		for (Slot& slot : slots)
			slot_destroy(slot);
		slots.clear();
	}
}
