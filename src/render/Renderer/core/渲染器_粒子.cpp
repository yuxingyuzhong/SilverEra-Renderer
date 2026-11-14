#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"

//游戏引擎命名空间
namespace engine
{
	//构造（绑定门面；创建粒子域状态）
	Render_Particle::Render_Particle(Renderer* owner)
		: owner(owner), state_ptr(new(std::nothrow) Particle_Domain_State())
	{
	}

	//析构
	Render_Particle::~Render_Particle(void)
	{
	}

	//粒子域状态（迁移期直取）
	Particle_Domain_State& Render_Particle::state(void)
	{
		return *state_ptr;
	}

	//粒子域状态（迁移期直取，只读）
	const Particle_Domain_State& Render_Particle::state(void) const
	{
		return *state_ptr;
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
		if (owner != nullptr && owner->impl != nullptr && state().particle_quad_mesh != 0)
		{
			owner->mesh_unload(state().particle_quad_mesh);
			state().particle_quad_mesh = 0;
		}
		/*
		绘制管线句柄
		CPU billboard 管线 / GPU 直接与间接绘制管线由管线模块在关闭流程统一销毁。
		*/
		state().particle_draw_pipeline = 0;
		state().particle_gpu_pipeline = 0;
		state().particle_indirect_pipeline = 0;
		state().particle_indirect_last_slot = 0;
		//计算资源句柄随计算上下文一并失效（第三百一十一轮；第三百二十七轮补常驻资源）
		state().particle_buffer_front = 0;
		state().particle_buffer_back = 0;
		state().particle_force_buffer = 0;
		state().particle_control_buffer = 0;
		state().particle_index_buffer = 0;
		state().particle_compact_count_buffer = 0;
		state().particle_indirect_buffers.clear();
		state().particle_indirect_written = false;
		state().particle_step_pipeline = 0;
		state().particle_emit_pipeline = 0;
		state().particle_resident_step_pipeline = 0;
		state().particle_compact_pipeline = 0;
		state().particle_buffer_capacity = 0;
		state().particle_control_bytes = 0;
		state().particle_resident_active = 0;
		state().particle_resident = false;
	}
}
