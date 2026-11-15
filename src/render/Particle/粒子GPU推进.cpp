#include "src/render/Particle/粒子GPU推进.h"
//六域内部完整状态
#include "Engine/EngineRenderer/src/render/Renderer/core/渲染器内部状态.h"

namespace engine
{
	//常驻 GPU 发射
	uint32_t Render_Particle::particle_gpu_emit_resident(uint32_t count)
	{
		//前置条件
		if (owner == nullptr || !owner->device().valid() || !state_ptr->particle_enabled || !state_ptr->particle_resident
			|| !state_ptr->particle_emitter_configured || count == 0)
			return 0;
		if (!particle_gpu_available())
			return 0;
		uint32_t capacity = state_ptr->particle_buffer_capacity;
		//当前活跃计数（GPU 侧 [1]）
		uint32_t active = 0;
		if (!owner->compute().compute_buffer_read(state_ptr->particle_control_buffer, &active, 4u, 4u))
			return 0;
		state_ptr->particle_resident_active = active;
		//池满
		if (active >= capacity)
			return 0;
		//本次发射数（不超过剩余容量）
		uint32_t budget = count;
		if (active + budget > capacity)
			budget = capacity - active;
		//绑定：状态（只写新槽）/ 控制 / 力场（占位）/ 状态（占位）
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_buffer_front, 0))
			return 0;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_control_buffer, 1))
			return 0;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_force_buffer, 2))
			return 0;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_buffer_front, 3))
			return 0;
		const Particle_Emitter& emitter = state_ptr->particle_emitter;
		//推送常量：位置与形状 / 速度与寿命 / 容量数量种子半径 / 主方向
		const float push[16] = {
			emitter.position[0], emitter.position[1], emitter.position[2], static_cast<float>(emitter.shape),
			emitter.speed_min, emitter.speed_max, emitter.lifetime_min, emitter.lifetime_max,
			static_cast<float>(capacity), static_cast<float>(budget),
			static_cast<float>(state_ptr->particle_born_total + 1u), emitter.extents[0],
			emitter.direction[0], emitter.direction[1], emitter.direction[2], 0.0f
		};
		if (!owner->compute().compute_dispatch(state_ptr->particle_emit_pipeline, budget, 64u, push, sizeof(push)))
			return 0;
		//回读新计数并结算实际发射数
		uint32_t after = 0;
		if (!owner->compute().compute_buffer_read(state_ptr->particle_control_buffer, &after, 4u, 4u))
			return 0;
		state_ptr->particle_resident_active = after;
		uint32_t spawned = after > active ? after - active : 0u;
		state_ptr->particle_born_total += spawned;
		return spawned;
	}

	//常驻 GPU 推进
	bool Render_Particle::particle_gpu_step_resident(float dt)
	{
		//前置条件
		if (owner == nullptr || !owner->device().valid() || !state_ptr->particle_enabled || !state_ptr->particle_resident)
			return false;
		if (!std::isfinite(dt) || dt <= 0.0f)
			return false;
		if (!particle_gpu_available())
			return false;
		//主机侧计时
		const auto sim_begin = std::chrono::high_resolution_clock::now();
		uint32_t capacity = state_ptr->particle_buffer_capacity;
		//推进前活跃计数
		uint32_t before = 0;
		if (!owner->compute().compute_buffer_read(state_ptr->particle_control_buffer, &before, 4u, 4u))
			return false;
		state_ptr->particle_resident_active = before;
		//上传力场（仅前 force_count 项）
		if (state_ptr->particle_params.force_count > 0)
		{
			uint32_t force_bytes = state_ptr->particle_params.force_count * static_cast<uint32_t>(PARTICLE_FORCE_STRIDE);
			if (!owner->compute().compute_buffer_write(state_ptr->particle_force_buffer, state_ptr->particle_params.forces, force_bytes))
				return false;
		}
		//绑定：front 状态 / back 状态 / 力场 / 控制缓冲
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_buffer_front, 0))
			return false;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_buffer_back, 1))
			return false;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_force_buffer, 2))
			return false;
		if (!owner->compute().compute_buffer_bind(state_ptr->particle_control_buffer, 3))
			return false;
		//推送常量（与同步链推进同布局）
		const float push[16] = {
			state_ptr->particle_params.gravity[0], state_ptr->particle_params.gravity[1], state_ptr->particle_params.gravity[2], dt,
			state_ptr->particle_params.damping, static_cast<float>(capacity),
			static_cast<float>(state_ptr->particle_params.force_count), 0.0f,
			state_ptr->particle_params.sphere_center[0], state_ptr->particle_params.sphere_center[1],
			state_ptr->particle_params.sphere_center[2], state_ptr->particle_params.sphere_radius,
			state_ptr->particle_params.floor_y, state_ptr->particle_params.restitution, state_ptr->particle_params.friction,
			static_cast<float>(state_ptr->particle_params.ground_enabled)
		};
		if (!owner->compute().compute_dispatch(state_ptr->particle_resident_step_pipeline, capacity, 64u, push, sizeof(push)))
			return false;
		//交换前后缓冲（下一步读取新结果）
		uint64_t swapped = state_ptr->particle_buffer_front;
		state_ptr->particle_buffer_front = state_ptr->particle_buffer_back;
		state_ptr->particle_buffer_back = swapped;
		//推进后活跃计数与死亡数
		uint32_t after = 0;
		if (!owner->compute().compute_buffer_read(state_ptr->particle_control_buffer, &after, 4u, 4u))
			return false;
		state_ptr->particle_resident_active = after;
		uint32_t died = before > after ? before - after : 0u;
		state_ptr->particle_gpu_died_last = died;
		state_ptr->particle_died_total += died;
		//设备耗时（不支持时间戳为 −1）与主机侧耗时
		state_ptr->particle_gpu_ms_last = owner->compute().compute_timestamp_supported()
			? owner->compute().compute_stats_get().elapsed_ms : -1.0;
		const auto sim_end = std::chrono::high_resolution_clock::now();
		state_ptr->particle_sim_ms_last = std::chrono::duration<double, std::milli>(sim_end - sim_begin).count();
		return true;
	}

	namespace particle_gpu
	{
		//GPU 发射（常驻模式）
		uint32_t emit(Renderer& renderer, uint32_t count)
		{
			return renderer.particle().particle_gpu_emit_resident(count);
		}

		//常驻推进
		bool step_resident(Renderer& renderer, float dt)
		{
			return renderer.particle().particle_gpu_step_resident(dt);
		}
	}
}
