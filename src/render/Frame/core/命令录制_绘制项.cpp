#include "命令录制_内部.h"

namespace engine
{
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
		//当前已绑定的 set0 binding5 动态偏移（收敛轮 863）
		VkDeviceSize bound_palette = 0;
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
						//仅在与当前已绑定（布局 + 数量 + 集合 + 调色板偏移）不同时重绑
						if (item.layout != bound_layout || item.descriptor_set_count != bound_set_count
							|| item.scene_set != bound_sets[0] || item.sampler_set != bound_sets[1]
							|| item.palette_offset != bound_palette)
						{
							//待绑定描述符集数组（set0 在前，set1 在后）
							VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
							//set0 binding5 动态偏移（静态项恒 0）
							const uint32_t palette_offset = static_cast<uint32_t>(item.palette_offset);
							//绑定描述符集
							vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
								0, item.descriptor_set_count, sets, 1, &palette_offset);
							//记录当前已绑定的布局与描述符集
							bound_layout = item.layout;
							bound_set_count = item.descriptor_set_count;
							bound_sets[0] = item.scene_set;
							bound_sets[1] = item.sampler_set;
							bound_palette = item.palette_offset;
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
				//仅在与当前已绑定（布局 + 数量 + 集合 + 调色板偏移）不同时重绑
				if (item.layout != bound_layout || item.descriptor_set_count != bound_set_count
					|| item.scene_set != bound_sets[0] || item.sampler_set != bound_sets[1]
					|| item.palette_offset != bound_palette)
				{
					//待绑定描述符集数组（set0 在前，set1 在后）
					VkDescriptorSet sets[2] = { item.scene_set, item.sampler_set };
					//set0 binding5 动态偏移（静态项恒 0）
					const uint32_t palette_offset = static_cast<uint32_t>(item.palette_offset);
					//绑定描述符集
					vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, item.layout,
						0, item.descriptor_set_count, sets, 1, &palette_offset);
					//记录当前已绑定的布局与描述符集
					bound_layout = item.layout;
					bound_set_count = item.descriptor_set_count;
					bound_sets[0] = item.scene_set;
					bound_sets[1] = item.sampler_set;
					bound_palette = item.palette_offset;
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
}
