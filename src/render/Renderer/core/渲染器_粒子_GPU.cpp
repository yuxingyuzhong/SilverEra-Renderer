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
	/*
	粒子绘制门面（第三百一十轮；默认关闭）
	模拟语义（发射 / 力场 / 碰撞 / 生命周期）已迁出渲染层：状态数组由应用层绑定，
	本层只做 GPU 上传与 billboard 绘制登记。关闭时不登记任何绘制项。
	*/
	//粒子路径是否可用（第三百一十一轮：改由计算路径可用性决定）

	bool Renderer_Compute::particle_gpu_ensure(void)
	{
		//未初始化或计算路径不可用
		if (impl == nullptr || !impl->initialized || !self().compute_supported())
			return false;
		//按 GPU 缓冲容量分配（容量为 0 视为不可用）
		uint32_t capacity = self().particle().state().particle_capacity;
		if (capacity == 0 || capacity > PARTICLE_CAPACITY_MAX)
			return false;
		//已按当前容量就绪
		if (self().particle().state().particle_buffer_capacity == capacity && self().particle().state().particle_buffer_front != 0
			&& self().particle().state().particle_buffer_back != 0 && self().particle().state().particle_step_pipeline != 0)
			return true;
		//容量变化：先释放旧资源
		self().particle_gpu_destroy();
		//双缓冲字节数
		uint32_t bytes = capacity * static_cast<uint32_t>(PARTICLE_STRIDE);
		//零值初值（未登记槽位 lifetime = 0，GPU 侧原样写回）
		std::vector<uint8_t> zeros(bytes, 0u);
		//力场缓冲（固定 8 场 × 40 字节，零值初值）
		std::vector<uint8_t> zero_forces(static_cast<std::size_t>(PARTICLE_FORCE_MAX) * PARTICLE_FORCE_STRIDE, 0u);
		//控制缓冲（环形指针 ＋ 活跃计数 ＋ 位图；第三百二十七轮）
		uint32_t control_bytes = 8u + ((capacity + 31u) / 32u) * 4u;
		std::vector<uint8_t> zero_control(control_bytes, 0u);
		//紧凑索引（uint32 × 容量）与紧凑计数（4 字节；第三百三十三轮）
		std::vector<uint8_t> zero_indices(static_cast<std::size_t>(capacity) * 4u, 0u);
		uint32_t zero_count = 0;
		/*
		按帧槽创建间接命令缓冲（第三百四十二轮；阶段 D）
		每条 20 字节（VkDrawIndexedIndirectCommand），用法 STORAGE ＋ INDIRECT、主机可见（便于回读校验）；
		每槽一份，保证「上一轮帧仍在读取的命令」不会被本帧写入覆盖。
		*/
		uint32_t slot_count = impl->frame.in_flight_count_get();
		slot_count = slot_count == 0 ? 1u : slot_count;
		std::vector<uint8_t> zero_command(Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_BYTES, 0u);
		bool indirect_created = true;
		for (uint32_t slot = 0; slot < slot_count && indirect_created; ++slot)
		{
			Compute_Buffer_Handle handle = 0;
			//创建该槽命令缓冲（追加 INDIRECT 用法）
			indirect_created = impl->compute_buffer_create_usage(handle, Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_BYTES,
				zero_command.data(), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			//成功则登记句柄
			if (indirect_created)
				self().particle().state().particle_indirect_buffers.push_back(handle);
		}
		bool created = indirect_created
			&& self().compute_buffer_load(self().particle().state().particle_buffer_front, bytes, zeros.data())
			&& self().compute_buffer_load(self().particle().state().particle_buffer_back, bytes, zeros.data())
			&& self().compute_buffer_load(self().particle().state().particle_force_buffer,
				static_cast<uint32_t>(zero_forces.size()), zero_forces.data())
			&& self().compute_buffer_load(self().particle().state().particle_control_buffer, control_bytes, zero_control.data())
			&& self().compute_buffer_load(self().particle().state().particle_index_buffer, static_cast<uint32_t>(zero_indices.size()),
				zero_indices.data())
			&& self().compute_buffer_load(self().particle().state().particle_compact_count_buffer, static_cast<uint32_t>(sizeof(zero_count)),
				&zero_count)
			&& self().compute_pipeline_load(self().particle().state().particle_step_pipeline, "Particle推进.comp.spv")
			&& self().compute_pipeline_load(self().particle().state().particle_emit_pipeline, "Particle发射.comp.spv")
			&& self().compute_pipeline_load(self().particle().state().particle_resident_step_pipeline, "Particle常驻推进.comp.spv")
			&& self().compute_pipeline_load(self().particle().state().particle_compact_pipeline, "Particle紧凑.comp.spv")
			&& self().compute_pipeline_load(self().particle().state().particle_indirect_pipeline, "Particle间接.comp.spv");
		//任一环节失败即整体回收
		if (!created)
		{
			logger.error("渲染器:: 粒子 GPU 推进资源创建失败（容量 {}）", capacity);
			self().particle_gpu_destroy();
			return false;
		}
		self().particle().state().particle_buffer_capacity = capacity;
		self().particle().state().particle_control_bytes = control_bytes;
		self().particle().state().particle_resident_active = 0;
		self().particle().state().particle_indirect_written = false;
		return true;
	}

	void Renderer_Compute::particle_gpu_destroy(void)
	{
		//未初始化
		if (impl == nullptr)
			return;
		//双缓冲与推进管线移交门面卸载（计算上下文仍存活时才有效）
		if (self().particle().state().particle_buffer_front != 0)
			self().compute_buffer_unload(self().particle().state().particle_buffer_front);
		if (self().particle().state().particle_buffer_back != 0)
			self().compute_buffer_unload(self().particle().state().particle_buffer_back);
		if (self().particle().state().particle_force_buffer != 0)
			self().compute_buffer_unload(self().particle().state().particle_force_buffer);
		if (self().particle().state().particle_control_buffer != 0)
			self().compute_buffer_unload(self().particle().state().particle_control_buffer);
		if (self().particle().state().particle_index_buffer != 0)
			self().compute_buffer_unload(self().particle().state().particle_index_buffer);
		if (self().particle().state().particle_compact_count_buffer != 0)
			self().compute_buffer_unload(self().particle().state().particle_compact_count_buffer);
		//间接命令缓冲（第三百四十二轮；按槽逐个卸载）
		for (Compute_Buffer_Handle handle : self().particle().state().particle_indirect_buffers)
			self().compute_buffer_unload(handle);
		if (self().particle().state().particle_step_pipeline != 0)
			self().compute_pipeline_unload(self().particle().state().particle_step_pipeline);
		if (self().particle().state().particle_emit_pipeline != 0)
			self().compute_pipeline_unload(self().particle().state().particle_emit_pipeline);
		if (self().particle().state().particle_resident_step_pipeline != 0)
			self().compute_pipeline_unload(self().particle().state().particle_resident_step_pipeline);
		if (self().particle().state().particle_compact_pipeline != 0)
			self().compute_pipeline_unload(self().particle().state().particle_compact_pipeline);
		if (self().particle().state().particle_indirect_pipeline != 0)
			self().compute_pipeline_unload(self().particle().state().particle_indirect_pipeline);
		self().particle().state().particle_indirect_buffers.clear();
		self().particle().state().particle_indirect_written = false;
		self().particle().state().particle_indirect_last_slot = 0;
		self().particle().state().particle_buffer_front = 0;
		self().particle().state().particle_buffer_back = 0;
		self().particle().state().particle_force_buffer = 0;
		self().particle().state().particle_control_buffer = 0;
		self().particle().state().particle_index_buffer = 0;
		self().particle().state().particle_compact_count_buffer = 0;
		self().particle().state().particle_step_pipeline = 0;
		self().particle().state().particle_emit_pipeline = 0;
		self().particle().state().particle_resident_step_pipeline = 0;
		self().particle().state().particle_compact_pipeline = 0;
		self().particle().state().particle_control_bytes = 0;
		self().particle().state().particle_resident_active = 0;
		self().particle().state().particle_compact_count = 0;
		self().particle().state().particle_buffer_capacity = 0;
	}

	bool Render_Particle::particle_gpu_available(void)
	{
		//未开启粒子或计算路径不可用
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled || !owner->compute_supported())
			return false;
		return owner->particle_gpu_ensure();
	}

	bool Render_Particle::particle_gpu_step(float dt)
	{
		//资源未就绪
		if (!owner->particle().particle_gpu_available())
			return false;
		//未绑定数据源
		if (state().particle_states == nullptr || state().particle_alive == nullptr
			|| state().particle_source_capacity == 0)
			return false;
		//常驻模式下禁止同步链推进（会污染常驻状态；第三百二十八轮）
		if (state().particle_resident)
			return false;
		//步长非法
		if (!std::isfinite(dt) || dt <= 0.0f)
			return false;
		//本次推进的死亡计数先清零
		state().particle_gpu_died_last = 0;
		//GPU 同步链主机侧计时（第三百二十二轮）
		const auto sim_begin = std::chrono::high_resolution_clock::now();
		uint32_t capacity = state().particle_buffer_capacity;
		//镜像容量须不小于推进容量
		if (state().particle_source_capacity < capacity)
			return false;
		uint32_t bytes = capacity * static_cast<uint32_t>(PARTICLE_STRIDE);
		//上传当前镜像到前缓冲
		if (!owner->compute_buffer_write(state().particle_buffer_front, state().particle_states, bytes))
			return false;
		//上传力场数组（仅上传前 force_count 项；第三百一十八轮）
		if (state().particle_params.force_count > 0)
		{
			uint32_t force_bytes = state().particle_params.force_count * static_cast<uint32_t>(PARTICLE_FORCE_STRIDE);
			if (!owner->compute_buffer_write(state().particle_force_buffer, state().particle_params.forces, force_bytes))
				return false;
		}
		//绑定前缓冲只读、后缓冲只写、力场数组只读
		if (!owner->compute_buffer_bind(state().particle_buffer_front, 0))
			return false;
		if (!owner->compute_buffer_bind(state().particle_buffer_back, 1))
			return false;
		if (!owner->compute_buffer_bind(state().particle_force_buffer, 2))
			return false;
		//推送常量：重力与步长、阻尼与容量与力场数、碰撞球、地面与材质（第三百一十九轮）
		const float push[16] = {
			state().particle_params.gravity[0], state().particle_params.gravity[1], state().particle_params.gravity[2], dt,
			state().particle_params.damping, static_cast<float>(capacity),
			static_cast<float>(state().particle_params.force_count), 0.0f,
			state().particle_params.sphere_center[0], state().particle_params.sphere_center[1],
			state().particle_params.sphere_center[2], state().particle_params.sphere_radius,
			state().particle_params.floor_y, state().particle_params.restitution, state().particle_params.friction,
			static_cast<float>(state().particle_params.ground_enabled)
		};
		if (!owner->compute_dispatch(state().particle_step_pipeline, capacity, 64u, push, sizeof(push)))
			return false;
		//回读覆盖镜像（GPU 结果作为下一步的上传源）
		if (!owner->compute_buffer_read(state().particle_buffer_back, state().particle_states, bytes))
			return false;
		/*
		生命周期同步（第三百一十三轮）
		GPU 以 lifetime ≤ 0 标记死亡；镜像侧逐索引置非活跃并清零寿命，
		使下次上传的非活跃槽位保持「lifetime = 0」不变式（着色器据此原样写回）。
		*/
		uint32_t died = 0;
		Particle_State* states = state().particle_states;
		uint8_t* alive = state().particle_alive;
		for (uint32_t index = 0; index < capacity; ++index)
		{
			//非活跃槽位保持零寿命不变式
			if (alive[index] == 0u)
			{
				states[index].lifetime = 0.0f;
				continue;
			}
			//GPU 标记死亡即置非活跃（索引记账由应用层负责）
			if (!(states[index].lifetime > 0.0f))
			{
				alive[index] = 0u;
				states[index].lifetime = 0.0f;
				++died;
			}
		}
		state().particle_gpu_died_last = died;
		state().particle_died_total += died;
		//GPU 设备耗时（设备不支持时间戳时为 −1）
		state().particle_gpu_ms_last = owner->impl->compute.timestamp_supported()
			? owner->impl->compute.stats_get().elapsed_ms : -1.0;
		//主机侧模拟耗时（含上传 / 调度 / 回读 + 镜像同步）
		const auto sim_end = std::chrono::high_resolution_clock::now();
		state().particle_sim_ms_last = std::chrono::duration<double, std::milli>(sim_end - sim_begin).count();
		return true;
	}

	uint32_t Render_Particle::particle_gpu_died_last(void) const
	{
		//未开启或未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled)
			return 0;
		return state().particle_gpu_died_last;
	}

	uint32_t Render_Particle::particle_gpu_active_count(void) const
	{
		//未初始化 / 未开启常驻
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_resident)
			return 0;
		return state().particle_resident_active;
	}

	bool Renderer_Compute::particle_draw_ensure(void)
	{
		//已就绪
		if (self().particle().state().particle_quad_mesh != 0 && self().particle().state().particle_draw_pipeline != 0)
			return true;
		//单位四边形网格（XY 平面，边长 1）
		if (self().particle().state().particle_quad_mesh == 0)
		{
			if (!self().resource().mesh_plane_load(self().particle().state().particle_quad_mesh, 1.0f, 1.0f))
				return false;
		}
		//粒子管线（Alpha 混合，与场景管线共享布局与描述符集布局）
		if (self().particle().state().particle_draw_pipeline == 0)
		{
			/*
			粒子管线：关闭背面剔除（billboard 朝向由相机决定）、深度写入按门面开关；
			混合档取粒子混合设置（默认 Alpha）。
			*/
			if (!impl->pipeline_load_ex(self().particle().state().particle_draw_pipeline, "粒子.vert.spv", "粒子.frag.spv",
				self().particle().state().particle_blend_mode, false, true, self().particle().state().particle_depth_write, false))
			{
				self().resource().mesh_unload(self().particle().state().particle_quad_mesh);
				self().particle().state().particle_quad_mesh = 0;
				return false;
			}
		}
		return true;
	}

	bool Renderer_Compute::particle_gpu_draw_ensure(void)
	{
		//单位四边形网格（与 CPU 路径共用）
		if (self().particle().state().particle_quad_mesh == 0)
		{
			if (!self().resource().mesh_plane_load(self().particle().state().particle_quad_mesh, 1.0f, 1.0f))
				return false;
		}
		//已就绪
		if (self().particle().state().particle_gpu_pipeline != 0)
			return true;
		//粒子 GPU 管线（关闭背面剔除、深度写入按门面开关、混合档取粒子设置）
		if (!impl->pipeline_load_ex(self().particle().state().particle_gpu_pipeline, "粒子GPU.vert.spv", "粒子.frag.spv",
			self().particle().state().particle_blend_mode, false, true, self().particle().state().particle_depth_write, false))
			return false;
		return true;
	}

	uint64_t Render_Particle::particle_gpu_bytes(void) const
	{
		//未初始化或资源未创建
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || state().particle_buffer_capacity == 0
			|| state().particle_buffer_front == 0 || state().particle_buffer_back == 0)
			return 0;
		uint64_t buffers = static_cast<uint64_t>(state().particle_buffer_capacity) * PARTICLE_STRIDE * 2u;
		uint64_t forces = state().particle_force_buffer != 0
			? static_cast<uint64_t>(PARTICLE_FORCE_MAX) * PARTICLE_FORCE_STRIDE : 0u;
		uint64_t control = static_cast<uint64_t>(state().particle_control_bytes);
		//紧凑索引与紧凑计数（第三百三十三轮）
		uint64_t compact = state().particle_index_buffer != 0
			? static_cast<uint64_t>(state().particle_buffer_capacity) * 4u + 4u : 0u;
		//间接命令缓冲（第三百五十二轮；按帧槽各 20 B）
		uint64_t indirect = static_cast<uint64_t>(state().particle_indirect_buffers.size())
			* Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_BYTES;
		return buffers + forces + control + compact + indirect;
	}

	bool Render_Particle::particle_resident_set(bool enabled)
	{
		//未初始化 / 未开启粒子 / 计算不可用
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled || !owner->compute_supported())
			return false;
		state().particle_resident = enabled;
		//开启时同步一次计数缓存
		if (enabled && owner->particle().particle_gpu_available())
		{
			uint32_t active = 0;
			if (owner->compute_buffer_read(state().particle_control_buffer, &active, 4u, 4u))
				state().particle_resident_active = active;
		}
		return true;
	}

	bool Render_Particle::particle_resident_get(void) const
	{
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return false;
		return state().particle_resident;
	}

	bool Render_Particle::particle_gpu_compact(void)
	{
		//前置条件
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled || !state().particle_resident)
			return false;
		if (!owner->particle().particle_gpu_available())
			return false;
		uint32_t capacity = state().particle_buffer_capacity;
		if (capacity == 0)
			return false;
		//主机侧计时（第三百四十轮）
		const auto compact_begin = std::chrono::high_resolution_clock::now();
		//清零紧凑计数
		uint32_t zero = 0;
		if (!owner->compute_buffer_write(state().particle_compact_count_buffer, &zero, static_cast<uint32_t>(sizeof(zero))))
			return false;
		//绑定：控制缓冲（位图）/ 紧凑索引 / 紧凑计数
		if (!owner->compute_buffer_bind(state().particle_control_buffer, 0))
			return false;
		if (!owner->compute_buffer_bind(state().particle_index_buffer, 1))
			return false;
		if (!owner->compute_buffer_bind(state().particle_compact_count_buffer, 2))
			return false;
		const uint32_t push[4] = { capacity, (capacity + 31u) / 32u, 0u, 0u };
		if (!owner->compute_dispatch(state().particle_compact_pipeline, capacity, 64u, push,
			static_cast<uint32_t>(sizeof(push))))
			return false;
		//回读索引数（防御越界）
		uint32_t count = 0;
		if (!owner->compute_buffer_read(state().particle_compact_count_buffer, &count, static_cast<uint32_t>(sizeof(count))))
			return false;
		if (count > capacity)
			count = capacity;
		state().particle_compact_count = count;
		const auto compact_end = std::chrono::high_resolution_clock::now();
		state().particle_compact_ms_last = std::chrono::duration<double, std::milli>(compact_end - compact_begin).count();
		return true;
	}

	uint32_t Render_Particle::particle_compact_count_get(void) const
	{
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_resident)
			return 0;
		return state().particle_compact_count;
	}

	uint32_t Render_Particle::particle_compact_indices_get(uint32_t* out, uint32_t max_count)
	{
		if (out == nullptr || max_count == 0)
			return 0;
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_resident)
			return 0;
		uint32_t count = state().particle_compact_count;
		if (count > max_count)
			count = max_count;
		if (count == 0)
			return 0;
		if (!owner->compute_buffer_read(state().particle_index_buffer, out, count * static_cast<uint32_t>(sizeof(uint32_t))))
			return 0;
		return count;
	}

	bool Render_Particle::particle_gpu_probe(void)
	{
		//资源未就绪
		if (!owner->particle().particle_gpu_available())
			return false;
		//常驻模式下自检会覆盖常驻状态，直接拒绝（第三百二十八轮）
		if (state().particle_resident)
			return false;
		//未绑定数据源
		if (state().particle_states == nullptr || state().particle_alive == nullptr
			|| state().particle_source_capacity == 0)
			return false;
		uint32_t capacity = state().particle_buffer_capacity;
		//镜像容量须不小于推进容量
		if (state().particle_source_capacity < capacity)
			return false;
		uint32_t bytes = capacity * static_cast<uint32_t>(PARTICLE_STRIDE);
		//上传应用层状态到前缓冲
		if (!owner->compute_buffer_write(state().particle_buffer_front, state().particle_states, bytes))
			return false;
		//绑定前缓冲只读、后缓冲只写、力场数组只读（零力场）
		if (!owner->compute_buffer_bind(state().particle_buffer_front, 0))
			return false;
		if (!owner->compute_buffer_bind(state().particle_buffer_back, 1))
			return false;
		if (!owner->compute_buffer_bind(state().particle_force_buffer, 2))
			return false;
		//零步推送常量：重力为零、dt 为零、阻尼为 1（模拟为恒等变换；力场 / 碰撞球均关闭、地面不触发）
		const float push[16] = { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, static_cast<float>(capacity), 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		if (!owner->compute_dispatch(state().particle_step_pipeline, capacity, 64u, push, sizeof(push)))
			return false;
		//回读后缓冲
		std::vector<uint8_t> output(bytes, 0u);
		if (!owner->compute_buffer_read(state().particle_buffer_back, output.data(), bytes))
			return false;
		//按活跃标记逐索引比对
		const Particle_State* source = state().particle_states;
		const uint8_t* alive = state().particle_alive;
		for (uint32_t index = 0; index < capacity; ++index)
		{
			//跳过非活跃槽位
			if (alive[index] == 0u)
				continue;
			std::size_t offset = static_cast<std::size_t>(index) * PARTICLE_STRIDE;
			if (memcmp(output.data() + offset, &source[index], PARTICLE_STRIDE) != 0)
				return false;
		}
		return true;
	}
}
