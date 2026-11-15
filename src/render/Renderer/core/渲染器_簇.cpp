#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	//是否支持 GPU 时间戳

	bool Render_Compute::cluster_enable_set(bool enabled)
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		//关闭只置标志并清空观测值
		if (!enabled)
		{
			state().cluster_enabled = false;
			state().last_cluster_light_count = 0;
			return true;
		}
		//惰性构建资源
		if (!owner->impl->cluster_ensure())
			return false;
		state().cluster_enabled = true;
		return true;
	}

	bool Render_Compute::cluster_enable_get(void) const
	{
		return (owner != nullptr && owner->impl != nullptr) ? state().cluster_enabled : false;
	}

	bool Render_Compute::cluster_supported(void) const
	{
		//未初始化或能力不足
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		return state().cluster_capable && state().compute.supported();
	}

	uint32_t Render_Compute::cluster_light_count(void) const
	{
		return (owner != nullptr && owner->impl != nullptr) ? state().last_cluster_light_count : 0;
	}

	bool Render_Compute::cluster_grid_set(uint32_t x, uint32_t y, uint32_t z)
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		state().cluster_grid = cluster_grid_clamp(Cluster_Grid{ x, y, z });
		return true;
	}

	bool Render_Compute::cluster_grid_get(uint32_t& x_out, uint32_t& y_out, uint32_t& z_out) const
	{
		//出参先归零
		x_out = 0;
		y_out = 0;
		z_out = 0;
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		x_out = state().cluster_grid.x;
		y_out = state().cluster_grid.y;
		z_out = state().cluster_grid.z;
		return true;
	}

	uint32_t Render_Compute::cluster_grid_cluster_count(void) const
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return 0;
		return cluster_grid_clusters(state().cluster_grid);
	}

	bool Render_Compute::cluster_counts_reference_ex(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out) const
	{
		//出参先归零
		if (total_out != nullptr)
			*total_out = 0;
		//未初始化或出参为空
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || counts_out == nullptr)
			return false;
		//分簇相机输入（当前相机与近远平面）
		Cluster_Input input{};
		owner->impl->scene_domain->state().camera.view_proj_get(input.view_proj);
		owner->impl->scene_domain->state().camera.position_get(input.camera_position);
		owner->impl->scene_domain->state().camera.fov_get(&input.fov);
		input.aspect = owner->impl->scene_domain->state().camera.aspect_effective_get();
		owner->impl->scene_domain->state().camera.clip_get(&input.near_plane, &input.far_plane);
		//光源记录快照（容量上限内全部导出）
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = owner->scene().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		//调用 CPU 参考实现
		uint32_t total = cluster_counts_reference(counts_out, capacity, state().cluster_grid,
			records.data(), count, input);
		//容量不足或入参非法
		if (total == 0)
			return false;
		if (total_out != nullptr)
			*total_out = total;
		return true;
	}

	bool Render_Compute::cluster_counts_gpu(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out)
	{
		//出参先归零
		if (total_out != nullptr)
			*total_out = 0;
		//未初始化、不支持计算或出参为空
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported() || counts_out == nullptr)
			return false;
		//网格总簇数
		uint32_t total = cluster_grid_clusters(state().cluster_grid);
		if (total == 0)
			return false;
		//容量不足
		if (capacity < total)
			return false;
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = owner->scene().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		if (count == 0)
			return false;
		//光源记录缓冲（只读）与计数缓冲（零初始化）
		Compute_Buffer_Handle light_handle = 0;
		Compute_Buffer_Handle count_handle = 0;
		if (!compute_buffer_load(light_handle, count * LIGHT_RECORD_BYTES, records.data()))
			return false;
		std::vector<uint32_t> zero(total, 0u);
		if (!compute_buffer_load(count_handle, total * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			compute_buffer_unload(light_handle);
			return false;
		}
		//计数管线懒加载
		if (state().cluster_count_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster计数.comp.spv"))
			{
				compute_buffer_unload(count_handle);
				compute_buffer_unload(light_handle);
				return false;
			}
			state().cluster_count_pipeline = loaded;
		}
		//推送常量：视图投影（64）＋ 网格与记录数（16）＋ 相机位置（16）＋ 参数（16）
		struct Cluster_Push
		{
			float view_proj[16];
			uint32_t grid[4];
			float camera_position[4];
			float params[4];
		} push{};
		owner->impl->scene_domain->state().camera.view_proj_get(push.view_proj);
		push.grid[0] = state().cluster_grid.x;
		push.grid[1] = state().cluster_grid.y;
		push.grid[2] = state().cluster_grid.z;
		push.grid[3] = count;
		owner->impl->scene_domain->state().camera.position_get(push.camera_position);
		owner->impl->scene_domain->state().camera.fov_get(&push.params[0]);
		push.params[1] = owner->impl->scene_domain->state().camera.aspect_effective_get();
		owner->impl->scene_domain->state().camera.clip_get(&push.params[2], &push.params[3]);
		//绑定 → 调度（每记录一线程） → 回读逐簇计数
		bool ok = compute_buffer_bind(light_handle, 0)
			&& compute_buffer_bind(count_handle, 1)
			&& compute_dispatch(state().cluster_count_pipeline, count, 64, &push, static_cast<uint32_t>(sizeof(push)))
			&& compute_buffer_read(count_handle, counts_out, total * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		compute_buffer_unload(count_handle);
		compute_buffer_unload(light_handle);
		//写出总簇数
		if (ok && total_out != nullptr)
			*total_out = total;
		return ok;
	}

	bool Render_Compute::cluster_prefix_block_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
		uint32_t* block_totals_out, uint32_t block_capacity, uint32_t* block_count_out) const
	{
		//出参先归零
		if (block_count_out != nullptr)
			*block_count_out = 0;
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		uint32_t blocks = cluster_prefix_block_scan(prefix_out, block_totals_out, block_capacity, counts, count);
		//入参非法或容量不足
		if (blocks == 0)
			return false;
		if (block_count_out != nullptr)
			*block_count_out = blocks;
		return true;
	}

	bool Render_Compute::cluster_prefix_block_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
		uint32_t* block_totals_out, uint32_t block_count_capacity, uint32_t* block_count_out)
	{
		//出参先归零
		if (block_count_out != nullptr)
			*block_count_out = 0;
		//未初始化、不支持计算或入参非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported() || counts == nullptr
			|| prefix_out == nullptr || block_totals_out == nullptr || count == 0)
			return false;
		//块数与容量
		uint32_t blocks = (count + CLUSTER_SCAN_BLOCK - 1) / CLUSTER_SCAN_BLOCK;
		if (block_count_capacity < blocks)
			return false;
		//输入 / 前缀输出 / 块总和缓冲
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle prefix_handle = 0;
		Compute_Buffer_Handle totals_handle = 0;
		if (!compute_buffer_load(counts_handle, count * static_cast<uint32_t>(sizeof(uint32_t)), counts))
			return false;
		if (!compute_buffer_load(prefix_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			compute_buffer_unload(counts_handle);
			return false;
		}
		std::vector<uint32_t> zero(blocks, 0u);
		if (!compute_buffer_load(totals_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			compute_buffer_unload(prefix_handle);
			compute_buffer_unload(counts_handle);
			return false;
		}
		//扫描管线懒加载
		if (state().cluster_scan_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster块内扫描.comp.spv"))
			{
				compute_buffer_unload(totals_handle);
				compute_buffer_unload(prefix_handle);
				compute_buffer_unload(counts_handle);
				return false;
			}
			state().cluster_scan_pipeline = loaded;
		}
		//推送常量：元素总数
		uint32_t push_count = count;
		//绑定 → 调度（每块一个工作组） → 回读前缀与块总和
		bool ok = compute_buffer_bind(counts_handle, 0)
			&& compute_buffer_bind(prefix_handle, 1)
			&& compute_buffer_bind(totals_handle, 2)
			&& compute_dispatch(state().cluster_scan_pipeline, count, CLUSTER_SCAN_BLOCK,
				&push_count, static_cast<uint32_t>(sizeof(push_count)))
			&& compute_buffer_read(prefix_handle, prefix_out,
				count * static_cast<uint32_t>(sizeof(uint32_t)), 0)
			&& compute_buffer_read(totals_handle, block_totals_out,
				blocks * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		compute_buffer_unload(totals_handle);
		compute_buffer_unload(prefix_handle);
		compute_buffer_unload(counts_handle);
		//写出块数
		if (ok && block_count_out != nullptr)
			*block_count_out = blocks;
		return ok;
	}

	bool Render_Compute::cluster_prefix_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out) const
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		return cluster_prefix_scan(prefix_out, counts, count) != 0;
	}

	bool Render_Compute::cluster_prefix_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out)
	{
		//未初始化、不支持计算或入参非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported() || counts == nullptr
			|| prefix_out == nullptr || count == 0)
			return false;
		//块数
		uint32_t blocks = (count + CLUSTER_SCAN_BLOCK - 1) / CLUSTER_SCAN_BLOCK;
		//计数 / 前缀 / 块总和 / 块偏移四缓冲
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle prefix_handle = 0;
		Compute_Buffer_Handle totals_handle = 0;
		Compute_Buffer_Handle offsets_handle = 0;
		auto cleanup = [&](void)
		{
			//逆序归还已建缓冲（空句柄由卸载函数幂等处理）
			compute_buffer_unload(offsets_handle);
			compute_buffer_unload(totals_handle);
			compute_buffer_unload(prefix_handle);
			compute_buffer_unload(counts_handle);
		};
		if (!compute_buffer_load(counts_handle, count * static_cast<uint32_t>(sizeof(uint32_t)), counts))
			return false;
		if (!compute_buffer_load(prefix_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			cleanup();
			return false;
		}
		std::vector<uint32_t> zero(blocks, 0u);
		if (!compute_buffer_load(totals_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data())
			|| !compute_buffer_load(offsets_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			cleanup();
			return false;
		}
		//三条管线懒加载（块内扫描 / 块间偏移 / 偏移合成）
		if (state().cluster_scan_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster块内扫描.comp.spv"))
			{
				cleanup();
				return false;
			}
			state().cluster_scan_pipeline = loaded;
		}
		if (state().cluster_block_offset_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster块间扫描.comp.spv"))
			{
				cleanup();
				return false;
			}
			state().cluster_block_offset_pipeline = loaded;
		}
		if (state().cluster_offset_apply_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster块偏移合成.comp.spv"))
			{
				cleanup();
				return false;
			}
			state().cluster_offset_apply_pipeline = loaded;
		}
		//第一步：块内扫描（计数 → 块内前缀 ＋ 块总和）
		uint32_t push_count = count;
		bool ok = compute_buffer_bind(counts_handle, 0)
			&& compute_buffer_bind(prefix_handle, 1)
			&& compute_buffer_bind(totals_handle, 2)
			&& compute_dispatch(state().cluster_scan_pipeline, count, CLUSTER_SCAN_BLOCK,
				&push_count, static_cast<uint32_t>(sizeof(push_count)));
		//第二步：块总和串行扫描 → 块排他偏移（单线程）
		uint32_t push_blocks = blocks;
		if (ok)
			ok = compute_buffer_bind(totals_handle, 0)
				&& compute_buffer_bind(offsets_handle, 1)
				&& compute_dispatch(state().cluster_block_offset_pipeline, 1, 1,
					&push_blocks, static_cast<uint32_t>(sizeof(push_blocks)));
		//第三步：逐元素叠加块偏移
		if (ok)
			ok = compute_buffer_bind(prefix_handle, 0)
				&& compute_buffer_bind(offsets_handle, 1)
				&& compute_dispatch(state().cluster_offset_apply_pipeline, count, CLUSTER_SCAN_BLOCK,
					&push_count, static_cast<uint32_t>(sizeof(push_count)));
		//回读全量前缀和
		if (ok)
			ok = compute_buffer_read(prefix_handle, prefix_out,
				count * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		cleanup();
		return ok;
	}

	bool Render_Compute::cluster_list_build_cpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
		const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out) const
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化或入参非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || list_out == nullptr || offsets_out == nullptr
			|| counts == nullptr || cluster_total == 0)
			return false;
		//排他偏移与总条目
		uint32_t entries = cluster_list_offsets(counts, cluster_total, offsets_out);
		if (entries == 0 || capacity < entries)
			return false;
		//写入游标初值（复制排他偏移，参考实现会推进）
		std::vector<uint32_t> cursor(cluster_total);
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
			cursor[cluster] = offsets_out[cluster];
		//分簇相机输入
		Cluster_Input input{};
		owner->impl->scene_domain->state().camera.view_proj_get(input.view_proj);
		owner->impl->scene_domain->state().camera.position_get(input.camera_position);
		owner->impl->scene_domain->state().camera.fov_get(&input.fov);
		input.aspect = owner->impl->scene_domain->state().camera.aspect_effective_get();
		owner->impl->scene_domain->state().camera.clip_get(&input.near_plane, &input.far_plane);
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = owner->scene().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		//调用 CPU 参考
		uint32_t written = cluster_list_reference(list_out, capacity, cursor.data(), counts, cluster_total,
			state().cluster_grid, records.data(), count, input);
		if (written == 0)
			return false;
		if (entries_out != nullptr)
			*entries_out = written;
		return true;
	}

	bool Render_Compute::cluster_list_build_gpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
		const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out)
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化、不支持计算或入参非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported() || list_out == nullptr
			|| offsets_out == nullptr || counts == nullptr || cluster_total == 0)
			return false;
		//总条目与容量
		uint32_t entries = 0;
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
			entries += counts[cluster];
		if (entries == 0 || capacity < entries)
			return false;
		//排他偏移
		for (uint32_t cluster = 0, running = 0; cluster < cluster_total; ++cluster)
		{
			offsets_out[cluster] = running;
			running += counts[cluster];
		}
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t light_count = owner->scene().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		if (light_count == 0)
			return false;
		//四缓冲：光源记录 / 计数 / 游标（排他偏移）/ 列表
		Compute_Buffer_Handle light_handle = 0;
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle cursor_handle = 0;
		Compute_Buffer_Handle list_handle = 0;
		auto cleanup = [&](void)
		{
			compute_buffer_unload(list_handle);
			compute_buffer_unload(cursor_handle);
			compute_buffer_unload(counts_handle);
			compute_buffer_unload(light_handle);
		};
		if (!compute_buffer_load(light_handle, light_count * LIGHT_RECORD_BYTES, records.data()))
			return false;
		if (!compute_buffer_load(counts_handle, cluster_total * static_cast<uint32_t>(sizeof(uint32_t)), counts))
		{
			cleanup();
			return false;
		}
		if (!compute_buffer_load(cursor_handle, cluster_total * static_cast<uint32_t>(sizeof(uint32_t)), offsets_out))
		{
			cleanup();
			return false;
		}
		if (!compute_buffer_load(list_handle, entries * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			cleanup();
			return false;
		}
		//列表写入管线懒加载
		if (state().cluster_list_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "Cluster列表写入.comp.spv"))
			{
				cleanup();
				return false;
			}
			state().cluster_list_pipeline = loaded;
		}
		//推送常量：视图投影 ＋ 网格 ＋ 相机位置 ＋ 参数 ＋ 光源记录数（116 字节）
		struct Cluster_List_Push
		{
			float view_proj[16];
			uint32_t grid[4];
			float camera_position[4];
			float params[4];
			uint32_t light_count;
		} push{};
		owner->impl->scene_domain->state().camera.view_proj_get(push.view_proj);
		push.grid[0] = state().cluster_grid.x;
		push.grid[1] = state().cluster_grid.y;
		push.grid[2] = state().cluster_grid.z;
		push.grid[3] = cluster_total;
		owner->impl->scene_domain->state().camera.position_get(push.camera_position);
		owner->impl->scene_domain->state().camera.fov_get(&push.params[0]);
		push.params[1] = owner->impl->scene_domain->state().camera.aspect_effective_get();
		owner->impl->scene_domain->state().camera.clip_get(&push.params[2], &push.params[3]);
		push.light_count = light_count;
		//绑定 → 调度（每记录一线程） → 回读列表
		bool ok = compute_buffer_bind(light_handle, 0)
			&& compute_buffer_bind(counts_handle, 1)
			&& compute_buffer_bind(cursor_handle, 2)
			&& compute_buffer_bind(list_handle, 3)
			&& compute_dispatch(state().cluster_list_pipeline, light_count, 64, &push,
				static_cast<uint32_t>(sizeof(push)))
			&& compute_buffer_read(list_handle, list_out,
				entries * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		cleanup();
		if (ok && entries_out != nullptr)
			*entries_out = entries;
		return ok;
	}

	bool Render_Compute::cluster_data_build(uint32_t* counts_out, uint32_t* offsets_out, uint32_t* list_out,
		uint32_t cluster_capacity, uint32_t list_capacity, uint32_t* entries_out, bool use_gpu)
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化或入参非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || counts_out == nullptr || offsets_out == nullptr
			|| list_out == nullptr)
			return false;
		//网格总簇数与容量
		uint32_t total = cluster_grid_clusters(state().cluster_grid);
		if (total == 0 || cluster_capacity < total)
			return false;
		//计时起点
		const auto begin = std::chrono::high_resolution_clock::now();
		//计数（GPU 可用且请求时走 GPU，否则回落 CPU 参考）
		bool gpu_path = use_gpu && compute_supported();
		uint32_t counted = 0;
		if (gpu_path)
			gpu_path = cluster_counts_gpu(counts_out, cluster_capacity, &counted);
		if (!gpu_path && !cluster_counts_reference_ex(counts_out, cluster_capacity, &counted))
			return false;
		//条目总数
		uint32_t entries = 0;
		for (uint32_t cluster = 0; cluster < total; ++cluster)
			entries += counts_out[cluster];
		if (entries == 0)
			return false;
		//列表（与计数采用同一路径）
		uint32_t built_entries = 0;
		bool ok = gpu_path
			? cluster_list_build_gpu(list_out, list_capacity, offsets_out, counts_out, total, &built_entries)
			: cluster_list_build_cpu(list_out, list_capacity, offsets_out, counts_out, total, &built_entries);
		if (!ok || built_entries != entries)
			return false;
		//计时终点与观测更新
		const auto end = std::chrono::high_resolution_clock::now();
		state().last_cluster_entries = entries;
		state().last_cluster_build_ms = std::chrono::duration<double, std::milli>(end - begin).count();
		if (entries_out != nullptr)
			*entries_out = entries;
		return true;
	}

	uint32_t Render_Compute::cluster_entries_get(void) const
	{
		return (owner != nullptr && owner->impl != nullptr) ? state().last_cluster_entries : 0;
	}

	double Render_Compute::cluster_build_ms_get(void) const
	{
		return (owner != nullptr && owner->impl != nullptr) ? state().last_cluster_build_ms : 0.0;
	}
}
