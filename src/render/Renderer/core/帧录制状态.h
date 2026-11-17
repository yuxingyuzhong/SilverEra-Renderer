#pragma once
//帧模块与线程池完整类型
#include "Engine/EngineRenderer/src/render/Frame/帧上下文.h"
#include "Engine/EngineRenderer/src/render/Thread/线程池.h"

//游戏引擎命名空间
namespace engine
{
	//设备域帧录制状态（帧暂存、实例与间接缓冲、剔除和合批统计）
	struct Device_Frame_State
	{
		/*
		最近一帧的即时观测（第一百三十二轮）
		frame_culled_items 为「本帧已登记阶段」累计、在 frame_render 收尾时转存到 last_culled_items 并清零；
		last_draw_items / last_instances 在合批之后、清空待绘制项之前取用（故与累计口径一致）。
		*/
		uint64_t frame_culled_items = 0;

		/*
		实例级剔除（第一百五十三轮）
		culled_instances 为自 init 以来因视锥剔除而未写入实例缓冲的实例数；
		frame_culled_instances 为当前帧累计，帧收尾转存 last_culled_instances 后清零；
		instanced_cull_scratch 为筛选可见实例的复用缓冲（避免每次调用分配）。
		*/
		uint64_t culled_instances = 0;

		uint64_t frame_culled_instances = 0;

		uint64_t last_culled_instances = 0;

		std::vector<float> instanced_cull_scratch;

		/*
		实例级剔除的可见性掩码（第一百七十七轮）
		并行判定时每个实例只写自己的掩码槽（1 = 可见、0 = 剔除），压缩阶段仍单线程按原顺序执行，
		故实例顺序（即绘制顺序）与单线程路径一致；按实例上限预留，避免逐次调用分配。
		*/
		std::vector<uint8_t> instanced_cull_mask;

		/*
		实例缓冲差异掩码（第一百七十七轮）
		按「一个实例 = 80 字节」逐实例比较上一次写入内容，并行只写各自的掩码槽；
		连续变化段仍单线程按掩码扫描，故上传区间与单线程路径完全一致。
		*/
		std::vector<uint8_t> instance_changed_mask;

		//实例级剔除转为并行的最小实例数（默认 256；第二百二十二轮起由配置覆盖）
		std::size_t cull_parallel_min_instances = 256;

		//实例缓冲差异扫描 / 缓存回写转为并行的最小实例数（默认 512；第二百二十六轮起由配置覆盖）
		std::size_t instance_scan_parallel_min = 512;

		uint64_t last_instances = 0;

		/*
		实例缓冲上传缓存（第六十一轮）
		逐帧槽保存上次写入的实例属性（帧槽数 × instance_capacity × 20 浮点）与已写入实例数；
		instance_block_cached 标记该槽缓存是否已建立（首次访问须整块写入）。
		*/
		std::vector<float> instance_block_cache;

		std::vector<uint32_t> instance_block_count;

		std::vector<uint8_t> instance_block_cached;

		//累计实例缓冲上传字节数（第六十一轮；仅供 instance_upload_bytes 观测，不随帧重置）
		uint64_t instance_upload_bytes = 0;

		//实例缓冲（长度 = 在途帧数，主机可见，逐帧写入本帧全部实例模型矩阵）
		std::vector<VkBuffer> instance_buffers;

		//实例缓冲内存（与 instance_buffers 一一对应）
		std::vector<VmaAllocation> instance_allocations;

		//每帧槽实例缓冲容量（实例个数）
		uint32_t instance_capacity = 256;

		//本帧待绘制的实例属性（列主序 16 浮点模型矩阵 ＋ 4 浮点材质 × 实例数；按登记顺序连续存放，一次写入帧槽实例缓冲）
		std::vector<std::array<float, INSTANCE_FLOAT_COUNT>> pending_instances;

		//累计场景 UBO 上传字节数（第六十轮；仅供 scene_upload_bytes 观测，不随帧重置）
		uint64_t scene_upload_bytes = 0;

		/*
		反向外扩描边（第二百二十八轮）
		width = 外扩宽度（世界单位；0 = 关闭），color = 描边色（RGB 0..1）；
		outline_pipeline 为「剔除正面」的具名场景管线（登记失败时为 0，此时描边自动失效）；
		outline_push 为逐帧推送块（x = 宽度、yzw = 颜色），绘制项持其指针直到 frame_render 消费。
		*/
		uint64_t outline_pipeline = 0;

		//聚光灯阴影槽位映射（第五百零七轮；前向槽位各自对应的瓦片号，-1 = 不投影；第七百零九轮扩为 32 槽）
		float spot_shadow_slot_tiles[32] = {
			-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
			-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
		};
		//光源空间视图投影矩阵（第五十二轮；每帧按方向光计算，同时写入 UBO 与阴影绘制推送常量）
		float light_view_proj[16] = {};

		//累计因视锥剔除而未登记的绘制项数（第二十三轮；仅供 frame_stats_get 观测）
		uint64_t culled_items = 0;

		//当前帧 / 最近一帧 / 累计的主机侧剔除耗时（毫秒；第二百四十七轮）
		double frame_cull_ms = 0.0;

		/*
		线程池（第一百七十七轮）
		render_threads > 0 时启动工作线程，把「实例级剔除」与「实例缓冲差异扫描」按块分派；
		0（默认）不启动任何线程，ranges_run 退化为调用线程整段执行，行为与引入并行前逐字节一致。
		*/
		Thread_Pool thread_pool;

