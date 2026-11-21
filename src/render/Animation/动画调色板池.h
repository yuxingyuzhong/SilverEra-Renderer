#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//调色板容量契约
#include "Engine/EngineRenderer/src/render/Animation/动画数据.h"

/*
渲染动画 · 调色板区间分配器（方向十 · 收敛轮 860）
本头 Vulkan-free：管理每帧槽调色板（mat4 条）的演员区间分配与回收，供多演员共存。
分配按「首个足够大的空洞」从左到右查找，末尾追加；释放仅回收区间，不移动其它演员数据。
容量上限 = ANIMATION_PALETTE_CAPACITY；无可用区间时返回 UINT32_MAX 且不改变状态。
*/
namespace engine
{
	class Animation_Palette_Pool
	{
	public:
		//默认构造（容量取调色板契约值）
		Animation_Palette_Pool(void) = default;

		//重置容量并清空全部分配（容量为 0 时按契约值兜底）
		void reset(uint32_t capacity = ANIMATION_PALETTE_CAPACITY);
		//分配 bone_count 条连续区间（成功返回起始偏移，失败返回 UINT32_MAX）
		uint32_t allocate(uint32_t bone_count);
		//释放起始偏移对应的区间（不存在返回假；成功时 count_out 写出条数）
		bool release(uint32_t offset, uint32_t* count_out = nullptr);
		//容量读取
		uint32_t capacity(void) const;
		//已占用条数
		uint32_t used(void) const;
		//区间数量
		uint32_t region_count(void) const;

	private:
		//容量（mat4 条数）
		uint32_t limit = ANIMATION_PALETTE_CAPACITY;
		//已分配区间（按起始偏移升序）
		struct Region
		{
			//起始偏移（条）
			uint32_t offset = 0;
			//条数
			uint32_t count = 0;
		};
		//区间集合
		std::vector<Region> regions;
	};
}
