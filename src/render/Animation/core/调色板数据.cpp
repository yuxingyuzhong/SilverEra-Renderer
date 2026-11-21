//获取调色板数据契约
#include "../动画数据.h"

namespace engine
{
	//以单位矩阵填充连续矩阵
	void palette_identity_fill(float* matrices, uint32_t count)
	{
		//空指针或零条直接返回
		if (matrices == nullptr || count == 0)
			return;
		for (uint32_t index = 0; index < count; ++index)
		{
			//目标矩阵起始浮点
			float* matrix = matrices + static_cast<std::size_t>(index) * ANIMATION_PALETTE_MATRIX_FLOATS;
			//先清零全部分量
			for (uint32_t element = 0; element < ANIMATION_PALETTE_MATRIX_FLOATS; ++element)
				matrix[element] = 0.0f;
			//列主序对角线写 1（0、5、10、15）
			matrix[0] = 1.0f;
			matrix[5] = 1.0f;
			matrix[10] = 1.0f;
			matrix[15] = 1.0f;
		}
	}

	//向调色板区间写入连续矩阵
	bool palette_matrix_write(float* destination, uint32_t capacity, uint32_t offset,
		const float* matrices, uint32_t count)
	{
		//空操作
		if (count == 0)
			return true;
		//空指针或越界整体拒绝
		if (destination == nullptr || matrices == nullptr || offset > capacity || count > capacity - offset)
			return false;
		//目标起始浮点（offset 条之后）
		float* target = destination + static_cast<std::size_t>(offset) * ANIMATION_PALETTE_MATRIX_FLOATS;
		//逐浮点搬运（容量已由上行约束，无溢出）
		for (uint32_t index = 0; index < count * ANIMATION_PALETTE_MATRIX_FLOATS; ++index)
			target[index] = matrices[index];
		return true;
	}
}
