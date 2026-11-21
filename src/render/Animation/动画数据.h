#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

/*
渲染动画 · 调色板数据契约（方向十 · 阶段 A 第 3 轮）
本头 Vulkan-free：只描述调色板容量与矩阵写入规则，供动画系统（后续轮次）与测试共用。
调色板按帧槽各持一份 GPU 存储缓冲（set 0 binding 5），每条为 4×4 列主序矩阵（16 浮点 / 64 字节）；
初始内容全部为单位矩阵，故「无动画」时蒙皮着色退化为原样输出，画面与引入前一致。
*/
namespace engine
{
	//每帧槽调色板容量（mat4 条数）
	inline constexpr uint32_t ANIMATION_PALETTE_CAPACITY = 1024;
	//单演员骨骼数上限（按 4 骨骼蒙皮管线预留）
	inline constexpr uint32_t ANIMATION_ACTOR_BONES_MAX = 64;
	//单条调色板矩阵的浮点数（4×4）
	inline constexpr uint32_t ANIMATION_PALETTE_MATRIX_FLOATS = 16;
	//单条调色板矩阵的字节数（16 浮点 × 4）
	inline constexpr uint32_t ANIMATION_PALETTE_MATRIX_BYTES = 64;

	//以单位矩阵填充连续 count 条（列主序；空指针或 count 为 0 时不写）
	void palette_identity_fill(float* matrices, uint32_t count);
	/*
	向已分配的调色板区间写入连续矩阵
	offset / count 以「条」计；越界（offset + count > capacity）或参数非法时整体拒绝、不做部分写。
	成功返回真；count 为 0 视为空操作并返回真。
	*/
	bool palette_matrix_write(float* destination, uint32_t capacity, uint32_t offset,
		const float* matrices, uint32_t count);
}
