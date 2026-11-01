#include "src/render/Particle/局部命名空间使用.h"

//std::isfinite
#include <cmath>

//引擎命名空间
namespace engine
{
	namespace
	{
		//浮点是否有限
		bool finite(float value)
		{
			return std::isfinite(value);
		}

		//浮点是否落在 [0, 1]
		bool unit_interval(float value)
		{
			return finite(value) && value >= 0.0f && value <= 1.0f;
		}
	}

	/*
	发射器参数是否合法
	形状 / 数量 / 速度区间 / 寿命区间 / 半尺寸 / 方向长度 / 全部字段有限性；
	任一不满足即返回假（调用方拒绝本次配置并保持既有发射器）。
	*/
	bool particle_emitter_valid(const Particle_Emitter& emitter)
	{
		//形状越界（只允许 点 / 盒 / 球壳）
		if (emitter.shape > static_cast<uint32_t>(Emit_Shape::Sphere_Shell))
			return false;
		//发射数量
		if (emitter.count == 0 || emitter.count > PARTICLE_EMIT_COUNT_MAX)
			return false;
		//速度区间（min 非负、max ≥ min）
		if (!finite(emitter.speed_min) || emitter.speed_min < 0.0f)
			return false;
		if (!finite(emitter.speed_max) || emitter.speed_max < emitter.speed_min)
			return false;
		//寿命区间（min 正、max ≥ min）
		if (!finite(emitter.lifetime_min) || emitter.lifetime_min <= 0.0f)
			return false;
		if (!finite(emitter.lifetime_max) || emitter.lifetime_max < emitter.lifetime_min)
			return false;
		//位置 / 半尺寸 / 方向逐分量
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			if (!finite(emitter.position[axis]) || !finite(emitter.extents[axis]) || !finite(emitter.direction[axis]))
				return false;
			//半尺寸非负
			if (emitter.extents[axis] < 0.0f)
				return false;
		}
		//方向长度非零（点发射亦要求方向合法，便于统一归一化）
		const float length_squared = emitter.direction[0] * emitter.direction[0]
			+ emitter.direction[1] * emitter.direction[1]
			+ emitter.direction[2] * emitter.direction[2];
		return length_squared > 1e-12f;
	}

	/*
	全局模拟参数是否合法
	重力 / 地面高度有限；阻尼 / 反弹 / 摩擦落在 [0, 1]。
	*/
	bool particle_params_valid(const Particle_Params& params)
	{
		for (uint32_t axis = 0; axis < 3; ++axis)
			if (!finite(params.gravity[axis]))
				return false;
		if (!finite(params.floor_y))
			return false;
		if (!unit_interval(params.damping))
			return false;
		if (!unit_interval(params.restitution))
			return false;
		return unit_interval(params.friction);
	}
}
