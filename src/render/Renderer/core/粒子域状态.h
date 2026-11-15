#pragma once
//依赖：渲染器内部状态.h 已提供模块类型

//游戏引擎命名空间
namespace engine
{
	/*
	粒子域状态
	门面只保留域对象指针；粒子绘制源 / GPU 缓冲 / 绘制资源 / 统计全部收拢在此。
	*/
	struct Particle_Domain_State
	{
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

		/*
		粒子 GPU 绘制管线（第三百四十一轮；不消费实例属性，按 gl_InstanceIndex 取紧凑索引）
		与间接绘制管线 / 帧槽（第三百四十二轮）同属粒子绘制资源，一并收拢在本域。
		*/
		uint64_t particle_gpu_pipeline = 0;

		Compute_Pipeline_Handle particle_indirect_pipeline = 0;

		uint32_t particle_indirect_last_slot = 0;

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
	};
}
