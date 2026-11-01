#pragma once
/*
粒子数据（第三百零七轮；深研方向二 阶段 A）
定位：Vulkan-free 纯数据与常量；CPU 参考模拟、GPU 缓冲上传与门面 API 共用同一布局。
约束：结构字段一律英文、下划线分隔；整体大小须与 GPU std430 布局一致（由静态断言守住）。
*/
#include <cstddef>
#include <cstdint>

//引擎命名空间
namespace engine
{
	/*
	单个粒子状态（32 字节：位置 12 ＋ 速度 12 ＋ 年龄 4 ＋ 寿命 4）
	age 达到 lifetime 即视为死亡；position / velocity 为世界坐标与每秒速度。
	*/
	struct Particle_State
	{
		float position[3];
		float velocity[3];
		float age;
		float lifetime;
	};

	//发射形状（0 = 点、1 = 盒、2 = 球壳；越界值一律拒绝）
	enum class Emit_Shape : uint32_t
	{
		Point = 0,
		Box = 1,
		Sphere_Shell = 2,
	};

	/*
	发射器（一次配置、逐帧按 count 发射）
	position = 发射中心；extents = 盒半尺寸 / 球壳半径（点发射忽略）；
	direction = 主发射方向（长度须非零，内部归一化），与随机锥混合；
	speed / lifetime 为 [min, max] 区间（min ≤ max）。
	*/
	struct Particle_Emitter
	{
		float position[3];
		float extents[3];
		float direction[3];
		float speed_min;
		float speed_max;
		float lifetime_min;
		float lifetime_max;
		uint32_t shape;
		uint32_t count;
	};

	/*
	全局模拟参数
	gravity = 每秒平方加速度；damping = 每秒速度衰减系数（0..1，1 = 不衰减）；
	floor_y = 地面高度；restitution / friction = 反弹系数与切向摩擦（0..1）。
	*/
	struct Particle_Params
	{
		float gravity[3];
		float damping;
		float floor_y;
		float restitution;
		float friction;
	};

	//单个粒子状态字节数（与 `Particle_State` 大小一致；GPU 缓冲步长）
	constexpr std::size_t PARTICLE_STRIDE = 32;
	//默认粒子容量（未显式配置时）
	constexpr uint32_t PARTICLE_CAPACITY_DEFAULT = 8192;
	//单系统粒子容量上限（1 048 576；双缓冲 × 32B ≈ 64 MB）
	constexpr uint32_t PARTICLE_CAPACITY_MAX = 1048576;
	//单帧单发射器发射数量上限
	constexpr uint32_t PARTICLE_EMIT_COUNT_MAX = 65536;

	//发射器参数是否合法（形状 / 速度 / 寿命 / 数量 / 半尺寸）
	bool particle_emitter_valid(const Particle_Emitter& emitter);
	//全局模拟参数是否合法（阻尼 / 反弹 / 摩擦）
	bool particle_params_valid(const Particle_Params& params);
}
