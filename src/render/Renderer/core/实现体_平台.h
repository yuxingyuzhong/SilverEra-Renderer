#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：平台
	struct Renderer_Impl_平台
	{
		//平台模块
		Platform_Context platform;

		//设备模块
		Device_Context device;

		//设备是否支持间接绘制命令的 firstInstance 非零（第二百五十二轮；不支持时整轮回落逐项）
		bool indirect_first_instance_supported = false;

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
	};
}
