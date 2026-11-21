//获取调色板区间分配器
#include "../动画调色板池.h"

namespace engine
{
	//重置容量并清空分配
	void Animation_Palette_Pool::reset(uint32_t requested_capacity)
	{
		limit = requested_capacity != 0 ? requested_capacity : ANIMATION_PALETTE_CAPACITY;
		regions.clear();
	}

	//分配连续区间
	uint32_t Animation_Palette_Pool::allocate(uint32_t bone_count)
	{
		//零条或超出容量
		if (bone_count == 0 || bone_count > limit)
			return UINT32_MAX;
		//候选起点（从 0 开始，逐个区间之后尝试）
		uint32_t candidate = 0;
		for (std::size_t index = 0; index < regions.size(); ++index)
		{
			const Region& region = regions[index];
			//当前空洞足够大则分配
			if (region.offset >= candidate && region.offset - candidate >= bone_count)
			{
				regions.insert(regions.begin() + index, Region{ candidate, bone_count });
				return candidate;
			}
			//跳过该区间
			if (region.offset + region.count > candidate)
				candidate = region.offset + region.count;
		}
		//末尾剩余空间
		if (limit - candidate >= bone_count)
		{
			regions.push_back(Region{ candidate, bone_count });
			return candidate;
		}
		return UINT32_MAX;
	}

	//释放区间
	bool Animation_Palette_Pool::release(uint32_t offset, uint32_t* count_out)
	{
		if (count_out != nullptr)
			*count_out = 0;
		for (std::size_t index = 0; index < regions.size(); ++index)
		{
			//起始偏移一致即命中
			if (regions[index].offset != offset)
				continue;
			if (count_out != nullptr)
				*count_out = regions[index].count;
			regions.erase(regions.begin() + index);
			return true;
		}
		return false;
	}

	//容量读取
	uint32_t Animation_Palette_Pool::capacity(void) const
	{
		return limit;
	}

	//已占用条数
	uint32_t Animation_Palette_Pool::used(void) const
	{
		uint32_t total = 0;
		for (const Region& region : regions)
			total += region.count;
		return total;
	}

	//区间数量
	uint32_t Animation_Palette_Pool::region_count(void) const
	{
		return static_cast<uint32_t>(regions.size());
	}
}
