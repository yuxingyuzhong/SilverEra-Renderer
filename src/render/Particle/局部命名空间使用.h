#pragma once
//粒子模块内部命名空间引入（仅 `.cpp` 使用；对外接口见 `粒子数据.h`）
#include "src/render/Particle/粒子数据.h"

#include <cstddef>
#include <cstdint>

using engine::Emit_Shape;
using engine::PARTICLE_CAPACITY_DEFAULT;
using engine::PARTICLE_CAPACITY_MAX;
using engine::PARTICLE_EMIT_COUNT_MAX;
using engine::PARTICLE_STRIDE;
using engine::Particle_Emitter;
using engine::Particle_Params;
using engine::Particle_State;
