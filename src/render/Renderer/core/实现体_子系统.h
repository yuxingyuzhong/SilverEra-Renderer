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

		/*
		计算管线上下文（第二百四十一轮）
		设备不支持计算路径时 supported() 为假，但初始化不失败；此时全部 GPU 路径保持关闭。
		*/
		Compute_Context compute;

		//计算健康检查管线句柄（内置自检；0 = 未登记）
		uint64_t compute_health_handle = 0;

		//计算健康检查着色器模块（所有权在本实现体，随 shutdown 销毁）
		VkShaderModule compute_health_module = VK_NULL_HANDLE;

		//GPU 能力与降级矩阵探测结果（第二百四十六轮）
		Compute_Capabilities compute_capabilities;

		//GPU 实例压缩管线句柄（懒加载；第二百四十九轮；0 = 尚未加载）
		Compute_Pipeline_Handle gpu_compact_pipeline = 0;

		//最近一次 GPU 压缩被 Hi-Z 完全遮挡而剔除的实例数（第二百六十一轮）
		uint32_t last_gpu_occluded = 0;

		//间接命令缓冲容量（每条命令 20 字节；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_CAPACITY = 4096;

		//间接命令字节数（与 VkDrawIndexedIndirectCommand 对齐；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_BYTES = 20;

		//下一个计算缓冲句柄
		uint64_t next_compute_buffer_handle = 1;

		//下一个计算管线句柄
		uint64_t next_compute_pipeline_handle = 1;

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
