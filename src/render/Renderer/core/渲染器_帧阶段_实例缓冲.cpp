#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	bool frame_stage_instance_buffers(Renderer_Impl_Holder::Impl* impl, uint32_t slot)
	{
		/*
		实例模型矩阵写入本帧槽实例缓冲
		本帧登记的全部实例矩阵按登记顺序连续存放，故一次写入即可；
		随后每个渲染项只携带「帧槽缓冲句柄 ＋ 字节偏移 ＋ 实例数」。
		*/
		if (!impl->pending_instances.empty())
		{
			//本帧实例属性总字节数（一个实例 = 20 浮点 = 80 字节）
			VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
			/*
			实例缓冲按需写入（第六十一轮）
			逐帧槽缓存上次写入的实例属性，按「一个实例 = 80 字节」为比较单元；
			连续变化实例合并为一段逐段写入；首次访问该帧槽整块写入本帧全部实例。
			写入内容与整块写逐字节一致，故画面不变。
			*/
			const std::size_t cache_slot_count = impl->instance_buffers.size();
			const std::size_t cache_slot_floats = static_cast<std::size_t>(impl->instance_capacity) * INSTANCE_FLOAT_COUNT;
			//若缓存规模与帧槽 / 容量不一致则重建
			if (impl->instance_block_cache.size() != cache_slot_count * cache_slot_floats)
			{
				impl->instance_block_cache.assign(cache_slot_count * cache_slot_floats, 0.0f);
				impl->instance_block_count.assign(cache_slot_count, 0);
				impl->instance_block_cached.assign(cache_slot_count, 0);
			}
			//本帧槽缓存段
			float* instance_cache = impl->instance_block_cache.data() + static_cast<std::size_t>(slot) * cache_slot_floats;
			//上次已写入的实例数
			const uint32_t cached_count = impl->instance_block_count[slot];
			//本帧实例数
			const std::size_t instance_count = impl->pending_instances.size();
			//本帧实际上传字节数
			std::size_t uploaded_bytes = 0;
			//若该帧槽尚未建立缓存
			if (impl->instance_block_cached[slot] == 0)
			{
				//整块写入
				if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
					impl->pending_instances.data(), instance_bytes, 0))
				{
					logger.error("渲染器:: 实例缓冲写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes = static_cast<std::size_t>(instance_bytes);
			}
			else
			{
				/*
				差异掩码（第一百七十七轮）
				逐实例只写自己的掩码槽，故可分块并行；后续连续段扫描仍单线程按掩码进行，
				扫描结果与逐实例比较完全一致（上传区间不变）。
				*/
				impl->instance_changed_mask.assign(instance_count, 0);
				//判定单实例是否变化（只读本帧实例与上轮缓存，写自己的掩码槽）
				auto mark_changed_instance = [impl, cached_count, instance_cache](std::size_t begin, std::size_t end)
				{
					for (std::size_t index = begin; index < end; ++index)
					{
						//超出上次缓存长度的实例一律视为变化
						bool changed = index >= cached_count;
						//未超出则逐浮点比较
						for (std::size_t offset = 0; !changed && offset < INSTANCE_FLOAT_COUNT; ++offset)
							if (impl->pending_instances[index][offset] != instance_cache[index * INSTANCE_FLOAT_COUNT + offset])
								changed = true;
						impl->instance_changed_mask[index] = changed ? 1 : 0;
					}
				};
				//本帧生效的扫描阈值（默认 512；可由配置调整，第二百二十六轮）
				const std::size_t scan_parallel_min = impl->instance_scan_parallel_min;
				//实例数达到阈值且工作线程可用时并行判定，否则由调用线程整段执行
				if (impl->thread_pool.active() && instance_count >= scan_parallel_min)
				{
					//派发计数差值（第二百二十四轮：按用途归入差异扫描）
					const uint64_t dispatch_before = impl->thread_pool.dispatch_count_get();
					impl->thread_pool.ranges_run(instance_count, scan_parallel_min, mark_changed_instance);
					impl->scan_parallel_dispatches += impl->thread_pool.dispatch_count_get() - dispatch_before;
				}
				else
					mark_changed_instance(0, instance_count);
				//当前变化段的起始实例与是否处于段内
				std::size_t run_start = 0;
				bool in_run = false;
				//按掩码扫描并合并连续变化区间
				for (std::size_t index = 0; index < instance_count; ++index)
				{
					//该实例本帧是否变化
					const bool changed = impl->instance_changed_mask[index] != 0;
					//若变化段遇到未变化实例则收束并写入
					if (!changed && in_run)
					{
						const std::size_t segment_bytes = (index - run_start) * INSTANCE_STRIDE_BYTES;
						if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
							impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
						{
							logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
							return false;
						}
						uploaded_bytes += segment_bytes;
						in_run = false;
					}
					//若未在段内且该实例变化则开启新段
					if (changed && !in_run)
					{
						run_start = index;
						in_run = true;
					}
				}
				//收尾：最后一段延伸到本帧实例末尾
				if (in_run)
				{
					const std::size_t segment_bytes = (instance_count - run_start) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
						impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
					{
						logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
				}
			}
			/*
			更新该帧槽缓存（与已写入内容一致；第二百一十九轮改为按块并行）
			每个实例只写自己的 20 浮点槽，块间区间不重叠，故可分块并行；
			结果与单线程逐元素拷贝完全一致，仅降低大批量实例时的主机耗时。
			*/
			auto refresh_instance_cache = [instance_cache](const std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& pending,
				std::size_t begin, std::size_t end)
			{
				for (std::size_t index = begin; index < end; ++index)
				{
					for (std::size_t offset = 0; offset < INSTANCE_FLOAT_COUNT; ++offset)
						instance_cache[index * INSTANCE_FLOAT_COUNT + offset] = pending[index][offset];
				}
			};
			//本帧生效的扫描 / 回写阈值（默认 512；可由配置调整，第二百二十六轮）
			const std::size_t scan_parallel_min = impl->instance_scan_parallel_min;
			//实例数达到阈值且工作线程可用时并行回写，否则由调用线程整段执行
			if (impl->thread_pool.active() && instance_count >= scan_parallel_min)
			{
				//派发计数差值（第二百二十四轮：按用途归入缓存回写）
				const uint64_t dispatch_before = impl->thread_pool.dispatch_count_get();
				impl->thread_pool.ranges_run(instance_count, scan_parallel_min,
					[impl, &refresh_instance_cache](std::size_t begin, std::size_t end)
					{
						refresh_instance_cache(impl->pending_instances, begin, end);
					});
				impl->cache_parallel_dispatches += impl->thread_pool.dispatch_count_get() - dispatch_before;
			}
			else
				refresh_instance_cache(impl->pending_instances, 0, instance_count);
			impl->instance_block_count[slot] = static_cast<uint32_t>(instance_count);
			impl->instance_block_cached[slot] = 1;
			//累计上传字节（观测用）
			impl->instance_upload_bytes += uploaded_bytes;
		}
		return true;
	}

	//帧阶段：追加天空盒绘制项（复用后处理全屏网格）
}
