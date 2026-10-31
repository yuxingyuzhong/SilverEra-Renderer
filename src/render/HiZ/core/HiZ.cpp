#include "src/render/HiZ/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像创建 / 销毁依赖它）
#include "src/render/Memory/内存上下文.h"
//计算上下文完整定义（第二百五十四轮：描述符集与调度）
#include "src/render/Compute/Compute.h"

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

		//录制单张图像的布局转换（同步 2 口径；级范围由调用方给出）
		void image_barrier_record(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect,
			VkImageLayout old_layout, VkImageLayout new_layout,
			VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
			VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
			uint32_t base_mip_level = 0, uint32_t level_count = 1)
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = src_stage;
			barrier.srcAccessMask = src_access;
			barrier.dstStageMask = dst_stage;
			barrier.dstAccessMask = dst_access;
			barrier.oldLayout = old_layout;
			barrier.newLayout = new_layout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = { aspect, base_mip_level, level_count, 0, 1 };
			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			//提交该图像的布局转换
			vkCmdPipelineBarrier2(command_buffer, &dependency);
		}

		//按纹素数换算工作组数（本地尺寸固定 8×8）
		uint32_t group_count(uint32_t elements)
		{
			return (elements + 7) / 8;
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
		this->generation_total = 0;
		/*
		深度采样器（第二百五十四轮）
		最近邻 ＋ 钳边 ＋ 点 mip：深度拷贝按纹素取值，任何线性过滤都会把边界深度拉向更近值而破坏保守性。
		*/
		{
			VkSamplerCreateInfo sampler_info{};
			sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler_info.magFilter = VK_FILTER_NEAREST;
			sampler_info.minFilter = VK_FILTER_NEAREST;
			sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler_info.maxLod = 0.0f;
			//创建采样器
			if (vkCreateSampler(device, &sampler_info, nullptr, &depth_sampler) != VK_SUCCESS)
			{
				logger.error("HiZ:: 深度采样器创建失败");
				depth_sampler = VK_NULL_HANDLE;
				return false;
			}
		}
		//逐槽创建资源（任一失败整体回滚）
		slots.resize(frame_overlap);
		for (uint32_t slot = 0; slot < frame_overlap; ++slot)
		{
			//创建该槽的历史与金字塔
			if (!slot_create(slots[slot]))
			{
				logger.error("HiZ:: 帧槽 {} 资源创建失败", slot);
				slots_destroy();
				if (depth_sampler != VK_NULL_HANDLE)
					vkDestroySampler(device, depth_sampler, nullptr);
				depth_sampler = VK_NULL_HANDLE;
				return false;
			}
		}
		logger.info("HiZ:: 资源已建立（{}×{}，级数 {}，槽数 {}，场景深度格式 {}）",
			width, height, levels, frame_overlap, static_cast<int32_t>(depth_format));
		return true;
	}

	//销毁（幂等）
	void HiZ_Context::destroy(void)
	{
		slots_destroy();
		//采样器最后销毁
		if (depth_sampler != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
			vkDestroySampler(device, depth_sampler, nullptr);
		depth_sampler = VK_NULL_HANDLE;
		depth_format = VK_FORMAT_UNDEFINED;
		width = 0;
		height = 0;
		levels = 0;
		generation_total = 0;
		compute = nullptr;
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

	//搭建计算调度关联
	void HiZ_Context::compute_set(Compute_Context* compute)
	{
		this->compute = compute;
	}

	/*
	生成金字塔（第二百五十四轮）
	级 0 = 全分辨率（调用同尺寸降采样：取 (2x,2x+1)×(2y,2y+1) 邻域最大值，略偏保守）；
	级 i = 对级 i-1 取 2×2 最大值（保守：取最远遮挡深度）。全部图像以 GENERAL 布局读写，
	生成后保持 GENERAL（采样读写均合法）。场景深度在生成前后做「深度附件 ↔ 着色器可读」往返转换。
	*/
	bool HiZ_Context::pyramid_generate(VkCommandBuffer command_buffer, uint32_t slot, VkImage depth_image, VkImageView depth_view,
		uint64_t depth_copy_pipeline, uint64_t downsample_pipeline)
	{
		//前置：命令缓冲 / 深度 / 槽位 / 计算关联 / 管线句柄
		if (command_buffer == VK_NULL_HANDLE || depth_image == VK_NULL_HANDLE || depth_view == VK_NULL_HANDLE
			|| slot >= slots.size() || !valid() || compute == nullptr
			|| depth_copy_pipeline == 0 || downsample_pipeline == 0)
			return false;
		Slot& target = slots[slot];
		//切换计算帧槽（描述符集按槽）
		if (!compute->slot_set(slot))
			return false;
		/*
		场景深度转着色器可读
		上一 pass 的深度写入归 late fragment tests；生成在计算阶段读取。
		*/
		image_barrier_record(command_buffer, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
		//历史图像准备为存储写（旧内容整幅覆盖）
		image_barrier_record(command_buffer, target.history_image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			0, 0,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
		//绑定深度采样器与历史存储图像
		if (!compute->sampler_bind(slot, depth_sampler, depth_view)
			|| !compute->storage_image_bind(slot, 0, target.history_view))
			return false;
		//全分辨率深度拷贝（采样深度 → 历史）
		{
			uint32_t push[4] = { width, height, width, height };
			if (!compute->compute_dispatch(command_buffer, depth_copy_pipeline,
				group_count(width), group_count(height), 1, push, static_cast<uint32_t>(sizeof(push))))
				return false;
		}
		//历史写 → 读（供第 0 级降采样读取）
		if (!compute->barrier(command_buffer, Compute_Barrier::ComputeToCompute))
			return false;
		/*
		逐级生成
		第 0 级：源 = 历史（同尺寸 ⇒ 边界钳制后退化为逐纹素拷贝）；第 i 级：源 = 第 i-1 级视图。
		*/
		uint32_t source_width = width;
		uint32_t source_height = height;
		for (uint32_t level = 0; level < levels; ++level)
		{
			//目标级尺寸
			uint32_t dest_width = 0;
			uint32_t dest_height = 0;
			if (!level_extent_get(level, dest_width, dest_height))
				return false;
			//目标级准备为存储写（旧内容整幅覆盖）
			image_barrier_record(command_buffer, target.pyramid_image, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				0, 0,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
				level, 1);
			//源视图（第 0 级取历史，其余取上一级）
			VkImageView source_view = level == 0 ? target.history_view : target.level_views[level - 1];
			//绑定源与目标存储图像
			if (!compute->storage_image_bind(slot, 0, source_view)
				|| !compute->storage_image_bind(slot, 1, target.level_views[level]))
				return false;
			//降采样（取 2×2 最大值）
			uint32_t push[4] = { source_width, source_height, dest_width, dest_height };
			if (!compute->compute_dispatch(command_buffer, downsample_pipeline,
				group_count(dest_width), group_count(dest_height), 1, push, static_cast<uint32_t>(sizeof(push))))
				return false;
			//本级的写 → 下一级的读
			if (!compute->barrier(command_buffer, Compute_Barrier::ComputeToCompute))
				return false;
			source_width = dest_width;
			source_height = dest_height;
		}
		/*
		场景深度恢复深度附件布局
		供同帧后续通道（UI / 后处理退出转换）继续以 DEPTH_ATTACHMENT_OPTIMAL 挂载。
		*/
		image_barrier_record(command_buffer, depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
		//标记有效并累计
		target.history_valid = true;
		++generation_total;
		return true;
	}

	//累计金字塔生成次数
	uint64_t HiZ_Context::generation_count(void) const
	{
		return generation_total;
	}

	//创建单个槽的全部资源
	bool HiZ_Context::slot_create(Slot& slot)
	{
		/*
		深度历史图像（第二百五十四轮改为 R32_SFLOAT 颜色面）
		深度拷贝 compute 以存储图像写入，故须 STORAGE；同时保持 SAMPLED 以便后续遮挡查询直接采样。
		*/
		if (!memory->image_create(slot.history_image, slot.history_allocation, width, height,
			HIZ_PYRAMID_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			return false;
		//历史视图（颜色面）
		if (!memory->image_view_create(slot.history_view, slot.history_image, HIZ_PYRAMID_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT))
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
