#include "src/render/Particle/粒子系统.h"

//引擎命名空间
namespace engine
{
	/*
	容量设置（钳制到 [1, PARTICLE_CAPACITY_MAX]）
	收缩时丢弃超出容量的粒子；容量变化后回收栈按新容量重建（活跃计数归零）。
	*/
	void Particle_System::capacity_set(uint32_t capacity)
	{
		//钳制下界
		if (capacity == 0)
			capacity = 1;
		//钳制上界
		if (capacity > PARTICLE_CAPACITY_MAX)
			capacity = PARTICLE_CAPACITY_MAX;
		capacity_ = capacity;
		particles_.resize(capacity_);
		alive_.assign(capacity_, 0u);
		//重建空闲栈（升序压栈，弹出顺序为倒序）
		free_indices_.clear();
		free_indices_.reserve(capacity_);
		for (uint32_t index = 0; index < capacity_; ++index)
			free_indices_.push_back(index);
		active_count_ = 0;
	}

	//当前容量
	uint32_t Particle_System::capacity_get(void) const
	{
		return capacity_;
	}

	//当前活跃粒子数
	uint32_t Particle_System::active_count_get(void) const
	{
		return active_count_;
	}

	/*
	分配一个新粒子索引（回收栈优先复用）
	池满返回假；成功时返回池内索引，状态内容保持调用前的值（由调用方填充）。
	*/
	bool Particle_System::spawn(uint32_t& index_out)
	{
		//池满
		if (free_indices_.empty())
			return false;
		index_out = free_indices_.back();
		free_indices_.pop_back();
		alive_[index_out] = 1u;
		++active_count_;
		return true;
	}

	/*
	回收指定索引（LIFO）
	越界或重复回收返回假；成功后该索引立即进入回收栈顶端。
	*/
	bool Particle_System::recycle(uint32_t index)
	{
		//越界
		if (index >= capacity_)
			return false;
		//重复回收
		if (alive_[index] == 0u)
			return false;
		alive_[index] = 0u;
		free_indices_.push_back(index);
		--active_count_;
		return true;
	}

	//清空全部粒子并复位回收栈（保留容量）
	void Particle_System::clear(void)
	{
		alive_.assign(capacity_, 0u);
		free_indices_.clear();
		free_indices_.reserve(capacity_);
		for (uint32_t index = 0; index < capacity_; ++index)
			free_indices_.push_back(index);
		active_count_ = 0;
	}
}
