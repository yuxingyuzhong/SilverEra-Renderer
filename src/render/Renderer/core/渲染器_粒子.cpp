#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"

//游戏引擎命名空间
namespace engine
{
	//构造（绑定门面；创建粒子域状态）
	Render_Particle::Render_Particle(Renderer* owner, const Render_Core_Context& core_context)
		: owner(owner), core(&core_context), state_ptr(new(std::nothrow) Particle_Domain_State())
	{
	}

	//析构
	Render_Particle::~Render_Particle(void)
	{
	}

	/*
	初始化（粒子域）
	粒子缓冲 / 管线 / 四边形网格全部按需惰性创建（见 particle_gpu_ensure 与 particle_draw_ensure），
	初始化阶段没有 GPU 资源需要建立；此处只保留域入口，供 init 流程按域顺序调用。
	*/
	void Render_Particle::init(const Renderer_Config&)
	{
		//状态缺失则无可装配
		if (state_ptr == nullptr)
			return;
	}

	/*
	关闭（粒子域资源；幂等）
	四边形网格持在网格模块内，须先于帧 / 网格上下文销毁；管线与计算资源句柄只清 0，
	实际资源由管线模块与计算上下文统一释放。资源句柄复位后即便重复调用也无副作用。
	*/
	void Render_Particle::shutdown(void)
	{
		//状态未分配则无内容可清理
		if (state_ptr == nullptr)
			return;
		//网格模块仍存活时归还粒子四边形网格
		if (owner != nullptr && (*state_ptr).particle_quad_mesh != 0)
		{
			owner->resource().mesh_unload((*state_ptr).particle_quad_mesh);
			(*state_ptr).particle_quad_mesh = 0;
		}
		/*
		绘制管线句柄
		CPU billboard 管线 / GPU 直接与间接绘制管线由管线模块在关闭流程统一销毁。
		*/
		(*state_ptr).particle_draw_pipeline = 0;
		(*state_ptr).particle_gpu_pipeline = 0;
		(*state_ptr).particle_indirect_pipeline = 0;
		(*state_ptr).particle_indirect_last_slot = 0;
		//计算资源句柄随计算上下文一并失效（第三百一十一轮；第三百二十七轮补常驻资源）
		(*state_ptr).particle_buffer_front = 0;
		(*state_ptr).particle_buffer_back = 0;
		(*state_ptr).particle_force_buffer = 0;
		(*state_ptr).particle_control_buffer = 0;
		(*state_ptr).particle_index_buffer = 0;
		(*state_ptr).particle_compact_count_buffer = 0;
		(*state_ptr).particle_indirect_buffers.clear();
		(*state_ptr).particle_indirect_written = false;
		(*state_ptr).particle_step_pipeline = 0;
		(*state_ptr).particle_emit_pipeline = 0;
		(*state_ptr).particle_resident_step_pipeline = 0;
		(*state_ptr).particle_compact_pipeline = 0;
		(*state_ptr).particle_buffer_capacity = 0;
		(*state_ptr).particle_control_bytes = 0;
		(*state_ptr).particle_resident_active = 0;
		(*state_ptr).particle_resident = false;
	}

	void Render_Particle::frame_stats_fill(Frame_Stats& stats) const
	{
		//统计非驻留路径的活跃标记
		uint32_t active_count = 0;
		if ((*state_ptr).particle_enabled && !(*state_ptr).particle_resident
			&& (*state_ptr).particle_alive != nullptr && (*state_ptr).particle_source_capacity > 0)
		{
			for (uint32_t index = 0; index < (*state_ptr).particle_source_capacity; ++index)
				if ((*state_ptr).particle_alive[index] != 0u)
					++active_count;
		}
		stats.particle_active = (*state_ptr).particle_enabled
			? ((*state_ptr).particle_resident ? (*state_ptr).particle_resident_active : active_count) : 0u;
		stats.particle_born_total = (*state_ptr).particle_born_total;
		stats.particle_died_total = (*state_ptr).particle_died_total;
		stats.particle_sim_ms = (*state_ptr).particle_sim_ms_last;
		stats.particle_gpu_ms = (*state_ptr).particle_gpu_ms_last;
		stats.particle_compact_ms = (*state_ptr).particle_compact_ms_last;
		stats.particle_draw_path = (*state_ptr).particle_draw_path_last;
	}
}
