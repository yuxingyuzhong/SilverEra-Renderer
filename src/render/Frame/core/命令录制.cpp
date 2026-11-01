#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	间接绘制命令（第二百五十二轮）
	字段顺序与 VkDrawIndexedIndirectCommand 一致（共 20 字节）。
	*/
	struct Indirect_Draw_Command
	{
		uint32_t index_count;
		uint32_t instance_count;
		uint32_t first_index;
		int32_t vertex_offset;
		uint32_t first_instance;
	};

	//推送常量逐字节比较（长度为零视为相等）
	static bool push_constants_equal(const Frame_Render_Item& first, const Frame_Render_Item& next)
	{
		//长度或偏移不同则不可合并
		if (first.push_constant_offset != next.push_constant_offset || first.push_constant_size != next.push_constant_size)
			return false;
		//无推送常量
		if (first.push_constant_size == 0)
			return true;
		//任一为空指针
		if (first.push_constants == nullptr || next.push_constants == nullptr)
			return false;
		const unsigned char* first_bytes = static_cast<const unsigned char*>(first.push_constants);
		const unsigned char* next_bytes = static_cast<const unsigned char*>(next.push_constants);
		for (uint32_t index = 0; index < first.push_constant_size; ++index)
			if (first_bytes[index] != next_bytes[index])
				return false;
		return true;
	}

	/*
	GPU 侧间接命令的可见性屏障（第三百四十二轮）
	命令缓冲由计算着色器在**前置提交**中写入，本帧提交读取的是同一队列上的后续提交；
	批内存在 gpu_indirect 项时在录制开始处（动态渲染之外）插入一次性内存屏障，
	把「计算写入」置为对「间接命令读取」可见（无此类项时不插入，既有路径逐字节不变）。
	*/
	static void indirect_command_sync(VkCommandBuffer command_buffer, const Frame_Render_Item* items, uint32_t item_count)
	{
		//批内是否存在 GPU 侧间接命令项
		bool needed = false;
		for (uint32_t index = 0; index < item_count; ++index)
			if (items[index].gpu_indirect)
			{
				needed = true;
				break;
			}
		//无此类项不插入屏障
		if (!needed)
			return;
		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	/*
	相邻渲染项是否可合并为一条间接命令（第二百五十二轮）
	条件：管线 / 布局 / 描述符集 / 推送常量 / 顶点、索引、实例缓冲与索引区一致，
	且下一项的实例区紧跟上一段末尾（同一实例缓冲内连续），合并后以 first_instance 表达起点。
	*/
	static bool indirect_items_mergeable(const Frame_Render_Item& first, const Frame_Render_Item& next,
		VkDeviceSize next_expected_offset)
	{
		if (first.pipeline != next.pipeline || first.layout != next.layout)
			return false;
		if (first.descriptor_set_count != next.descriptor_set_count)
			return false;
		if (first.scene_set != next.scene_set || first.sampler_set != next.sampler_set)
			return false;
		if (!push_constants_equal(first, next))
			return false;
		if (first.vertex_buffer != next.vertex_buffer || first.index_buffer != next.index_buffer)
			return false;
		if (first.index_offset != next.index_offset || first.index_count != next.index_count)
			return false;
		//不使用实例属性的项不参与实例区合并（第三百四十一轮）
		if (!first.instance_input || !next.instance_input)
			return false;
		if (first.instance_buffer != next.instance_buffer)
			return false;
		return next.instance_offset == next_expected_offset;
	}

	/*
	逐渲染项绘制（带状态缓存）
	本层每帧各渲染项的 set0 / set1 描述符集都是「本帧槽」的同一组值（门面按 slot 统一填装），
	而各渲染项**可挂不同图形管线**（多材质 / 多管线），故管线与描述符集只在发生变化时重绑，
	以减少 GPU 状态切换（K 优化）。推送常量与顶点 / 索引 / 实例缓冲各网格不同，仍须逐项绑定。
	由 command_record（交换链路径）与 command_record_target（离屏路径）共用。
	★ 第十一轮：返回**实际绘制**的项数与实例数（不完整项被跳过，故可能小于入参 item_count），
	  供帧统计区分「登记量」与「实际绘制量」（处置 R-FRAME-10 / R-FACADE-09）。
	*/
	Frame_Context::Recorded_Counts Frame_Context::items_record(VkCommandBuffer command_buffer,
		const Frame_Render_Item* items, uint32_t item_count)
	{
		//本批实际绘制的项数与实例数
		Recorded_Counts counts{};
		//当前已绑定的图形管线（初始为空）
		VkPipeline bound_pipeline = VK_NULL_HANDLE;
		//当前已绑定的管线布局
		VkPipelineLayout bound_layout = VK_NULL_HANDLE;
		//当前已绑定的描述符集数量
		uint32_t bound_set_count = 0;
		//当前已绑定的描述符集（set0 在前，set1 在后）
		VkDescriptorSet bound_sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		/*
		间接录制（第二百五十二轮）
		就近连续且状态一致的渲染项合并为一条 vkCmdDrawIndexedIndirect：
		合并段以实例缓冲偏移 0 绑定、由 first_instance 承担起点语义，与逐项绑定后 firstInstance=0 逐字节等价。
		仅在映射可用、容量足够容纳本批全部命令且非并行分段录制时启用；否则整体回落逐项直接绘制。
		*/
		//批内存在「不使用实例属性」的项时整体回落逐项路径（第三百四十一轮）
		bool indirect_compatible = true;
		for (uint32_t index = 0; index < item_count; ++index)
			if (!items[index].instance_input)
			{
				indirect_compatible = false;
				break;
			}
		const bool indirect_ready = indirect_mapped != nullptr && indirect_buffer != VK_NULL_HANDLE
			&& indirect_capacity > 1 && active_secondary_count <= 1 && indirect_compatible;
		if (indirect_ready)
		{
			//预扫描本批命令条数（不完整项跳过）
			uint32_t planned_commands = 0;
			for (uint32_t index = 0; index < item_count;)
			{
				const Frame_Render_Item& item = items[index];
				//不完整项跳过
				if (item.pipeline == VK_NULL_HANDLE || item.vertex_buffer == VK_NULL_HANDLE
					|| item.index_buffer == VK_NULL_HANDLE || item.index_count == 0
					|| item.instance_buffer == VK_NULL_HANDLE || item.instance_count == 0)
				{
					++index;
					continue;
				}
				//合并起点之后的首个期望实例偏移
				VkDeviceSize expected_offset = item.instance_offset
					+ static_cast<VkDeviceSize>(item.instance_count) * indirect_instance_stride;
				++index;
				//向后吞并连续可合并项
				while (index < item_count)
				{
					const Frame_Render_Item& next = items[index];
					//不完整项终止合并
					if (next.pipeline == VK_NULL_HANDLE || next.vertex_buffer == VK_NULL_HANDLE
						|| next.index_buffer == VK_NULL_HANDLE || next.index_count == 0
						|| next.instance_buffer == VK_NULL_HANDLE || next.instance_count == 0)
						break;
					//状态或实例区不连续则终止合并
					if (!indirect_items_mergeable(item, next, expected_offset))
						break;
					expected_offset += static_cast<VkDeviceSize>(next.instance_count) * indirect_instance_stride;
					++index;
				}
				++planned_commands;
			}
			//容量不足则整体回落（不与逐项路径混用，保持行为一致）
			if (frame_indirect_commands + planned_commands <= indirect_capacity)
			{
				Indirect_Draw_Command* commands = static_cast<Indirect_Draw_Command*>(indirect_mapped);
				uint32_t index = 0;
				while (index < item_count)
				{
					const Frame_Render_Item& item = items[index];
					//不完整项跳过
					if (item.pipeline == VK_NULL_HANDLE || item.vertex_buffer == VK_NULL_HANDLE
						|| item.index_buffer == VK_NULL_HANDLE || item.index_count == 0
						|| item.instance_buffer == VK_NULL_HANDLE || item.instance_count == 0)
					{
						++index;
						continue;
					}
					//本段合并的实例数与本段项数
					uint32_t merged_instances = item.instance_count;
					uint32_t merged_items = 1;
					VkDeviceSize expected_offset = item.instance_offset
						+ static_cast<VkDeviceSize>(item.instance_count) * indirect_instance_stride;
					++index;
					//向后吞并连续可合并项
					while (index < item_count)
					{
						const Frame_Render_Item& next = items[index];
						//不完整项终止合并
						if (next.pipeline == VK_NULL_HANDLE || next.vertex_buffer == VK_NULL_HANDLE
							|| next.index_buffer == VK_NULL_HANDLE || next.index_count == 0
							|| next.instance_buffer == VK_NULL_HANDLE || next.instance_count == 0)
							break;
						//状态或实例区不连续则终止合并
						if (!indirect_items_mergeable(item, next, expected_offset))
							break;
						merged_instances += next.instance_count;
						++merged_items;
						expected_offset += static_cast<VkDeviceSize>(next.instance_count) * indirect_instance_stride;
						++index;
					}

					//仅在与当前已绑定管线不同时重绑（同帧各渲染项可挂不同管线）
					if (item.pipeline != bound_pipeline)
					{
						//绑定图形管线
						vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline);
						bound_pipeline = item.pipeline;
						//管线变化使「已绑定布局 / 描述符集」失效，一并复位
						bound_layout = VK_NULL_HANDLE;
						bound_set_count = 0;
						bound_sets[0] = VK_NULL_HANDLE;
						bound_sets[1] = VK_NULL_HANDLE;
					}

					//若存在可绑定描述符集
					if (item.descriptor_set_count > 0)
					{
						//仅在与当前已绑定（布局 + 数量 + 集合）不同时重绑
						if (item.layout != bound_layout || item.descriptor_set_count != bound_set_count
							|| item.scene_set != bound_sets[0] || item.sampler_set != bound_sets[1])
						{
							//待绑定描述符集数组（set0 在前，set1 在后）
							VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
							//绑定描述符集
							vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
								0, item.descriptor_set_count, sets, 0, nullptr);
							//记录当前已绑定的布局与描述符集
							bound_layout = item.layout;
							bound_set_count = item.descriptor_set_count;
							bound_sets[0] = item.scene_set;
							bound_sets[1] = item.sampler_set;
						}
					}

					//若存在推送常量
					if (item.push_constants != nullptr && item.push_constant_size > 0)
						//推送常量到顶点与片元阶段（偏移由绘制项给出）
						vkCmdPushConstants(command_buffer, item.layout,
							VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							item.push_constant_offset, item.push_constant_size, item.push_constants);

					//绑定顶点与实例缓冲（实例偏移由命令的 first_instance 表达，故绑定偏移恒 0）
					VkBuffer vertex_buffers[2] = { item.vertex_buffer, item.instance_buffer };
					VkDeviceSize vertex_offsets[2] = { 0, 0 };
					vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, vertex_offsets);
					//绑定索引缓冲
					vkCmdBindIndexBuffer(command_buffer, item.index_buffer, item.index_offset, VK_INDEX_TYPE_UINT32);

					//写入合并命令（实例区起点换算为实例序号）
					Indirect_Draw_Command& command = commands[frame_indirect_commands];
					command.index_count = item.index_count;
					command.instance_count = merged_instances;
					command.first_index = 0;
					command.vertex_offset = 0;
					command.first_instance = static_cast<uint32_t>(item.instance_offset / indirect_instance_stride);
					const VkDeviceSize command_offset = static_cast<VkDeviceSize>(frame_indirect_commands)
						* static_cast<VkDeviceSize>(sizeof(Indirect_Draw_Command));
					//下达间接绘制（单条命令）
					vkCmdDrawIndexedIndirect(command_buffer, indirect_buffer, command_offset, 1,
						static_cast<uint32_t>(sizeof(Indirect_Draw_Command)));
					++frame_indirect_commands;
					++total_indirect_draws;
					//累计本段实际绘制量
					counts.items += merged_items;
					counts.instances += merged_instances;
				}
				return counts;
			}
		}

		for (uint32_t index = 0; index < item_count; ++index)
		{
			//当前渲染项
			const Frame_Render_Item& item = items[index];
			//管线或顶点 / 索引缓冲缺失则跳过该项；使用实例属性时实例缓冲亦必填，GPU 侧间接项须有命令缓冲（第三百四十一 / 三百四十二轮）
			if (item.pipeline == VK_NULL_HANDLE || item.vertex_buffer == VK_NULL_HANDLE
				|| item.index_buffer == VK_NULL_HANDLE || item.index_count == 0
				|| item.instance_count == 0
				|| (item.instance_input && item.instance_buffer == VK_NULL_HANDLE)
				|| (item.gpu_indirect && item.indirect_command_buffer == VK_NULL_HANDLE))
				continue;

			//仅在与当前已绑定管线不同时重绑（同帧各渲染项可挂不同管线）
			if (item.pipeline != bound_pipeline)
			{
				//绑定图形管线
				vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline);
				bound_pipeline = item.pipeline;
				//管线变化使「已绑定布局 / 描述符集」失效，一并复位
				bound_layout = VK_NULL_HANDLE;
				bound_set_count = 0;
				bound_sets[0] = VK_NULL_HANDLE;
				bound_sets[1] = VK_NULL_HANDLE;
			}

			//若存在可绑定描述符集
			if (item.descriptor_set_count > 0)
			{
				//仅在与当前已绑定（布局 + 数量 + 集合）不同时重绑
				if (item.layout != bound_layout || item.descriptor_set_count != bound_set_count
					|| item.scene_set != bound_sets[0] || item.sampler_set != bound_sets[1])
				{
					//待绑定描述符集数组（set0 在前，set1 在后）
					VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
					//绑定描述符集
					vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
						0, item.descriptor_set_count, sets, 0, nullptr);
					//记录当前已绑定的布局与描述符集
					bound_layout = item.layout;
					bound_set_count = item.descriptor_set_count;
					bound_sets[0] = item.scene_set;
					bound_sets[1] = item.sampler_set;
				}
			}

			//若存在推送常量
			if (item.push_constants != nullptr && item.push_constant_size > 0)
				//推送常量到顶点与片元阶段（偏移由绘制项给出：文字 0 / 后处理尺寸 80）
				vkCmdPushConstants(command_buffer, item.layout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					item.push_constant_offset, item.push_constant_size, item.push_constants);

			/*
			绑定顶点与实例缓冲（第二百一十一轮：binding 0 与 binding 1 合并为一次调用）
			实例缓冲承载逐实例模型矩阵；实例数为 1 时即「逐次绘制」的等价路径。
			第三百四十一轮：`instance_input` 为假的项（管线不消费实例属性）只绑定 binding 0。
			*/
			if (item.instance_input)
			{
				VkBuffer vertex_buffers[2] = { item.vertex_buffer, item.instance_buffer };
				VkDeviceSize vertex_offsets[2] = { 0, item.instance_offset };
				vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_buffers, vertex_offsets);
			}
			else
			{
				const VkDeviceSize vertex_offset = 0;
				vkCmdBindVertexBuffers(command_buffer, 0, 1, &item.vertex_buffer, &vertex_offset);
			}
			//绑定索引缓冲（uint32 索引；偏移由网格记录给出，顶点与索引可共用一个缓冲）
			vkCmdBindIndexBuffer(command_buffer, item.index_buffer, item.index_offset, VK_INDEX_TYPE_UINT32);

			/*
			绘制索引几何（实例数 ≥ 1）
			第三百四十二轮：GPU 侧间接项以单条 vkCmdDrawIndexedIndirect 下达，实例数取自命令
			（索引数亦以命令为准，故此处只校验命令缓冲存在）。
			*/
			if (item.gpu_indirect)
				vkCmdDrawIndexedIndirect(command_buffer, item.indirect_command_buffer,
					item.indirect_command_offset, 1, static_cast<uint32_t>(sizeof(Indirect_Draw_Command)));
			else
				vkCmdDrawIndexed(command_buffer, item.index_count, item.instance_count, 0, 0, 0);
			//累计本批实际绘制量（仅在此处递增，被跳过的项不计）
			++counts.items;
			counts.instances += item.instance_count;
		}
		return counts;
	}

	/*
	设置间接录制上下文（第二百五十二轮）
	mapped 为空、缓冲为空或容量 ≤ 1 时视为关闭（回落逐项直接绘制，行为与引入前一致）。
	*/
	void Frame_Context::indirect_context_set(void* mapped, VkBuffer buffer, uint32_t capacity, uint32_t instance_stride)
	{
		//关闭条件
		if (mapped == nullptr || buffer == VK_NULL_HANDLE || capacity <= 1 || instance_stride == 0)
		{
			indirect_mapped = nullptr;
			indirect_buffer = VK_NULL_HANDLE;
			indirect_capacity = 0;
			return;
		}
		indirect_mapped = mapped;
		indirect_buffer = buffer;
		indirect_capacity = capacity;
		indirect_instance_stride = instance_stride;
	}

	//本帧已写入的间接命令数
	uint32_t Frame_Context::indirect_command_count_get(void) const
	{
		return frame_indirect_commands;
	}

	//累计间接绘制调用数
	uint64_t Frame_Context::indirect_draw_count_get(void) const
	{
		return total_indirect_draws;
	}

	//写入指定帧槽的间接命令缓冲（门面初始化时调用；槽序号越界返回假）
	bool Frame_Context::slot_indirect_buffer_set(uint32_t slot_index, VkBuffer buffer, void* mapped, uint32_t capacity)
	{
		//槽序号越界
		if (slot_index >= slots.size())
			return false;
		slots[slot_index].indirect_buffer = buffer;
		slots[slot_index].indirect_mapped = mapped;
		slots[slot_index].indirect_capacity = buffer == VK_NULL_HANDLE ? 0 : capacity;
		return true;
	}

	//UI 阶段录制（MSAA 解析后；单采样通道，仅颜色附件）
	void Frame_Context::ui_pass_record(VkCommandBuffer command_buffer, VkImage color_image, VkImageView color_view,
		VkExtent2D extent, VkViewport viewport, VkRect2D scissor, const Frame_Ui_Callback* ui_callback)
	{
		//解析写入 → 下一通道读取 / 写入的依赖屏障（同布局）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = color_image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		//UI 颜色附件（载入解析结果）
		VkRenderingAttachmentInfo ui_color{};
		ui_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		ui_color.imageView = color_view;
		ui_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		ui_color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		ui_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		//UI 渲染信息（无深度附件）
		VkRenderingInfo ui_rendering{};
		ui_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		ui_rendering.renderArea.offset = { 0, 0 };
		ui_rendering.renderArea.extent = extent;
		ui_rendering.layerCount = 1;
		ui_rendering.colorAttachmentCount = 1;
		ui_rendering.pColorAttachments = &ui_color;

		//进入 UI 通道并恢复视口 / 裁剪（上层 UI 后端按当前动态状态绘制）
		vkCmdBeginRendering(command_buffer, &ui_rendering);
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);
		ui_callback->fn(command_buffer, ui_callback->user_data);
		vkCmdEndRendering(command_buffer);
	}

	//录制命令缓冲：颜色 / 深度布局转换 + 动态渲染（清屏 + 逐项绘制 + UI 阶段）+ 可选像素回读 + 呈现前布局转换
	void Frame_Context::command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
		VkImage depth_image, VkImageView depth_view, VkExtent2D extent,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool,
		VkImage msaa_image, VkImageView msaa_color_view, VkImageView msaa_depth_view, VkImage msaa_depth_image,
		uint32_t sample_count, const Frame_Post_Scene_Callback* post_scene)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 4);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		颜色布局转换：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
		同步由 acquire 信号量保护，故 srcStage = COLOR_ATTACHMENT_OUTPUT、srcAccess = 0（K11）。
		*/
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样颜色图像内容逐帧丢弃（loadOp CLEAR），故 oldLayout 用 UNDEFINED；
		解析目标（交换链图像）已在上方转入 COLOR_ATTACHMENT_OPTIMAL。
		*/
		if (sample_count > 1 && msaa_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier{};
			msaa_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			msaa_barrier.image = msaa_image;
			msaa_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		//颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到 image_view）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = sample_count > 1 ? msaa_color_view : image_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color.float32[0] = clear_color[0];
		color_attachment.clearValue.color.float32[1] = clear_color[1];
		color_attachment.clearValue.color.float32[2] = clear_color[2];
		color_attachment.clearValue.color.float32[3] = clear_color[3];
		//MSAA：声明解析目标（单采样交换链图像）与平均解析
		if (sample_count > 1)
		{
			color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color_attachment.resolveImageView = image_view;
			color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		/*
		深度附件信息
		深度图像由交换链模块持有，布局已在交换链重建时统一转为 DEPTH_ATTACHMENT_OPTIMAL，
		故此处不再逐帧转换，仅作清屏（深度 1.0，模板 0）后保留；MSAA 时用多重采样深度附件。
		*/
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = sample_count > 1 ? msaa_depth_view : depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil.depth = 1.0f;
		depth_attachment.clearValue.depthStencil.stencil = 0;
		/*
		深度解析（第二百六十轮）
		MSAA 下场景只写多重采样深度；当多重采样深度图像与单采样深度图像均就绪时，
		以「样本 0」解析到单采样深度，供 Hi-Z 生成读取（不影响既有颜色 / UI 路径）。
		*/
		if (sample_count > 1 && msaa_depth_image != VK_NULL_HANDLE && depth_image != VK_NULL_HANDLE)
		{
			depth_attachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depth_attachment.resolveImageView = depth_view;
			depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		//渲染信息（渲染区 = 交换链范围，单颜色附件 + 深度附件）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		last_parallel_segments = 0;
		/*
		视口 / 裁剪（正高度；Y 轴向下由投影处理，K2）
		声明在此（而非单线程分支内）：MSAA 解析后的 UI 通道同样需要这两个值。
		*/
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段清屏、其余保留；MSAA 解析仅末段；
		单采样时 UI 与场景共用本通道，故 UI 回调放在末段（位置与单线程一致）。
		返回 0 表示未规划并行（线程数 / 项数 / 次级缓冲不满足），走下方单线程录制。
		*/
		if (scene_parallel_record(command_buffer, rendering_info, color_attachment, depth_attachment,
			viewport, scissor, items, item_count, sample_count, ui_callback) == 0)
		{
		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);

		//视口 / 裁剪（值见上方声明）
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		//逐渲染项绘制（带状态缓存）
		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		frame_recorded = items_record(command_buffer, items, item_count);

		/*
		UI 阶段
		若设置了 UI 绘制回调，则交还上层绘制 UI：上层（Test 层）在此调用
		ImGui_ImplVulkan_RenderDrawData —— 直接画在本帧已开启的动态渲染通道内
		（与场景共用同一通道；该通道附件为交换链颜色附件 + 深度附件，故上层
		ImGui 管线的 PipelineRenderingCreateInfo 必须与本通道附件格式相容）。
		回调返回后本通道仍处于开启状态。
		*/
		//单采样：UI 与场景共用同一动态渲染通道；MSAA：留到解析完成后另起单采样通道
		if (sample_count <= 1 && ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_callback->fn(command_buffer, ui_callback->user_data);

		//结束场景动态渲染（MSAA 时此处完成解析到交换链图像）
		vkCmdEndRendering(command_buffer);
		}

		/*
		场景退场回调（第二百五十四轮；Hi-Z 深度拷贝与金字塔生成）
		必须在渲染通道之外执行；单采样直接用场景深度，MSAA 用深度解析后的单采样深度（第二百六十轮）。
		回调自行完成深度布局往返，返回后深度仍为 DEPTH_ATTACHMENT_OPTIMAL。
		*/
		if (post_scene != nullptr && post_scene->fn != nullptr
			&& (sample_count <= 1 || msaa_depth_image != VK_NULL_HANDLE)
			&& depth_image != VK_NULL_HANDLE && depth_view != VK_NULL_HANDLE)
			post_scene->fn(command_buffer, depth_image, depth_view, extent, post_scene->user_data);

		/*
		场景 / 后处理分段时间戳（第一百二十八轮）
		直通路径无后处理：场景结束处同时记录查询 1 与 2，使「后处理耗时」恒 0。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		}

		/*
		UI 阶段（MSAA；第九十六轮；第二百一十四轮改走公共实现）
		解析完成后以「加载后保留」的单采样通道绘制 UI（UI 管线为单采样）；通道仅含颜色附件。
		*/
		if (sample_count > 1 && ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_pass_record(command_buffer, image, image_view, extent, viewport, scissor, ui_callback);

		/*
		像素回读（可选）
		渲染通道结束后先把颜色附件转入 TRANSFER_SRC_OPTIMAL，再把整张附件拷入主机可见的回读缓冲；
		屏障 src 为颜色附件输出写入、dst 为传输读取，保证拷贝读到本帧已完成写入的像素。
		*/
		if (readback_buffer != VK_NULL_HANDLE)
		{
			//布局转换：颜色附件 → 传输源
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			//拷贝区域（整张颜色附件，RGBA8 紧排；行距 / 图距为 0 表示按范围紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { extent.width, extent.height, 1 };
			//把颜色附件拷入回读缓冲
			vkCmdCopyImageToBuffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				readback_buffer, 1, &copy_region);

			/*
			颜色布局转换：TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR
			呈现同步由 render_finished 信号量与 present 等待承担，故 dstStage = BOTTOM_OF_PIPE、dstAccess = 0（K12）。
			*/
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		//若不回读则直接由颜色附件转入呈现布局（与无回读路径逐字节一致）
		else
		{
			/*
			颜色布局转换：COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
			呈现同步由 render_finished 信号量与 present 等待承担，故 dstStage = BOTTOM_OF_PIPE、dstAccess = 0（K12）。
			*/
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = 0;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		//GPU 时间戳终点（可选；管线底部，含此前全部命令与收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}

	/*
	录制「渲染到离屏目标」的命令缓冲
	颜色进入布局为 SHADER_READ_ONLY_OPTIMAL（目标创建时已转换、帧间恒保持），故先转入 COLOR_ATTACHMENT_OPTIMAL；
	深度进入布局恒为 DEPTH_ATTACHMENT_OPTIMAL，逐帧以 CLEAR 清空，故此处不再逐帧转换；
	场景 pass 结束后若给定回读缓冲，则转入 TRANSFER_SRC 把颜色附件拷入缓冲，再转回 SHADER_READ_ONLY；
	否则直接由 COLOR_ATTACHMENT_OPTIMAL 转回 SHADER_READ_ONLY，使目标在帧间保持可采样布局（不呈现到窗口）。
	*/
	void Frame_Context::command_record_target(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
		const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
		const Frame_Ui_Callback* ui_callback, VkBuffer readback_buffer, VkQueryPool timestamp_pool, bool shadow_pass,
		const Frame_Post_Scene_Callback* post_scene)
	{
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			//阴影 pass 为独立提交，仅记录始 / 末两个查询；普通离屏路径记录四段
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, shadow_pass ? 2 : 4);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		颜色布局转换：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
		离屏目标颜色在帧间恒为着色器可读布局（可能已被采样），故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
		*/
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = target.color_image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样附件内容逐帧丢弃，oldLayout 用 UNDEFINED；解析目标（target.color_image）
		已在上方转入 COLOR_ATTACHMENT_OPTIMAL。
		*/
		if (target.sample_count > 1 && target.msaa_color_view != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier = barrier;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		//颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到目标颜色图像）
		VkRenderingAttachmentInfo color_attachment{};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = target.sample_count > 1 ? target.msaa_color_view : target.color_view;
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		/*
		清屏或保留（第一百二十一轮）
		clear_color 为空表示「保留已有内容」，供同一目标的后续分片 pass 使用（级联阴影第 2 片）。
		*/
		color_attachment.loadOp = clear_color != nullptr ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (clear_color != nullptr)
		{
			color_attachment.clearValue.color.float32[0] = clear_color[0];
			color_attachment.clearValue.color.float32[1] = clear_color[1];
			color_attachment.clearValue.color.float32[2] = clear_color[2];
			color_attachment.clearValue.color.float32[3] = clear_color[3];
		}
		else
			color_attachment.clearValue.color.float32[0] = 0.0f;
		//MSAA：解析到目标单采样颜色图像
		if (target.sample_count > 1)
		{
			color_attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color_attachment.resolveImageView = target.color_view;
			color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		//深度附件信息（清屏后保留；目标深度在帧间恒为深度附件布局；MSAA 时用多重采样深度）
		VkRenderingAttachmentInfo depth_attachment{};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = target.sample_count > 1 ? target.msaa_depth_view : target.depth_view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil.depth = 1.0f;
		depth_attachment.clearValue.depthStencil.stencil = 0;

		//渲染信息（渲染区 = 目标范围，单颜色附件 + 深度附件）
		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = target.extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;
		rendering_info.pDepthAttachment = &depth_attachment;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		last_parallel_segments = 0;
		/*
		视口 / 裁剪（含子视口收缩；第二百轮）
		声明在分支外：并行段与 MSAA 解析后的 UI 通道都要用同一组值。
		*/
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(target.extent.width);
		viewport.height = static_cast<float>(target.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = target.extent;
		if (target.viewport_rect.extent.width != 0 && target.viewport_rect.extent.height != 0)
		{
			viewport.x = static_cast<float>(target.viewport_rect.offset.x);
			viewport.y = static_cast<float>(target.viewport_rect.offset.y);
			viewport.width = static_cast<float>(target.viewport_rect.extent.width);
			viewport.height = static_cast<float>(target.viewport_rect.extent.height);
			scissor = target.viewport_rect;
		}
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段沿用清屏、其余保留；MSAA 解析仅末段；
		单采样时 UI 与场景共用本通道，故 UI 回调放在末段（位置与单线程一致）。
		返回 0 表示未规划并行（线程数 / 项数 / 次级缓冲不满足），走下方单线程录制。
		*/
		if (scene_parallel_record(command_buffer, rendering_info, color_attachment, depth_attachment,
			viewport, scissor, items, item_count, target.sample_count, ui_callback) == 0)
		{
		//进入动态渲染
		vkCmdBeginRendering(command_buffer, &rendering_info);
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		//逐渲染项绘制（带状态缓存）
		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		frame_recorded = items_record(command_buffer, items, item_count);

		//单采样：UI 与场景共用同一通道；MSAA：留到解析完成后另起单采样通道
		if (target.sample_count <= 1 && ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_callback->fn(command_buffer, ui_callback->user_data);

		//结束场景动态渲染（MSAA 时此处完成解析到目标颜色图像）
		vkCmdEndRendering(command_buffer);
		}

		/*
		场景退场回调（第三百六十八轮；离屏路径的 Hi-Z 深度拷贝与金字塔生成）
		必须在渲染通道之外执行；单采样直接用目标深度，MSAA 用深度解析后的单采样目标深度。
		回调自行完成深度布局往返，返回后深度仍为 DEPTH_ATTACHMENT_OPTIMAL。
		*/
		if (post_scene != nullptr && post_scene->fn != nullptr
			&& (target.sample_count <= 1 || target.msaa_depth_view != VK_NULL_HANDLE)
			&& target.depth_image != VK_NULL_HANDLE && target.depth_view != VK_NULL_HANDLE)
			post_scene->fn(command_buffer, target.depth_image, target.depth_view, target.extent, post_scene->user_data);

		/*
		场景 / 后处理分段时间戳（第一百二十八轮）
		离屏与阴影 pass 均无后处理：普通离屏路径在场景结束处同时写查询 1 / 2；
		阴影 pass 使用两查询池，末点改由下方收尾处记录，故此处不写。
		*/
		if (timestamp_pool != VK_NULL_HANDLE && !shadow_pass)
		{
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		}

		/*
		UI 阶段（MSAA；第九十六轮；第二百一十四轮改走公共实现）
		解析完成后以「加载后保留」的单采样通道绘制 UI（UI 管线为单采样）；通道仅含颜色附件。
		*/
		if (target.sample_count > 1 && ui_callback != nullptr && ui_callback->fn != nullptr)
			ui_pass_record(command_buffer, target.color_image, target.color_view, target.extent,
				viewport, scissor, ui_callback);

		//若请求回读：颜色附件 → 传输源，拷贝到回读缓冲，再转到着色器可读
		if (readback_buffer != VK_NULL_HANDLE)
		{
			//布局转换：颜色附件 → 传输源
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			//拷贝区域（整张目标颜色附件，RGBA8 紧排）
			VkBufferImageCopy copy_region{};
			copy_region.bufferOffset = 0;
			copy_region.bufferRowLength = 0;
			copy_region.bufferImageHeight = 0;
			copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy_region.imageSubresource.mipLevel = 0;
			copy_region.imageSubresource.baseArrayLayer = 0;
			copy_region.imageSubresource.layerCount = 1;
			copy_region.imageOffset = { 0, 0, 0 };
			copy_region.imageExtent = { target.extent.width, target.extent.height, 1 };
			//把颜色附件拷入回读缓冲
			vkCmdCopyImageToBuffer(command_buffer, target.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				readback_buffer, 1, &copy_region);

			//布局转换：传输源 → 着色器可读（使目标帧间可被采样）
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}
		//若不回读：颜色附件直接转回着色器可读（使目标帧间可被采样）
		else
		{
			barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		//GPU 时间戳终点（可选；阴影 pass 为末点 1，普通离屏为末点 3）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool,
				shadow_pass ? 1 : 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}

	/*
	录制「场景 pass ＋ N 级后处理 pass（多目标轮转）＋ 呈现」的命令缓冲
	场景 pass：targets[0] 颜色 SHADER_READ_ONLY → COLOR_ATTACHMENT，绘制 items 后转回 SHADER_READ_ONLY；
	后处理各级（stage 自 0 起）：源 = targets[stage % target_count]、恒 SHADER_READ_ONLY，
	        宿 = 「末级 → 交换链（UNDEFINED 转入颜色附件）／非末级 → targets[(stage + 1) % target_count]
	              （SHADER_READ_ONLY 转入颜色附件，写完后转回 SHADER_READ_ONLY）」，
	        以 post_items[stage] 全屏绘制；
	交换链收尾与既有路径同口径（回读则 TRANSFER_SRC → PRESENT_SRC，否则 COLOR_ATTACHMENT → PRESENT_SRC）。
	*/
	void Frame_Context::command_record_chain_present(VkCommandBuffer command_buffer, VkImage swapchain_image,
		VkImageView swapchain_view, VkImageView swapchain_depth_view, VkExtent2D swapchain_extent,
		const Frame_Render_Target* targets, uint32_t target_count,
		const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item* post_items,
		uint32_t post_count, const float clear_color[4], VkBuffer readback_buffer, VkQueryPool timestamp_pool,
		VkImage swapchain_msaa_image, VkImageView swapchain_msaa_color_view,
		VkImageView swapchain_msaa_depth_view, uint32_t swapchain_sample_count)
	{
		//场景目标（目标组首项）
		const Frame_Render_Target& scene_target = targets[0];
		//开始录制（一次性提交）
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_result_check(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer");

		/*
		GPU 时间戳起点（可选；查询池为空即跳过）
		先重置该查询池（须在渲染通道之外，故紧随录制开始处）再于管线顶部记录起点；
		终点在收尾处记录，故该值覆盖「场景 pass ＋ 全部后处理 pass」的 GPU 耗时。
		*/
		if (timestamp_pool != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(command_buffer, timestamp_pool, 0, 4);
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamp_pool, 0);
		}

		//布局屏障（逐段复用同一对象，改字段即可）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		//GPU 侧间接命令的可见性屏障（第三百四十二轮；批内无此类项时不插入）
		indirect_command_sync(command_buffer, items, item_count);

		/*
		场景 pass · 场景目标颜色布局转换：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
		目标颜色在帧间恒为着色器可读布局（可能已被采样），故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.image = scene_target.color_image;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		场景 pass · MSAA 颜色附件布局转换（第九十六轮；仅采样数 > 1）
		多重采样附件内容逐帧丢弃，oldLayout 用 UNDEFINED；解析目标（场景目标颜色图像）已在上方转入颜色附件布局。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_color_view != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_barrier = barrier;
			msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			msaa_barrier.image = scene_target.msaa_color_image;
			msaa_barrier.srcAccessMask = 0;
			msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
		}

		/*
		场景 pass · MSAA 深度附件布局转换（第一百九十轮；仅采样数 > 1）
		多重采样深度附件内容逐帧丢弃，故 oldLayout 用 UNDEFINED；
		msaa_depth_image 为空（未开启深度解析）时跳过，保持既有行为。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier msaa_depth_barrier = barrier;
			msaa_depth_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			msaa_depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			msaa_depth_barrier.image = scene_target.msaa_depth_image;
			msaa_depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			msaa_depth_barrier.srcAccessMask = 0;
			msaa_depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &msaa_depth_barrier);
		}

		//场景 pass · 颜色附件信息（清屏后保留；MSAA 时写入多重采样附件并解析到场景目标）
		VkRenderingAttachmentInfo scene_color{};
		scene_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		scene_color.imageView = scene_target.sample_count > 1 ? scene_target.msaa_color_view : scene_target.color_view;
		scene_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		scene_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		scene_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		scene_color.clearValue.color.float32[0] = clear_color[0];
		scene_color.clearValue.color.float32[1] = clear_color[1];
		scene_color.clearValue.color.float32[2] = clear_color[2];
		scene_color.clearValue.color.float32[3] = clear_color[3];
		//MSAA：解析到场景目标单采样颜色图像
		if (scene_target.sample_count > 1)
		{
			scene_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			scene_color.resolveImageView = scene_target.color_view;
			scene_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		//场景 pass · 深度附件信息（场景目标深度在帧间恒为深度附件布局，逐帧清屏；MSAA 时用多重采样深度）
		VkRenderingAttachmentInfo scene_depth{};
		scene_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		scene_depth.imageView = scene_target.sample_count > 1 ? scene_target.msaa_depth_view : scene_target.depth_view;
		scene_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		scene_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		scene_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		scene_depth.clearValue.depthStencil.depth = 1.0f;
		scene_depth.clearValue.depthStencil.stencil = 0;
		/*
		MSAA 深度解析（第一百九十轮）
		深度类后处理（景深 / 深度描边 / 屏幕空间遮蔽）只能采样单采样深度；
		MSAA 下场景 pass 只写多重采样深度附件，故在 pass 结束时按「样本 0」解析到单采样深度图像。
		多重采样深度图与单采样深度图均就绪时才启用；否则保持既有行为。
		*/
		if (scene_target.sample_count > 1 && scene_target.msaa_depth_image != VK_NULL_HANDLE
			&& scene_target.depth_image != VK_NULL_HANDLE)
		{
			scene_depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			scene_depth.resolveImageView = scene_target.depth_view;
			scene_depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		//场景 pass · 渲染信息（渲染区 = 场景目标范围）
		VkRenderingInfo scene_rendering{};
		scene_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		scene_rendering.renderArea.offset = { 0, 0 };
		scene_rendering.renderArea.extent = scene_target.extent;
		scene_rendering.layerCount = 1;
		scene_rendering.colorAttachmentCount = 1;
		scene_rendering.pColorAttachments = &scene_color;
		scene_rendering.pDepthAttachment = &scene_depth;

		//清零本帧实际绘制量（本次录制重新累计；链路径的各级另在下方逐级累加）
		frame_recorded = Recorded_Counts{};
		//清零并行段数（并行分支按实际段数回填；第一百九十八轮）
		last_parallel_segments = 0;
		/*
		场景 pass 的并行录制（第二百零八轮统一走 core/并行录制.cpp 的公共实现）
		段内完整渲染实例、首段清屏、其余保留；多重采样解析只在末段开启；
		主缓冲以 vkCmdExecuteCommands 顺序执行：GPU 执行顺序与单线程完全一致，画面与统计逐字节相同。
		链路径不绘制 UI（UI 由上层在此帧之外处理），故回调传空指针。
		*/
		VkViewport scene_viewport{};
		scene_viewport.x = 0.0f;
		scene_viewport.y = 0.0f;
		scene_viewport.width = static_cast<float>(scene_target.extent.width);
		scene_viewport.height = static_cast<float>(scene_target.extent.height);
		scene_viewport.minDepth = 0.0f;
		scene_viewport.maxDepth = 1.0f;
		VkRect2D scene_scissor{};
		scene_scissor.offset = { 0, 0 };
		scene_scissor.extent = scene_target.extent;
		if (scene_parallel_record(command_buffer, scene_rendering, scene_color, scene_depth,
			scene_viewport, scene_scissor, items, item_count, scene_target.sample_count, nullptr) == 0)
		{
			//场景 pass · 进入动态渲染并设置视口 / 裁剪（按场景目标范围）
			vkCmdBeginRendering(command_buffer, &scene_rendering);
			vkCmdSetViewport(command_buffer, 0, 1, &scene_viewport);
			vkCmdSetScissor(command_buffer, 0, 1, &scene_scissor);
			//场景 pass · 场景绘制
			frame_recorded = items_record(command_buffer, items, item_count);
			//场景 pass · 结束动态渲染
			vkCmdEndRendering(command_buffer);
		}
		//场景结束时间戳（第一百二十八轮；后处理耗时 = 查询 2 − 查询 1）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 1);

		/*
		场景 pass → 首级后处理 · 场景目标颜色布局转换：COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
		首级后处理在片元阶段采样该目标，故 dstStage = FRAGMENT_SHADER、dstAccess = SHADER_READ。
		*/
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = scene_target.color_image;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		/*
		场景 pass → 后处理 · 场景深度布局转换（第一百二十三轮）
		深度采样类阶段（景深）在片元阶段读取深度图，故由深度附件转为着色器可读；
		深度图像句柄为空（未取深度）时跳过，保持既有行为。
		*/
		if (scene_target.depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier depth_barrier{};
			depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			depth_barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.image = scene_target.depth_image;
			depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depth_barrier.subresourceRange.baseMipLevel = 0;
			depth_barrier.subresourceRange.levelCount = 1;
			depth_barrier.subresourceRange.baseArrayLayer = 0;
			depth_barrier.subresourceRange.layerCount = 1;
			depth_barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			depth_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			/*
			srcStage 同时覆盖「深度附件写」与「解析写」（第一百九十轮）：
			MSAA 深度解析在动态渲染结束时执行，其写入归入颜色附件输出阶段。
			*/
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &depth_barrier);
		}

		/*
		后处理各级（多目标轮转）
		第 stage 级：源 = targets[stage % target_count]（恒为 SHADER_READ_ONLY，可直接采样）；
		           宿 = 末级交换链图像 / 非末级 targets[(stage + 1) % target_count]。
		*/
		for (uint32_t stage = 0; stage < post_count; ++stage)
		{
			//本级是否为末级（宿为交换链）
			bool last_stage = (stage + 1 == post_count);
			//本级宿（非末级时的中间目标：源的下一个目标，环形轮转）
			const Frame_Render_Target& dest = targets[(stage + 1) % target_count];

			//宿布局转换：末级为交换链图像（UNDEFINED → 颜色附件），否则为中间目标（SHADER_READ_ONLY → 颜色附件）
			if (last_stage)
			{
				/*
				交换链图像：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
				同步由 acquire 信号量保护，故 srcStage = COLOR_ATTACHMENT_OUTPUT、srcAccess = 0（K11）。
				*/
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.image = swapchain_image;
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);
			}
			else
			{
				/*
				中间目标：SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL
				该目标可能刚被上一级采样（WAR）或本就处于着色器可读布局，故 srcStage = FRAGMENT_SHADER、srcAccess = SHADER_READ。
				*/
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.image = dest.color_image;
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);
			}

			/*
			本级宿 MSAA 参数（第九十六轮）
			末级宿为交换链（用交换链 MSAA 附件）；非末级宿为中间目标（用目标自带 MSAA 附件）。
			*/
			VkImage stage_msaa_image = last_stage ? swapchain_msaa_image : dest.msaa_color_image;
			VkImageView stage_msaa_color = last_stage ? swapchain_msaa_color_view : dest.msaa_color_view;
			VkImageView stage_msaa_depth = last_stage ? swapchain_msaa_depth_view : dest.msaa_depth_view;
			/*
			本级采样数取该级管线实际烘焙值（第九十八轮）
			后处理为全屏 pass，单采样与多重采样结果等价；内置后处理管线固定单采样，
			故通常直接写单采样宿，省去多重采样附件的写入与解析带宽。
			*/
			uint32_t stage_sample_count = post_items[stage].sample_count;
			if (stage_sample_count == 0)
				stage_sample_count = 1;
			//本级宿 MSAA 颜色附件布局转换（内容逐帧丢弃，oldLayout 用 UNDEFINED）
			if (stage_sample_count > 1 && stage_msaa_image != VK_NULL_HANDLE)
			{
				VkImageMemoryBarrier msaa_barrier = barrier;
				msaa_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				msaa_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				msaa_barrier.image = stage_msaa_image;
				msaa_barrier.srcAccessMask = 0;
				msaa_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &msaa_barrier);
			}

			//本级宿范围（末级取交换链范围，否则取中间目标范围）
			VkExtent2D stage_extent = last_stage ? swapchain_extent : dest.extent;
			//本级源目标（第 stage 级读第 stage % 目标数 个中间目标）
			const Frame_Render_Target& source = targets[stage % target_count];
			/*
			本级「源 / 宿尺寸 ＋ 泛光参数 ＋ 描边色」推送（offset 80，共 12 个浮点）
			前 4 个为源宽 / 源高 / 宿宽 / 宿高（后处理片元据此映射像素坐标）；
			其后 4 个为泛光阈值 / 软膝 / 强度 / 保留（由门面写在绘制项里）；
			最后 4 个为描边色（rgb ＋ 保留；第一百八十一轮，默认黑 = 压暗轮廓）。
			*/
			float stage_uniform[12] = {
				static_cast<float>(source.extent.width), static_cast<float>(source.extent.height),
				static_cast<float>(stage_extent.width), static_cast<float>(stage_extent.height),
				post_items[stage].post_params[0], post_items[stage].post_params[1],
				post_items[stage].post_params[2], post_items[stage].post_params[3],
				post_items[stage].post_color[0], post_items[stage].post_color[1],
				post_items[stage].post_color[2], post_items[stage].post_color[3],
			};
			//本级绘制项（复制后补推送数据；不改调用方数组）
			Frame_Render_Item stage_item = post_items[stage];
			stage_item.push_constants = stage_uniform;
			stage_item.push_constant_size = static_cast<uint32_t>(sizeof(stage_uniform));
			stage_item.push_constant_offset = 80;

			//本级颜色附件信息（清屏；全屏后处理覆盖，故内容由后处理决定；MSAA 时解析到宿单采样颜色）
			VkRenderingAttachmentInfo stage_color{};
			stage_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			stage_color.imageView = stage_sample_count > 1 ? stage_msaa_color
				: (last_stage ? swapchain_view : dest.color_view);
			stage_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			stage_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			stage_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stage_color.clearValue.color.float32[0] = clear_color[0];
			stage_color.clearValue.color.float32[1] = clear_color[1];
			stage_color.clearValue.color.float32[2] = clear_color[2];
			stage_color.clearValue.color.float32[3] = clear_color[3];
			//MSAA：解析到宿单采样颜色视图
			if (stage_sample_count > 1)
			{
				stage_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				stage_color.resolveImageView = last_stage ? swapchain_view : dest.color_view;
				stage_color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}

			//本级深度附件信息（末级取交换链深度，否则取中间目标深度；逐帧清屏；MSAA 时用多重采样深度）
			VkRenderingAttachmentInfo stage_depth{};
			stage_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			stage_depth.imageView = stage_sample_count > 1 ? stage_msaa_depth
				: (last_stage ? swapchain_depth_view : dest.depth_view);
			stage_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			stage_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			stage_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stage_depth.clearValue.depthStencil.depth = 1.0f;
			stage_depth.clearValue.depthStencil.stencil = 0;

			//本级渲染信息（渲染区 = 本级宿范围）
			VkRenderingInfo stage_rendering{};
			stage_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			stage_rendering.renderArea.offset = { 0, 0 };
			stage_rendering.renderArea.extent = stage_extent;
			stage_rendering.layerCount = 1;
			stage_rendering.colorAttachmentCount = 1;
			stage_rendering.pColorAttachments = &stage_color;
			stage_rendering.pDepthAttachment = &stage_depth;

			//本级 · 进入动态渲染并设置视口 / 裁剪（按本级宿范围）
			vkCmdBeginRendering(command_buffer, &stage_rendering);
			VkViewport stage_viewport{};
			stage_viewport.x = 0.0f;
			stage_viewport.y = 0.0f;
			stage_viewport.width = static_cast<float>(stage_extent.width);
			stage_viewport.height = static_cast<float>(stage_extent.height);
			stage_viewport.minDepth = 0.0f;
			stage_viewport.maxDepth = 1.0f;
			vkCmdSetViewport(command_buffer, 0, 1, &stage_viewport);
			VkRect2D stage_scissor{};
			stage_scissor.offset = { 0, 0 };
			stage_scissor.extent = stage_extent;
			vkCmdSetScissor(command_buffer, 0, 1, &stage_scissor);
			//本级 · 全屏后处理绘制（单绘制项，含源 / 宿尺寸推送）
			Recorded_Counts stage_counts = items_record(command_buffer, &stage_item, 1);
			frame_recorded.items += stage_counts.items;
			frame_recorded.instances += stage_counts.instances;
			//本级 · 结束动态渲染
			vkCmdEndRendering(command_buffer);

			//末级（交换链）：按是否回读收尾为 TRANSFER_SRC → PRESENT_SRC 或直接 COLOR_ATTACHMENT → PRESENT_SRC
			if (last_stage)
			{
				if (readback_buffer != VK_NULL_HANDLE)
				{
					//布局转换：颜色附件 → 传输源
					barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					barrier.image = swapchain_image;
					barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					vkCmdPipelineBarrier(command_buffer,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &barrier);

					//拷贝区域（整张交换链颜色附件，RGBA8 紧排）
					VkBufferImageCopy copy_region{};
					copy_region.bufferOffset = 0;
					copy_region.bufferRowLength = 0;
					copy_region.bufferImageHeight = 0;
					copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copy_region.imageSubresource.mipLevel = 0;
					copy_region.imageSubresource.baseArrayLayer = 0;
					copy_region.imageSubresource.layerCount = 1;
					copy_region.imageOffset = { 0, 0, 0 };
					copy_region.imageExtent = { swapchain_extent.width, swapchain_extent.height, 1 };
					//把颜色附件拷入回读缓冲
					vkCmdCopyImageToBuffer(command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						readback_buffer, 1, &copy_region);

					//布局转换：传输源 → 呈现布局
					barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
					barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					barrier.dstAccessMask = 0;
					vkCmdPipelineBarrier(command_buffer,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						0, 0, nullptr, 0, nullptr, 1, &barrier);
				}
				//若不回读：颜色附件直接转入呈现布局
				else
				{
					barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
					barrier.image = swapchain_image;
					barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					barrier.dstAccessMask = 0;
					vkCmdPipelineBarrier(command_buffer,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						0, 0, nullptr, 0, nullptr, 1, &barrier);
				}
			}
			//非末级（中间目标）：颜色附件 → 着色器可读，使下一级可采样且帧间保持该布局
			else
			{
				barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.image = dest.color_image;
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(command_buffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &barrier);
			}
		}

		/*
		后处理链收尾 · 场景深度布局回转（第一百二十三轮）
		本帧采样结束后把深度转回深度附件布局，供下一帧以 DEPTH_ATTACHMENT_OPTIMAL 重新挂载；
		深度图像句柄为空时跳过（既有行为不变）。
		*/
		//后处理结束时间戳（第一百二十八轮；含全部后处理级与各级收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 2);
		if (scene_target.depth_image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier depth_barrier{};
			depth_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			depth_barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			depth_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depth_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			depth_barrier.image = scene_target.depth_image;
			depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depth_barrier.subresourceRange.baseMipLevel = 0;
			depth_barrier.subresourceRange.levelCount = 1;
			depth_barrier.subresourceRange.baseArrayLayer = 0;
			depth_barrier.subresourceRange.layerCount = 1;
			depth_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
				| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			vkCmdPipelineBarrier(command_buffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0, 0, nullptr, 0, nullptr, 1, &depth_barrier);
		}

		//GPU 时间戳终点（可选；管线底部，含此前全部命令与收尾布局转换）
		if (timestamp_pool != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamp_pool, 3);

		//结束录制
		vk_result_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
	}
}
