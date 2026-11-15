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

	bool Render_Compute::gpu_cull_mask(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], uint32_t* mask_out)
	{
		//未初始化或不支持计算路径
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported())
			return false;
		//入参非法
		if (transforms == nullptr || mask_out == nullptr || count == 0
			|| local_min == nullptr || local_max == nullptr)
			return false;
		//当前视图 × 投影 → 六平面
		float view_proj[16] = {};
		owner->impl->scene_domain->state().camera.view_proj_get(view_proj);
		Frustum_Planes planes;
		if (!frustum_planes_extract(planes, view_proj))
			return false;
		//CPU 侧逐实例求世界包围球（vec4：中心 xyz ＋ 半径）
		std::vector<float> spheres(static_cast<std::size_t>(count) * 4, 0.0f);
		for (uint32_t index = 0; index < count; ++index)
		{
			World_Bounds bounds;
			//包围体构造失败（矩阵非法）
			if (!world_bounds_build(bounds, local_min, local_max, transforms + static_cast<std::size_t>(index) * 16))
				return false;
			const std::size_t offset = static_cast<std::size_t>(index) * 4;
			spheres[offset + 0] = bounds.center[0];
			spheres[offset + 1] = bounds.center[1];
			spheres[offset + 2] = bounds.center[2];
			spheres[offset + 3] = bounds.radius;
		}
		//球缓冲与掩码缓冲（主机可见，便于直接回读）
		Compute_Buffer_Handle sphere_handle = 0;
		Compute_Buffer_Handle mask_handle = 0;
		if (!compute_buffer_load(sphere_handle, static_cast<uint32_t>(spheres.size() * sizeof(float)), spheres.data()))
			return false;
		//掩码缓冲全量由着色器写入，无需初值
		if (!compute_buffer_load(mask_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			compute_buffer_unload(sphere_handle);
			return false;
		}
		//剔除管线懒加载
		if (state().gpu_cull_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "视锥剔除.comp.spv"))
			{
				compute_buffer_unload(mask_handle);
				compute_buffer_unload(sphere_handle);
				return false;
			}
			state().gpu_cull_pipeline = loaded;
		}
		//推送常量：六平面（96 字节）＋ 球数（4 字节）
		struct Cull_Push
		{
			float planes[24];
			uint32_t count;
		} push{};
		for (uint32_t plane = 0; plane < 6; ++plane)
			for (uint32_t column = 0; column < 4; ++column)
				push.planes[plane * 4 + column] = planes.planes[plane][column];
		push.count = count;
		//绑定 → 调度 → 回读
		bool ok = compute_buffer_bind(sphere_handle, 0)
			&& compute_buffer_bind(mask_handle, 1)
			&& compute_dispatch(state().gpu_cull_pipeline, count, 256, &push, static_cast<uint32_t>(sizeof(push)))
			&& compute_buffer_read(mask_handle, mask_out, count * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		compute_buffer_unload(mask_handle);
		compute_buffer_unload(sphere_handle);
		return ok;
	}

	void Render_Compute::gpu_cull_enable_set(bool enabled)
	{
		//未初始化时静默忽略
		if ((owner == nullptr || owner->impl == nullptr))
			return;
		state().gpu_cull_enabled = enabled;
	}

	bool Render_Compute::gpu_cull_enable_get(void) const
	{
		return owner != nullptr && owner->impl != nullptr && state().gpu_cull_enabled;
	}

	bool Render_Compute::gpu_instance_compact(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], float* matrices_out, uint32_t* visible_out)
	{
		//无距离上限、背面关
		const float zero_camera[3] = { 0.0f, 0.0f, 0.0f };
		const float zero_forward[3] = { 0.0f, 0.0f, 0.0f };
		return gpu_instance_compact_ex(transforms, count, local_min, local_max, zero_camera, zero_forward,
			0.0f, false, matrices_out, visible_out);
	}

	bool Render_Compute::gpu_instance_compact_ex(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], const float camera[3], const float forward[3],
		float max_distance, bool backface, float* matrices_out, uint32_t* visible_out)
	{
		//未初始化或不支持计算
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported())
			return false;
		//入参非法
		if (transforms == nullptr || matrices_out == nullptr || visible_out == nullptr || count == 0
			|| local_min == nullptr || local_max == nullptr || camera == nullptr || forward == nullptr)
			return false;
		*visible_out = 0;
		//当前视图 × 投影（与 CPU 剔除同一矩阵）
		float view_proj[16] = {};
		owner->impl->scene_domain->state().camera.view_proj_get(view_proj);
		//四个缓冲：源矩阵 / 压缩输出 / 原子计数 / 剔除参数
		const uint32_t matrix_bytes = count * static_cast<uint32_t>(sizeof(float)) * 16u;
		Compute_Buffer_Handle transform_handle = 0;
		Compute_Buffer_Handle compact_handle = 0;
		Compute_Buffer_Handle counter_handle = 0;
		Compute_Buffer_Handle params_handle = 0;
		if (!compute_buffer_load(transform_handle, matrix_bytes, transforms))
			return false;
		if (!compute_buffer_load(compact_handle, matrix_bytes))
		{
			compute_buffer_unload(transform_handle);
			return false;
		}
		//计数清零（主机可见初值 0；第 0 个为可见数、第 1 个为遮挡剔除数）
		const uint32_t zero[2] = { 0u, 0u };
		if (!compute_buffer_load(counter_handle, static_cast<uint32_t>(sizeof(zero)), zero))
		{
			compute_buffer_unload(compact_handle);
			compute_buffer_unload(transform_handle);
			return false;
		}
		//剔除参数（相机位置 / 前向 / 阈值与背面开关 ＋ Hi-Z 遮挡参数）
		struct Cull_Params
		{
			float camera_position[4];
			float camera_forward[4];
			float limits[4];
			//Hi-Z：(第 0 级宽, 第 0 级高, 级数, 是否启用)
			float hi_z[4];
		} params{};
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			params.camera_position[axis] = camera[axis];
			params.camera_forward[axis] = forward[axis];
		}
		params.limits[0] = max_distance;
		params.limits[1] = backface ? 1.0f : 0.0f;
		/*
		Hi-Z 遮挡启用判定（第二百五十六轮）
		需开关开启、资源就绪、最近生成槽的金字塔有效且完整链视图非空；
		首帧（尚无生成）自动跳过，保持与引入前逐字节一致。
		*/
		const bool occlusion_enabled = state().hiz_enabled && state().hiz.valid()
			&& state().hiz.history_valid(owner->impl->last_hiz_slot)
			&& state().hiz.pyramid_full_view(owner->impl->last_hiz_slot) != VK_NULL_HANDLE
			&& state().hiz.depth_sampler_get() != VK_NULL_HANDLE;
		if (occlusion_enabled)
		{
			params.hi_z[0] = static_cast<float>(state().hiz.width_get());
			params.hi_z[1] = static_cast<float>(state().hiz.height_get());
			params.hi_z[2] = static_cast<float>(state().hiz.level_count());
			params.hi_z[3] = 1.0f;
		}
		//遮挡开启时把计算帧槽切到「最近生成槽」并把金字塔绑到采样器位
		if (occlusion_enabled)
		{
			state().compute.slot_set(owner->impl->last_hiz_slot);
			state().compute.sampler_bind(owner->impl->last_hiz_slot, state().hiz.depth_sampler_get(),
				state().hiz.pyramid_full_view(owner->impl->last_hiz_slot));
		}
		if (!compute_buffer_load(params_handle, static_cast<uint32_t>(sizeof(params)), &params))
		{
			compute_buffer_unload(counter_handle);
			compute_buffer_unload(compact_handle);
			compute_buffer_unload(transform_handle);
			return false;
		}
		//压缩管线懒加载
		if (state().gpu_compact_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!compute_pipeline_load(loaded, "实例压缩.comp.spv"))
			{
				compute_buffer_unload(params_handle);
				compute_buffer_unload(counter_handle);
				compute_buffer_unload(compact_handle);
				compute_buffer_unload(transform_handle);
				return false;
			}
			state().gpu_compact_pipeline = loaded;
		}
		//推送常量：视图投影（64）＋ 局部界（32）＋ 实例数（4）
		struct Compact_Push
		{
			float view_proj[16];
			float bounds_min[4];
			float bounds_max[4];
			uint32_t count;
		} push{};
		for (uint32_t index = 0; index < 16; ++index)
			push.view_proj[index] = view_proj[index];
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			push.bounds_min[axis] = local_min[axis];
			push.bounds_max[axis] = local_max[axis];
		}
		push.count = count;
		//绑定 → 调度 → 回读计数与压缩矩阵（含第 3 号剔除参数缓冲）
		uint32_t occluded = 0;
		uint32_t counters[2] = { 0u, 0u };
		bool ok = compute_buffer_bind(transform_handle, 0)
			&& compute_buffer_bind(compact_handle, 1)
			&& compute_buffer_bind(counter_handle, 2)
			&& compute_buffer_bind(params_handle, 3)
			&& compute_dispatch(state().gpu_compact_pipeline, count, 256, &push, static_cast<uint32_t>(sizeof(push)))
			&& compute_buffer_read(counter_handle, counters, static_cast<uint32_t>(sizeof(counters)), 0);
		//可见数与遮挡剔除数（后者仅在 Hi-Z 启用时非零）
		if (ok)
		{
			*visible_out = static_cast<uint32_t>(counters[0]);
			occluded = static_cast<uint32_t>(counters[1]);
			state().last_gpu_occluded = occluded;
		}
		//读取压缩后的矩阵（仅可见前缀）
		if (ok && *visible_out > 0)
			ok = compute_buffer_read(compact_handle, matrices_out,
				(*visible_out) * static_cast<uint32_t>(sizeof(float)) * 16u, 0);
		//记录 GPU 剔除耗时（计算时间戳；不支持时间戳时保持 −1 语义在统计侧）
		if (ok)
		{
			state().last_gpu_cull_ms = state().compute.stats_get().elapsed_ms;
			//累计 GPU 派发次数（第二百五十一轮）
			++state().gpu_cull_dispatches;
		}
		compute_buffer_unload(params_handle);
		compute_buffer_unload(counter_handle);
		compute_buffer_unload(compact_handle);
		compute_buffer_unload(transform_handle);
		return ok;
	}

	bool Render_Compute::compute_pipeline_load(Compute_Pipeline_Handle& handle_out, const std::string& shader_name)
	{
		//失败路径一律复位句柄（与材质句柄约定一致）
		handle_out = 0;
		//未初始化或不支持计算
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported() || shader_name.empty())
			return false;
		VkShaderModule module = VK_NULL_HANDLE;
		//加载 SPIR-V 模块
		if (!owner->impl->shader.module_load(module, shader_name))
		{
			logger.error("渲染器:: 计算着色器加载失败：{}", shader_name);
			return false;
		}
		uint64_t context_handle = 0;
		//登记计算管线（失败则回收模块）
		if (!state().compute.pipeline_register(context_handle, module))
		{
			logger.error("渲染器:: 计算管线登记失败：{}", shader_name);
			owner->impl->shader.module_destroy(module);
			return false;
		}
		Compute_Pipeline_Record record;
		record.handle = state().next_compute_pipeline_handle++;
		record.context_handle = context_handle;
		record.module = module;
		handle_out = record.handle;
		state().compute_pipelines.push_back(record);
		return true;
	}

	void Render_Compute::compute_pipeline_unload(Compute_Pipeline_Handle handle)
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return;
		for (std::size_t index = 0; index < state().compute_pipelines.size(); ++index)
		{
			//命中即销毁管线与模块并移出列表
			if (state().compute_pipelines[index].handle == handle)
			{
				state().compute.pipeline_release(state().compute_pipelines[index].context_handle);
				if (state().compute_pipelines[index].module != VK_NULL_HANDLE)
					owner->impl->shader.module_destroy(state().compute_pipelines[index].module);
				state().compute_pipelines.erase(state().compute_pipelines.begin() + static_cast<std::ptrdiff_t>(index));
				return;
			}
		}
	}

	bool Render_Compute::compute_buffer_bind(Compute_Buffer_Handle buffer, uint32_t binding)
	{
		//未初始化或 binding 越界
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || binding >= COMPUTE_BINDING_MAX)
			return false;
		for (Compute_Buffer_Record& record : state().compute_buffers)
		{
			//命中缓冲即写入描述符（整段绑定）
			if (record.handle == buffer)
				return state().compute.storage_bind(state().compute.slot_get(), binding, record.buffer, 0, record.size);
		}
		return false;
	}

	bool Render_Compute::compute_dispatch(Compute_Pipeline_Handle pipeline, uint32_t elements, uint32_t local_size,
		const void* push_data, uint32_t push_size)
	{
		//未初始化或不支持计算
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported())
			return false;
		//空元素数视为无需调度
		if (elements == 0)
			return true;
		//未指定组大小
		if (local_size == 0)
			return false;
		uint64_t context_handle = 0;
		for (Compute_Pipeline_Record& record : state().compute_pipelines)
		{
			//命中门面句柄
			if (record.handle == pipeline)
			{
				context_handle = record.context_handle;
				break;
			}
		}
		//句柄无效
		if (context_handle == 0)
			return false;
		//一次性提交：起时间戳 → 调度 → 计算→主机屏障 → 止时间戳（返回即可读回）
		bool dispatched = state().compute.single_submit([&](VkCommandBuffer command_buffer) -> bool
		{
			if (!state().compute.timestamp_begin(command_buffer))
				return false;
			if (!state().compute.dispatch_ceil(command_buffer, context_handle, elements, local_size, push_data, push_size))
				return false;
			if (!state().compute.barrier(command_buffer, Compute_Barrier::ComputeToHost))
				return false;
			return state().compute.timestamp_end(command_buffer);
		});
		//围栏等待之后读回计时（不支持时间戳时为空操作）
		if (dispatched)
			state().compute.timestamp_refresh();
		return dispatched;
	}
}
