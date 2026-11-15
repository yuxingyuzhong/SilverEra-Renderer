#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：场景
	struct Renderer_Impl_场景
	{
		std::vector<VkBuffer> light_buffers;

		std::vector<VmaAllocation> light_allocations;

		//光源记录组装缓冲（复用，避免逐帧分配）
		std::vector<Light_Record> light_records_scratch;

		//反向外扩描边的逐帧推送块（用户参数由场景域持有）
		std::array<float, 4> outline_push = { 0.0f, 0.0f, 0.0f, 0.0f };

		//远区光源空间视图投影矩阵（cascade_count >= 2 时有效；每帧更新）
		float light_view_proj_far[16] = {};

		//第三级光源空间视图投影矩阵（cascade_count == 3 时有效；每帧更新）
		float light_view_proj_far2[16] = {};

		//场景顶点着色器声明的顶点输入位置位集（第九十七轮；0 = 未解析 → 按全量处理）
		uint32_t scene_input_locations = 0;

		double last_cull_ms = 0.0;

		double cull_total_ms = 0.0;

		uint64_t last_culled_items = 0;

		//是否启用视锥剔除（第二十五轮；默认开启；关闭时全部绘制项照常登记）
		bool cull_enabled = true;

		//并行派发按用途累计（第二百二十四轮；实例剔除 / 差异扫描 / 缓存回写）
		uint64_t cull_parallel_dispatches = 0;

		//逐瓦片软度缩放起始浮点偏移（第七百零八轮）
		static constexpr std::size_t SCENE_SPOT_SOFTNESS_BASE = 940;

		//逐瓦片强度缩放起始浮点偏移（第七百零八轮）
		static constexpr std::size_t SCENE_SPOT_STRENGTH_BASE = 948;

		//每组高 8 槽占用的浮点数（位置 / 颜色 / 聚光方向 / 锥 / 光泽度）
		static constexpr std::size_t SCENE_HIGH_GROUP_FLOATS = 160;
	};
}
