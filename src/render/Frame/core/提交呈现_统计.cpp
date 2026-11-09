#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::chrono（主机侧帧耗时计时）

namespace engine
{
	//文件内部工具
	namespace
	{
		//重置帧槽的全部命令池（主池 ＋ 各次级池；第二百一十八轮）
		//次级池与次级命令缓冲一一对应，每段独占一池，故必须与主池一同重置
		bool frame_slot_pools_reset(VkDevice device, Frame_Slot& slot)
		{
			//重置主命令池（主命令缓冲随之可再次录制）
			if (!vk_result_check(vkResetCommandPool(device, slot.command_pool, 0), "vkResetCommandPool"))
				return false;
			//逐个重置次级命令池
			for (VkCommandPool pool : slot.secondary_pools)
			{
				if (!vk_result_check(vkResetCommandPool(device, pool, 0), "vkResetCommandPool(次级)"))
					return false;
			}
			return true;
		}
	}

	//登记态绘制统计（按登记项累计；实际绘制量由 items_record 的返回值 frame_recorded 另计）
	//累计本帧绘制统计（登记量 ＋ 实际绘制量）
	//登记量按登记项累计；实际绘制量取 items_record 的返回值 frame_recorded（跳过的不完整项不计）
	//登记态绘制统计（按登记项累计；实际绘制量由 items_record 的返回值 frame_recorded 另计）

	void Frame_Context::frame_stats_accumulate(const Frame_Render_Item* items, uint32_t item_count)
	{
		//累计绘制项数（按登记项计）
		total_draw_items += item_count;
		//累计实例数（各绘制项 instance_count 之和；0 表该项不完整、无实例可计）
		for (uint32_t index = 0; index < item_count; ++index)
			total_instances += items[index].instance_count;
		//累计实际绘制量（条件：本次录制已发生，frame_recorded 非零；被跳过的项不计）
		total_drawn_items += frame_recorded.items;
		total_drawn_instances += frame_recorded.instances;
	}

	void Frame_Context::gpu_timestamp_read(Frame_Slot& slot)
	{
		//无查询池 / 尚未写过 / 能力未启用即跳过
		if (slot.timestamp_pool == VK_NULL_HANDLE || !slot.timestamp_written || !timestamp_supported)
			return;
		//读回四个时间戳（帧始 / 场景结束 / 后处理结束 / 帧末；该槽上一轮已由栅栏等待保证执行完毕）
		uint64_t ticks[4] = { 0, 0, 0, 0 };
		VkResult status = vkGetQueryPoolResults(device, slot.timestamp_pool, 0, 4,
			sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		//尚未就绪（首轮未执行完等）即跳过，不改动已有值
		if (status != VK_SUCCESS)
			return;
		/*
		按图形队列族的时间戳有效位掩去高位
		有效位小于 64 时高位为未定义内容，直接相减会得到错误差值。
		*/
		uint64_t mask = timestamp_valid_bits >= 64 ? ~0ull : ((1ull << timestamp_valid_bits) - 1ull);
		uint64_t begin_ticks = ticks[0] & mask;
		uint64_t scene_end_ticks = ticks[1] & mask;
		uint64_t post_end_ticks = ticks[2] & mask;
		uint64_t end_ticks = ticks[3] & mask;
		//换算毫秒（timestamp_period 单位为「纳秒 / tick」）
		double period = static_cast<double>(timestamp_period) / 1.0e6;
		last_gpu_frame_ms = static_cast<double>(end_ticks - begin_ticks) * period;
		/*
		场景 / 后处理分解（第一百二十八轮）
		场景 = 查询 1 − 查询 0；后处理 = 查询 2 − 查询 1；两者之和小于等于总耗时（差值为收尾布局转换）。
		*/
		last_gpu_scene_ms = static_cast<double>(scene_end_ticks - begin_ticks) * period;
		last_gpu_post_ms = static_cast<double>(post_end_ticks - scene_end_ticks) * period;
		//累计本次读回的 GPU 耗时与样本数（仅在确实读回时递增，故样本数 = 已成功读回的帧数）
		total_gpu_frame_ms += last_gpu_frame_ms;
		//累计分段耗时（第一百三十一轮；与总耗时同一读回口径）
		total_gpu_scene_ms += last_gpu_scene_ms;
		total_gpu_post_ms += last_gpu_post_ms;
		++gpu_sample_count;
	}

	double Frame_Context::shadow_gpu_ms_read(void)
	{
		//无能力 / 无帧槽 / 槽号越界即返回现有值
		if (!timestamp_supported || slots.empty() || last_shadow_slot >= slots.size())
			return last_gpu_shadow_ms;
		Frame_Slot& slot = slots[last_shadow_slot];
		//尚未写过阴影时间戳即保持原值
		if (slot.shadow_timestamp_pool == VK_NULL_HANDLE || !slot.shadow_timestamp_written)
			return last_gpu_shadow_ms;
		//调用方已等尽在途帧，结果应已就绪
		uint64_t ticks[2] = { 0, 0 };
		if (vkGetQueryPoolResults(device, slot.shadow_timestamp_pool, 0, 2,
			sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
			return last_gpu_shadow_ms;
		uint64_t mask = timestamp_valid_bits >= 64 ? ~0ull : ((1ull << timestamp_valid_bits) - 1ull);
		double period = static_cast<double>(timestamp_period) / 1.0e6;
		last_gpu_shadow_ms = static_cast<double>((ticks[1] & mask) - (ticks[0] & mask)) * period;
		//累计阴影耗时与样本数（第一百三十一轮；仅在确实读回时递增）
		total_gpu_shadow_ms += last_gpu_shadow_ms;
		++gpu_shadow_sample_count;
		return last_gpu_shadow_ms;
	}
}
