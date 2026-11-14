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

		/*
		粒子绘制数据源（第三百一十轮；模拟状态已迁出渲染层）
		states / alive / source_capacity 由应用层绑定；本层只读取（GPU 推进回读时写入 states），
		并在其上完成上传与 billboard 绘制登记。
		*/
		bool particle_enabled = false;

		Particle_State* particle_states = nullptr;

		uint8_t* particle_alive = nullptr;

		uint32_t particle_source_capacity = 0;

		//GPU 缓冲容量（独立于绑定容量；容量变化才重建缓冲）
		uint32_t particle_capacity = PARTICLE_CAPACITY_DEFAULT;

		Particle_Params particle_params{};

		Particle_Emitter particle_emitter{};

		bool particle_emitter_configured = false;

		/*
		CPU 绘制临时缓冲（第三百六十四轮；逐帧复用）
		particle_draw 每次调用都会重建实例矩阵 / 深度 / 常驻回读三组临时数组，容量稳定时可复用底层存储，
		避免每帧重复分配（内容每次 clear 后重填，语义与局部 vector 一致）。
		*/
		std::vector<float> particle_draw_transforms;

		std::vector<float> particle_draw_depths;

		std::vector<uint8_t> particle_draw_resident_bitmap;

		std::vector<Particle_State> particle_draw_resident_states;

		std::vector<uint8_t> particle_draw_resident_alive;

		/*
		粒子 GPU 推进资源（第三百一十一轮；惰性创建）
		front / back 为双缓冲（容量 × 32 字节主机可见 SSBO）；pipeline 为推进着色器。
		*/
		Compute_Buffer_Handle particle_buffer_front = 0;

		Compute_Buffer_Handle particle_buffer_back = 0;

		//力场数组（第三百一十八轮；8 × 40 字节）
		Compute_Buffer_Handle particle_force_buffer = 0;

		/*
		常驻推进（第三百二十七轮；默认关闭）
		control 缓冲 = [0] 环形写指针、[1] 活跃计数、[2..] 活跃位图（每 32 粒子一字）；
		emit_pipeline 为「Particle发射.comp」；resident_active 为最近一次回读的 GPU 活跃数。
		*/
		bool particle_resident = false;

		Compute_Buffer_Handle particle_control_buffer = 0;

		Compute_Pipeline_Handle particle_emit_pipeline = 0;

		Compute_Pipeline_Handle particle_resident_step_pipeline = 0;

		uint32_t particle_control_bytes = 0;

		uint32_t particle_resident_active = 0;

		Compute_Pipeline_Handle particle_step_pipeline = 0;

		uint32_t particle_buffer_capacity = 0;

		/*
		常驻紧凑索引（第三百三十三轮；阶段 D）
		compact_pipeline 为「Particle紧凑.comp」，index_buffer 为 uint32 × 容量，compact_count_buffer 存原子计数；
		compact_count 为最近一次回读的索引数。
		*/
		Compute_Pipeline_Handle particle_compact_pipeline = 0;

		Compute_Buffer_Handle particle_index_buffer = 0;

		Compute_Buffer_Handle particle_compact_count_buffer = 0;

		uint32_t particle_compact_count = 0;

		//最近一次紧凑的主机侧耗时（毫秒；第三百四十轮）
		double particle_compact_ms_last = 0.0;

		//最近一次粒子绘制登记的路径（第三百四十八轮；0 未登记 / 1 CPU / 2 GPU 直接 / 3 GPU 间接）
		uint32_t particle_draw_path_last = 0;

		//最近一次 GPU 单步判定的死亡数（第三百一十三轮）
		uint32_t particle_gpu_died_last = 0;

		/*
		粒子 billboard 绘制资源（第三百一十四轮；惰性创建）
		quad 为单位四边形网格（XY 平面 ±0.5），pipeline 为「粒子.vert / 粒子.frag」Alpha 混合管线。
		*/
		Mesh_Handle particle_quad_mesh = 0;

		uint64_t particle_draw_pipeline = 0;

		std::vector<Compute_Buffer_Handle> particle_indirect_buffers;

		bool particle_indirect_written = false;

		//粒子混合档与深度写入（第三百一十六轮；切档重建粒子管线）
		Blend_Mode particle_blend_mode = Blend_Mode::Alpha;

		bool particle_depth_write = false;

		//粒子深度分桶数（第三百一十七轮；0 = 关闭）
		uint32_t particle_sort_buckets = 0;

		/*
		粒子模拟统计（第三百二十二轮）
		born / died 自 init 累计；sim_ms 为最近一次步进主机侧耗时；gpu_ms 为最近一次 GPU 调度设备耗时（−1 表示不可用）。
		*/
		uint64_t particle_born_total = 0;

		uint64_t particle_died_total = 0;

		double particle_sim_ms_last = 0.0;

		double particle_gpu_ms_last = -1.0;

		//间接命令缓冲容量（每条命令 20 字节；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_CAPACITY = 4096;

		//间接命令字节数（与 VkDrawIndexedIndirectCommand 对齐；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_BYTES = 20;

		//下一个计算缓冲句柄
		uint64_t next_compute_buffer_handle = 1;

		//下一个计算管线句柄
		uint64_t next_compute_pipeline_handle = 1;

		//当前绘制所选管线句柄（0 = 默认场景管线）
		uint64_t bound_pipeline_request = 0;

		//网格模块
		Mesh_Context mesh;

		//纹理模块
		Texture_Context texture;

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

		/*
		当前材质贴图视图（第一百七十四轮）
		依次为 法线 / 粗糙度 / 自发光 / 环境光遮蔽 / 金属度；无贴图时为内置默认白纹理视图。
		*/
		VkImageView material_views[5] = {};

		//当前 3D LUT 纹理句柄（第一百四十四轮；0 = 未绑定）
		uint64_t lut_texture = 0;

		/*
		逐物体反照率色调吸附状态（第六十九轮）
		material_tint_set 之后，后续非文字网格绘制在推送常量中携带该色调（albedo × tint）；
		material_tint_clear（或 material_bind_set 绑定无色调的材质）回落白色（画面不变）。
		mesh_draw 显式给出的 color 形参优先于该吸附状态。
		*/
		bool material_tint_active = false;

		//逐物体反照率色调（RGBA，各分量 0..1）
		float material_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		//逐物体自发光强度（第七十轮；0 = 无自发光，恒生效，无需覆盖标志）
		float material_emissive = 0.0f;

		//逐物体法线强度（第一百二十四轮；0 = 不启用，随推送常量 offset 80 的 x 分量下发）
		float material_normal_strength = 0.0f;

		//逐物体贴图 UV 缩放（第一百七十一轮；默认 (1,1)，随推送常量 offset 96 的 xy 下发）
		float material_uv_scale[2] = { 1.0f, 1.0f };

		//逐物体贴图 UV 偏移（第一百七十一轮；默认 (0,0)，随推送常量 offset 96 的 zw 下发）
		float material_uv_offset[2] = { 0.0f, 0.0f };

		//逐物体贴图 UV 旋转（第一百八十三轮；默认 0，随推送常量 offset 80 的 w 下发）
		float material_uv_rotation = 0.0f;

		//逐物体 Alpha 裁剪阈值（第一百八十三轮；默认 0 = 关闭，随推送常量 offset 80 的 z 下发）
		float material_alpha_cutoff = 0.0f;

		//逐物体清漆强度与粗糙度（第二百三十三轮；默认 0 = 关闭，随推送常量 offset 112 的 xy 下发）
		float material_clear_coat = 0.0f;

		float material_clear_coat_roughness = 0.15f;

		//顶点着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule vert_module = VK_NULL_HANDLE;

		//片元着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule frag_module = VK_NULL_HANDLE;

		//逐绘制项采样集批量写入的输入缓冲（第二百二十一轮；复用，避免每帧分配）
		std::vector<Pipeline_Context::Draw_Set_Views> draw_set_inputs;
	};
}