		/*
		合批收益观测（第一百三十五轮）
		frame_merged_items 为本帧（主 pass ＋ 阴影各片）被合并掉的项数，帧收尾转存 last_merged_items 并清零；
		merged_items 为自 init 以来累计被合并项数。
		*/
		uint64_t frame_merged_items = 0;

		//当前生效的渲染工作线程数（第一百七十七轮；0 = 单线程）
		uint32_t render_threads = 0;

		//本帧推送常量存储（每项 32 浮点 = mat4 矩阵 ＋ 色调 ＋ 材质贴图参数 ＋ UV 变换 ＋ 清漆；deque 保证元素引用稳定，绘制项持其指针）
		std::deque<std::array<float, 32>> pending_push_constants;

		//场景 UBO 缓冲（长度 = 在途帧数，主机可见，逐帧写入相机矩阵）
		std::vector<VkBuffer> scene_ubos;

		//场景 UBO 内存（与 scene_ubos 一一对应）
		std::vector<VmaAllocation> scene_ubo_allocs;

		/*
		场景 UBO 上传缓存（第六十轮）
		逐帧槽保存上一次已写入的 160 浮点（每个槽一段），用于按变化区间写入；
		scene_block_cached 标记该槽缓存是否已建立（首次访问须整块写入）。
		*/
		std::vector<float> scene_block_cache;

		std::vector<uint8_t> scene_block_cached;

		/*
		场景 UBO 浮点数与字节数（第一百七十九轮抽出常量）
		浮点布局：0 视图投影、16 光照块 … 288 第三级参数、292 卡通高光参数（vec4）、296 簇参数（vec4）；
		第二百八十五轮由 296 扩为 300（字节 1184 → 1200），追加的 vec4 供下一窗口着色器 Cluster 采样读取。
		*/
		//场景 UBO 浮点数（456 个 32 位浮点 = 1824 字节；第五百零七轮追四份聚光矩阵 / 跨度 / 槽位映射，第五百一十三轮扩为 8 份，第五百二十一轮追逐瓦片偏置）
		/*
		场景 UBO 浮点规模（第六百七十二轮 460；第六百九十九轮 620；第七百零一轮 940；第七百零八轮 956；第七百零九轮 980）
		456..459 = `lighting_extra`；460..619 = 高 8..15 槽；620..779 = 16..23 槽；780..939 = 24..31 槽（每组 160 浮点）；
		940..947 = 逐瓦片软度缩放（8）；948..955 = 逐瓦片强度缩放（8）；956..979 = 槽位 8..31 的瓦片映射（24）。
		*/
		static constexpr std::size_t SCENE_BLOCK_FLOATS = 992;

		//槽位 8..31 的瓦片映射起始浮点偏移（第七百零九轮）
		static constexpr std::size_t SCENE_SPOT_SLOTS_HI_BASE = 956;

		//旧段光源槽位数（UBO 原偏移保持不变）
		static constexpr uint32_t UBO_LEGACY_SLOTS = 8;

		//高 8 槽在场景 UBO 中的起始浮点偏移（= 620 − 160）
		static constexpr std::size_t SCENE_HIGH_SLOTS_BASE = 460;

		//场景 UBO 字节数（浮点数 × 4）
		static constexpr std::size_t SCENE_BLOCK_BYTES = SCENE_BLOCK_FLOATS * sizeof(float);

		//待绘制项（由 mesh_draw 逐次登记，frame_render 消费后清空；同帧可登记多个网格）
		std::vector<Frame_Render_Item> pending_items;

		//合批复用缓冲（第一百三十六轮；避免每帧为合并结果分配新向量）
		std::vector<Frame_Render_Item> batch_scratch;

		/*
		阴影列表复用缓冲（第一百八十二轮）
		阴影 pass 与后处理链原本逐帧在栈上新建 std::vector 并 reserve，
		改为复用这两个成员（每帧 clear 后 push_back），避免逐帧堆分配与释放；
		内容在 frame_render 返回前不再被使用（render_to_target / render_chain_present 均为同步调用）。
		*/
		std::vector<Frame_Render_Item> shadow_items_scratch;

		//已呈现帧数
		uint64_t frame_count = 0;

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

		//间接命令缓冲容量（每条命令 20 字节；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_CAPACITY = 4096;

		//间接命令字节数（与 VkDrawIndexedIndirectCommand 对齐；第二百五十二轮）
		static constexpr uint32_t INDIRECT_COMMAND_BYTES = 20;

		//并行录制的最小绘制项数（第二百零二轮；默认 32）
		uint32_t parallel_min_items = 32;

		//并行录制的最大段数（第二百零三轮；默认 8）
		uint32_t parallel_max_segments = 8;

		uint64_t last_draw_items = 0;

		uint64_t last_merged_items = 0;

		uint64_t merged_items = 0;

		uint64_t scan_parallel_dispatches = 0;

		uint64_t cache_parallel_dispatches = 0;

		//间接绘制开关（第二百五十二轮；默认关）
		bool gpu_indirect_enabled = false;

		//是否启用静态合批（第一百二十七轮；默认开启）
		bool static_batching = true;

		/*
		间接命令缓冲（第二百五十二轮；长度 = 在途帧数，主机可见，逐帧写入合批命令）
		mapped 为持久映射指针（由缓冲创建时的 MAPPED 标志获得），录制时由帧模块直接写入。
		*/
		std::vector<VkBuffer> indirect_buffers;

		std::vector<VmaAllocation> indirect_allocations;

		std::vector<void*> indirect_mapped;

	};
}
