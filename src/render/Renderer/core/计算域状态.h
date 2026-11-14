#pragma once
//依赖：core/渲染器实现.h 已提供模块类型

//游戏引擎命名空间
namespace engine
{
	/*
	计算存储缓冲记录（第二百四十三轮；第二百四十四轮补设备本地标记）
	句柄自 1 起，槽不复用（卸载即销毁并移出列表）；
	host_visible 为假表示设备本地（写入经暂存上传，读回须走 compute_readback）。
	*/
	struct Compute_Buffer_Record
	{
		//门面句柄
		uint64_t handle = 0;
		//Vulkan 缓冲
		VkBuffer buffer = VK_NULL_HANDLE;
		//VMA 分配
		VmaAllocation allocation = VK_NULL_HANDLE;
		//字节数
		uint32_t size = 0;
		//是否主机可见（设备本地缓冲不可直接映射读写）
		bool host_visible = true;
	};

	/*
	计算管线条目（第二百四十三轮）
	handle = 门面句柄；context_handle = 计算上下文内部句柄；module = 着色器模块（计算域持有）。
	*/
	struct Compute_Pipeline_Record
	{
		//门面句柄
		uint64_t handle = 0;
		//计算上下文内部句柄
		uint64_t context_handle = 0;
		//着色器模块
		VkShaderModule module = VK_NULL_HANDLE;
	};

	/*
	计算域状态（从 Impl 迁出的成员，名字保持不变以减少改写）
	门面只保留域对象指针；计算上下文 / 缓冲与管线登记 / GPU 剔除与压缩 /
	Hi-Z 深度历史与金字塔 / 分簇光照与簇数据整备全部收拢在此。
	*/
	struct Compute_State
	{
		/*
		计算管线上下文（第二百四十一轮）
		设备不支持计算路径时 supported() 为假，但初始化不失败；此时全部 GPU 路径保持关闭。
		*/
		Compute_Context compute;

		//计算健康检查管线句柄（内置自检；0 = 未登记）
		uint64_t compute_health_handle = 0;

		//计算健康检查着色器模块（所有权在本域，随 shutdown 销毁）
		VkShaderModule compute_health_module = VK_NULL_HANDLE;

		//GPU 能力与降级矩阵探测结果（第二百四十六轮）
		Compute_Capabilities compute_capabilities;

		//GPU 实例压缩管线句柄（懒加载；第二百四十九轮；0 = 尚未加载）
		Compute_Pipeline_Handle gpu_compact_pipeline = 0;

		//最近一次 GPU 压缩被 Hi-Z 完全遮挡而剔除的实例数（第二百六十一轮）
		uint32_t last_gpu_occluded = 0;

		//下一个计算缓冲句柄
		uint64_t next_compute_buffer_handle = 1;

		//下一个计算管线句柄
		uint64_t next_compute_pipeline_handle = 1;

		//计算存储缓冲集合
		std::vector<Compute_Buffer_Record> compute_buffers;

		//计算管线条目集合
		std::vector<Compute_Pipeline_Record> compute_pipelines;

		//视锥剔除管线句柄（懒加载；第二百四十八轮；0 = 尚未加载）
		Compute_Pipeline_Handle gpu_cull_pipeline = 0;

		//GPU 实例剔除开关（第二百四十九轮；默认关）
		bool gpu_cull_enabled = false;

		//最近一次 GPU 剔除耗时（毫秒；第二百四十九轮）
		double last_gpu_cull_ms = 0.0;

		//GPU 压缩累计派发次数（第二百五十一轮）
		uint64_t gpu_cull_dispatches = 0;

		/*
		Hi-Z 上下文（第二百五十三轮）
		按帧槽持有深度历史与 mip 金字塔；默认关闭时不创建任何资源，首次开启时惰性构建。
		hiz_capable 为初始化时探测结果（计算可用 ＋ R32 浮点存储图像可用）。
		*/
		HiZ_Context hiz;

		bool hiz_enabled = false;

		bool hiz_capable = false;

		//Hi-Z 生成管线（第二百五十四轮；懒加载，0 = 尚未加载）
		Compute_Pipeline_Handle hiz_depth_copy_pipeline = 0;

		Compute_Pipeline_Handle hiz_downsample_pipeline = 0;

		/*
		Cluster 光照（第二百六十六轮）
		cluster_enabled 为开关（默认关）；cluster_capable 为初始化探测（计算路径可用）；
		last_cluster_light_count 为最近一帧上传的记录数。
		*/
		bool cluster_enabled = false;

		bool cluster_capable = false;

		uint32_t last_cluster_light_count = 0;

		//分簇网格（第二百六十七轮；默认 16 × 9 × 24，运行期钳制）
		Cluster_Grid cluster_grid;

		//逐簇计数管线（第二百六十八轮；懒加载，0 = 尚未加载）
		Compute_Pipeline_Handle cluster_count_pipeline = 0;

		//块内前缀和管线（第二百六十九轮；懒加载，0 = 尚未加载）
		Compute_Pipeline_Handle cluster_scan_pipeline = 0;

		//块间偏移与偏移合成管线（第二百七十轮；懒加载，0 = 尚未加载）
		Compute_Pipeline_Handle cluster_block_offset_pipeline = 0;

		Compute_Pipeline_Handle cluster_offset_apply_pipeline = 0;

		//列表写入管线（第二百七十一轮；懒加载，0 = 尚未加载）
		Compute_Pipeline_Handle cluster_list_pipeline = 0;

		//最近一次簇数据整备观测（第二百七十二轮）
		uint32_t last_cluster_entries = 0;

		double last_cluster_build_ms = 0.0;
	};
}
