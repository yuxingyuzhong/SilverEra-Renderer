#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：子系统
	struct Renderer_Impl_子系统
	{
		//着色器模块
		Shader_Context shader;

		//间接命令缓冲容量（每条命令 20 字节；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_CAPACITY = 4096;

		//间接命令字节数（与 VkDrawIndexedIndirectCommand 对齐；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_BYTES = 20;

		//本帧渲染目标句柄（0 = 直接渲染到交换链）
		uint64_t active_target = 0;

		//全屏四边形网格句柄（0 = 尚未创建；后处理 pass 的几何）
		Mesh_Handle fullscreen_mesh = 0;

		//当前 MSAA 采样数（第九十六轮；1 = 关闭）
		uint32_t msaa_samples = 1;

		//并行录制的最小绘制项数（第二百零二轮；默认 32）
		uint32_t parallel_min_items = 32;

		//并行录制的最大段数（第二百零三轮；默认 8）
		uint32_t parallel_max_segments = 8;

		uint64_t last_draw_items = 0;

		uint64_t last_merged_items = 0;

		uint64_t merged_items = 0;

		uint64_t scan_parallel_dispatches = 0;

		uint64_t cache_parallel_dispatches = 0;
	};
}
