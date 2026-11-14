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

	bool Render_Particle::particle_supported(void) const
	{
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized && owner->compute_supported();
	}

	bool Render_Particle::particle_enable_set(bool enabled)
	{
		//未初始化拒绝
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 粒子开关设置失败（未初始化）");
			return false;
		}
		state().particle_enabled = enabled;
		return true;
	}

	bool Render_Particle::particle_enable_get(void) const
	{
		//未初始化返回假
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return false;
		return state().particle_enabled;
	}

	bool Render_Particle::particle_capacity_set(uint32_t capacity)
	{
		//未初始化拒绝
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 粒子容量设置失败（未初始化）");
			return false;
		}
		//钳制下界
		if (capacity == 0)
			capacity = 1;
		//钳制上界
		if (capacity > PARTICLE_CAPACITY_MAX)
			capacity = PARTICLE_CAPACITY_MAX;
		const uint32_t old_capacity = state().particle_capacity;
		state().particle_capacity = capacity;
		//容量实际变化时才等尽在途帧并释放旧尺寸 GPU 资源（同值重复设置为空操作）
		if (old_capacity != state().particle_capacity)
		{
			owner->impl->frame.frames_wait();
			owner->particle_gpu_destroy();
		}
		//重置发射器配置（旧发射器可能已超出新容量）
		state().particle_emitter_configured = false;
		return true;
	}

	uint32_t Render_Particle::particle_capacity_get(void) const
	{
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().particle_capacity;
	}

	void Render_Particle::particle_source_bind(Particle_State* states, uint8_t* alive, uint32_t capacity)
	{
		//未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().particle_states = states;
		state().particle_alive = alive;
		state().particle_source_capacity = (states == nullptr || alive == nullptr) ? 0u : capacity;
	}

	uint32_t Render_Particle::particle_source_capacity(void) const
	{
		//未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().particle_source_capacity;
	}

	bool Render_Particle::particle_emitter_set(const Particle_Emitter& emitter)
	{
		//未初始化或未开启
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled)
		{
			logger.error("渲染器:: 粒子发射器设置失败（未初始化或未开启）");
			return false;
		}
		//参数非法
		if (!particle_emitter_valid(emitter))
		{
			logger.warn("渲染器:: 粒子发射器参数非法（已拒绝）");
			return false;
		}
		state().particle_emitter = emitter;
		state().particle_emitter_configured = true;
		return true;
	}

	Particle_Emitter Render_Particle::particle_emitter_get(void) const
	{
		//未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return Particle_Emitter{};
		return state().particle_emitter;
	}

	void Render_Particle::particle_stats_add(uint32_t born, uint32_t died)
	{
		//未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().particle_born_total += born;
		state().particle_died_total += died;
	}

	bool Render_Particle::particle_params_set(const Particle_Params& params)
	{
		//未初始化或未开启
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || !state().particle_enabled)
		{
			logger.error("渲染器:: 粒子参数设置失败（未初始化或未开启）");
			return false;
		}
		//参数非法
		if (!particle_params_valid(params))
		{
			logger.warn("渲染器:: 粒子参数非法（已拒绝）");
			return false;
		}
		state().particle_params = params;
		return true;
	}

	Particle_Params Render_Particle::particle_params_get(void) const
	{
		//未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return Particle_Params{};
		return state().particle_params;
	}
}
