#pragma once
/*
粒子系统（第三百零八轮；深研方向二 阶段 A）
定位：Vulkan-free 粒子池（容量 / 活跃计数 / 分配回收）；GPU 资源与渲染接入在后续轮次由门面扩展。
约束：默认构造即为默认容量；分配走 LIFO 回收栈，保证回收后立即复用同一索引。
*/
#include "粒子数据.h"

//标准容器
#include <vector>

//引擎命名空间
namespace engine
{
	//粒子池
	class Particle_System
	{
	public:
		//容量设置（钳制到 [1, PARTICLE_CAPACITY_MAX]；收缩时丢弃超出容量的粒子）
		void capacity_set(uint32_t capacity);
		//当前容量
		uint32_t capacity_get(void) const;
		//当前活跃粒子数
		uint32_t active_count_get(void) const;
		/*
		分配一个新粒子（不初始化状态内容，由调用方填充）
		池满返回假；成功时 index_out 为池内索引（回收栈优先复用）。
		*/
		bool spawn(uint32_t& index_out);
		//回收指定索引（非法 / 重复回收返回假）
		bool recycle(uint32_t index);
		//清空全部粒子并复位回收栈（保留容量）
		void clear(void);

	private:
		//粒子状态数组（长度 = 容量）
		std::vector<Particle_State> particles_;
		//空闲索引栈（分配自尾部弹出）
		std::vector<uint32_t> free_indices_;
		//逐索引活跃标记（1 = 活跃）
		std::vector<uint8_t> alive_;
		//活跃粒子数
		uint32_t active_count_ = 0;
		//当前容量
		uint32_t capacity_ = PARTICLE_CAPACITY_DEFAULT;
	};
}
