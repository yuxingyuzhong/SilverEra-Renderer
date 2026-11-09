#pragma once
#include "../局部命名空间使用.h"
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
	inline bool push_constants_equal(const Frame_Render_Item& first, const Frame_Render_Item& next)
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
	inline void indirect_command_sync(VkCommandBuffer command_buffer, const Frame_Render_Item* items, uint32_t item_count)
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
	inline bool indirect_items_mergeable(const Frame_Render_Item& first, const Frame_Render_Item& next,
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
}
