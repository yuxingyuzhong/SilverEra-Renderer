#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：计算
	*/
	class Renderer_Compute : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		是否支持 GPU 时间戳（设备与图形队列族均支持时为真；未初始化时为假）
		为假时帧统计的 gpu_frame_ms 恒 0（已优雅降级，不影响渲染）。
		*/
		bool gpu_timestamp_supported(void) const;
		//是否支持像素回读（交换链图像具备传输源用法；未初始化时为假）
		bool readback_supported(void) const;
		/*
		是否支持计算路径（第二百四十一轮）
		为真表示：图形队列族含 COMPUTE 位、计算上下文构建完成、健康检查管线已登记。
		为假时全部 GPU 驱动路径（剔除 / Hi-Z / Cluster）保持关闭，初始化本身不失败。
		*/
		bool compute_supported(void) const;
		/*
		计算链路健康检查（第二百四十一轮）
		在一次性命令缓冲内完成「绑定 → 推送常量 → 调度 → 计算→主机屏障 → 回读首元素」，
		回读值应等于传入的 magic（按 uint32 位模式比较）。不支持计算或无管线时返回假。
		*/
		bool compute_health_check(uint32_t magic, uint32_t& value_out);
		/*
		计算存储缓冲（第二百四十三轮；第二百四十四轮补设备本地选项）
		默认主机可见 SSBO（`STORAGE_BUFFER` 用法），供计算着色器读写；句柄自 1 起，0 = 无效。
		size 须 > 0 且 ≤ COMPUTE_BUFFER_MAX_BYTES；data 为初值（空 = 分配后不复位）。
		device_local 为真时改为设备本地（`STORAGE_BUFFER | TRANSFER_SRC | TRANSFER_DST`）：
		初值与后续写入经暂存上传，读回须走 compute_readback（GPU 拷贝到暂存再映射）。
		*/
		bool compute_buffer_load(Compute_Buffer_Handle& handle_out, uint32_t size,
			const void* data = nullptr, bool device_local = false);
		//卸载计算存储缓冲（幂等；句柄复位为 0）
		void compute_buffer_unload(Compute_Buffer_Handle handle);
		//写入缓冲一段（主机可见缓冲直接映射写入；设备本地缓冲经暂存上传；越界或句柄无效返回假）
		bool compute_buffer_write(Compute_Buffer_Handle handle, const void* data, uint32_t size, uint32_t offset = 0);
		//读回缓冲一段（仅主机可见缓冲直接映射；设备本地缓冲请用 compute_readback）
		bool compute_buffer_read(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset = 0);
		/*
		粒子 GPU 推进资源（第三百一十一轮）
		ensure 惰性创建双缓冲（容量 × 32 字节）与推进管线；destroy 幂等释放（须先于计算上下文销毁）。
		*/
		bool particle_gpu_ensure(void);
		void particle_gpu_destroy(void);
		//粒子 billboard 绘制资源惰性创建（第三百一十四轮；单位四边形 ＋ 粒子管线）
		bool particle_draw_ensure(void);
		//粒子 GPU 绘制管线惰性创建（第三百四十一轮；粒子GPU.vert ＋ 粒子.frag，不消费实例属性）
		bool particle_gpu_draw_ensure(void);
		/*
		粒子外观推送（第三百五十四轮；第三百五十五轮补软边宽度）
		按当前粒子参数写入推送块：offset 64 = 新生颜色 rgb、offset 80 = 衰老颜色 rgb、
		offset 96.x = 颜色启用标志、offset 96.y = 软边宽度。
		未启用颜色时标志写 0（片元着色器回落内置暖白 → 暗红曲线）；软边宽度恒写（0 = 内置 0.65 内半径），
		CPU 路径占位块残留的材质数值一并被覆盖。
		*/
		void particle_style_push_fill(std::array<float, 32>& push) const;
		/*
		粒子软粒子深度源（第三百六十六轮）
		取本帧槽 HiZ 历史视图与最近邻采样器的原生句柄（uint64_t）；参数档位为 0 / HiZ 未启用 / 该槽历史未就绪时返回假。
		*/
		bool particle_depth_source(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const;
		/*
		GPU 回读通道（第二百四十四轮）
		在一次性命令缓冲内：计算→传输屏障 → `vkCmdCopyBuffer` 到暂存 → 传输→主机屏障 → 围栏等待 → 映射读回。
		与缓冲是否设备本地无关（主机可见缓冲亦可，用于统一口径的验证）。
		越界或句柄无效返回假；size 须 > 0。
		*/
		bool compute_readback(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset = 0);
		/*
		计算统计（第二百四十五轮）
		dispatch_count = 累计调度次数；elapsed_ms = 最近一次同步调度的 GPU 耗时（毫秒）。
		本机不支持时间戳时 elapsed_ms 恒为 −1（统计退化为计数，不影响调度本身）。
		*/
		Compute_Stats compute_stats_get(void) const;
		//是否支持计算侧时间戳计时（不支持时 compute_stats_get 的 elapsed_ms 恒为 −1）
		bool compute_timestamp_supported(void) const;
		/*
		GPU 能力与降级矩阵（第二百四十六轮）
		五项探测与方向一规划 §2.5 对应；未初始化时各项为假。`compute_enable = false` 时 compute 项仍如实反映设备能力。
		*/
		Compute_Capabilities compute_capabilities_get(void) const;
		/*
		视锥剔除原语（第二百四十八轮）
		输入 count 个列主序模型矩阵与网格局部 AABB：CPU 侧求世界包围球 → 写 SSBO → 计算着色器逐球测试 → 回读掩码。
		mask_out 每项 1 = 可见、0 = 剔除；长度须为 count。GPU 一律用球判定（比 AABB 保守），保证 CPU 的可见集是 GPU 结果的子集。
		默认路径不调用本接口；返回假表示未初始化 / 不支持计算 / 入参非法 / 调度或回读失败。
		*/
		bool gpu_cull_mask(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], uint32_t* mask_out);
		/*
		GPU 实例压缩（第二百四十九轮）
		与 CPU 剔除逐式同构：mvp × 8 角点 × 六面「全部角点在外」判定；可见者原子计数并写压缩矩阵。
		输出为按原相对顺序压缩的模型矩阵（每实例 16 浮点）与可见数；失败返回假（调用方回落 CPU 路径）。
		*/
		bool gpu_instance_compact(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], float* matrices_out, uint32_t* visible_out);
		/*
		带距离 / 背面剔除的实例压缩（第二百五十轮）
		camera / forward 为相机世界位置与单位前向；max_distance ≤ 0 不限距离；backface 为真时剔除位于相机后方的实例。
		与 `gpu_instance_compact` 的差别仅多这两个判据，视锥判定与压缩口径完全一致。
		*/
		bool gpu_instance_compact_ex(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], const float camera[3], const float forward[3],
			float max_distance, bool backface, float* matrices_out, uint32_t* visible_out);
		//GPU 剔除开关（默认关；为真且计算可用时实例化绘制走 GPU 压缩）
		void gpu_cull_enable_set(bool enabled);
		bool gpu_cull_enable_get(void) const;
		/*
		Hi-Z 开关与资源状态（第二百五十三轮）
		hiz_enable_set(true) 在计算路径可用时惰性创建深度历史 ＋ 金字塔资源，失败返回假且开关保持关闭；
		hiz_enable_set(false) 只关闭开关，保留已创建资源。hiz_supported() 反映「计算可用 ＋ R32 浮点存储图像可用」。
		hiz_state_get 写出当前资源尺寸与级数（未创建时全 0）；hiz_level_extent 写出指定级尺寸（越界返回假）。
		*/
		bool hiz_enable_set(bool enabled);
		bool hiz_enable_get(void) const;
		bool hiz_supported(void) const;
		bool hiz_state_get(uint32_t& width_out, uint32_t& height_out, uint32_t& level_count_out) const;
		bool hiz_level_extent(uint32_t level, uint32_t& width_out, uint32_t& height_out) const;
		//累计金字塔生成次数（第二百五十四轮；关闭或未生成时为 0）
		uint64_t hiz_generation_count(void) const;
		//指定帧槽是否已生成过有效金字塔（索引超出在途帧数返回假）
		bool hiz_history_ready(uint32_t slot) const;
		/*
		HiZ 深度历史采样句柄（第三百六十五轮；阶段 E 软粒子地基）
		该槽历史已生成时写出历史视图与最近邻采样器的原生句柄（uint64_t 承载 VkImageView / VkSampler）；
		生成结束时历史已转为 SHADER_READ_ONLY 布局，可直接以 sampler2D 采样，
		内容 = 该槽上一次使用时的场景深度（双在途帧槽下约为两帧前）。
		未启用 Hi-Z / 资源未建 / 该槽无历史时返回假且出参归零。
		*/
		bool hiz_history_sample_get(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const;
		/*
		Cluster 光照开关与资源状态（第二百六十六轮）
		cluster_enable_set(true) 在计算路径可用时惰性创建每帧槽光源记录 SSBO，失败返回假且开关保持关闭；
		关闭时只停用上传、保留已建资源。cluster_supported 反映计算路径是否可用。
		cluster_light_count 为最近一帧实际上传的光源记录数（关闭或尚未出帧时为 0）。
		*/
		bool cluster_enable_set(bool enabled);
		bool cluster_enable_get(void) const;
		bool cluster_supported(void) const;
		uint32_t cluster_light_count(void) const;
		/*
		分簇网格与 CPU 逐簇计数（第二百六十七轮）
		cluster_grid_set 按 [1,64] 逐轴钳制并保证总簇数 ≤ 4096；未初始化返回假。
		cluster_grid_get 写出当前网格（未初始化时全 0）；cluster_grid_cluster_count 返回总簇数。
		cluster_counts_reference_ex 用当前相机与光源记录写出逐簇计数（CPU 参考，供 268 轮 GPU 计数对数）；
		counts_out 长度须 ≥ 总簇数；成功返回真并写出总簇数，容量不足或未初始化返回假。
		*/
		bool cluster_grid_set(uint32_t x, uint32_t y, uint32_t z);
		bool cluster_grid_get(uint32_t& x_out, uint32_t& y_out, uint32_t& z_out) const;
		uint32_t cluster_grid_cluster_count(void) const;
		bool cluster_counts_reference_ex(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out) const;
		/*
		GPU 逐簇计数（第二百六十八轮）
		与 CPU 参考同口径：上传光源记录与清零计数缓冲 → 每记录一线程调度 → 回读逐簇计数。
		成功返回真并写出总簇数；不支持计算 / 容量不足 / 调度或回读失败返回假。
		*/
		bool cluster_counts_gpu(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out);
		/*
		块内前缀和（第二百六十九轮）
		CPU 参考与 GPU 同口径：按 CLUSTER_SCAN_BLOCK（256）分块的包含式前缀和 ＋ 每块总和。
		GPU 路径写出 prefix_out（长度 count）与 block_totals_out（长度 ceil(count/256)）；
		成功返回真并写出块数；不支持计算 / 入参非法 / 调度失败返回假。
		*/
		bool cluster_prefix_block_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
			uint32_t* block_totals_out, uint32_t block_capacity, uint32_t* block_count_out) const;
		bool cluster_prefix_block_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
			uint32_t* block_totals_out, uint32_t block_count_capacity, uint32_t* block_count_out);
		/*
		全量前缀和（第二百七十轮）
		CPU 参考与 GPU 同口径的包含式全量前缀和（块内前缀 ＋ 块间偏移）。
		GPU 路径：块内扫描 → 块总和串行扫描（排他偏移）→ 逐元素叠加偏移 → 回读。
		成功返回真；不支持计算 / 入参非法 / 调度失败返回假。
		*/
		bool cluster_prefix_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out) const;
		bool cluster_prefix_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out);
		/*
		簇光源列表（第二百七十一轮）
		以逐簇计数为输入，写出逐簇排他偏移（offsets_out）与「簇 → 光源索引」列表（list_out）。
		entries_out 为列表总条目（= 计数总和）；容量不足 / 未初始化 / 不支持计算返回假。
		CPU 路径按光源顺序写入（确定性）；GPU 路径簇内顺序由原子竞争决定，成员与 CPU 一致。
		*/
		bool cluster_list_build_cpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
			const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out) const;
		bool cluster_list_build_gpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
			const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out);
		/*
		簇数据整备（第二百七十二轮）
		一次调用完成「逐簇计数 → 排他偏移 → 光源列表」；use_gpu 为真且计算可用时走 GPU 路径，否则走 CPU 参考。
		counts_out / offsets_out 长度须 ≥ 网格总簇数；list_out 长度须 ≥ 条目总数（= 计数之和）。
		成功返回真并写出条目数；未初始化 / 容量不足 / 入参非法返回假。同时更新构建耗时与条目观测值。
		*/
		bool cluster_data_build(uint32_t* counts_out, uint32_t* offsets_out, uint32_t* list_out,
			uint32_t cluster_capacity, uint32_t list_capacity, uint32_t* entries_out, bool use_gpu);
		//最近一次簇数据整备的条目数（未构建时为 0）
		uint32_t cluster_entries_get(void) const;
		//最近一次簇数据整备耗时（毫秒；未构建时为 0）
		double cluster_build_ms_get(void) const;
		/*
		计算管线（第二百四十三轮）
		按 `.spv` 文件名加载并登记（入口固定 main）；句柄自 1 起，0 = 无效。
		卸载时同时释放管线与着色器模块。
		*/
		bool compute_pipeline_load(Compute_Pipeline_Handle& handle_out, const std::string& shader_name);
		//卸载计算管线（幂等；句柄复位为 0）
		void compute_pipeline_unload(Compute_Pipeline_Handle handle);
		//把存储缓冲绑定到计算集 set 0 的指定 binding（binding < COMPUTE_BINDING_MAX）
		bool compute_buffer_bind(Compute_Buffer_Handle buffer, uint32_t binding);
		/*
		同步调度（第二百四十三轮）
		一次性命令缓冲内：激活管线 → 写推送常量 → 按 elements / local_size 换算组数调度 →
		计算→主机屏障 → 围栏等待。返回即可读回结果；elements 为 0 视为无需调度（返回真）。
		*/
		bool compute_dispatch(Compute_Pipeline_Handle pipeline, uint32_t elements, uint32_t local_size,
			const void* push_data = nullptr, uint32_t push_size = 0);
	};
}
