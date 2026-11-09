#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：资源
	struct Renderer_Impl_资源
	{
		/*
		阴影 pass 诊断（第六百四十七轮；A4 补齐）
		last_shadow_passes = 最近一帧实际执行的阴影 pass 数（方向光级联 ＋ 聚光瓦片；复用帧为 0）；
		last_spot_shadow_passes = 其中聚光瓦片 pass 数。
		*/
		uint32_t last_shadow_passes = 0;

		uint32_t last_spot_shadow_passes = 0;

		/*
		PCSS 软阴影配置（第六百五十七轮）
		默认关闭；开启后着色器以 16 点阻塞物搜索 ＋ 16 点可变半径 PCF 取代硬边 / 3×3 软度口径。
		*/
		bool shadow_pcss_enabled = false;

		float shadow_pcss_search = 24.0f;

		float shadow_pcss_max = 8.0f;

		//逐盏聚光灯阴影偏置缩放（第六百五十八轮；按前向槽位，默认 1）
		float spot_shadow_bias_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		//逐盏聚光灯阴影质量缩放（第七百零八轮；软度 / 强度，按前向槽位，默认 1）
		float spot_shadow_softness_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		float spot_shadow_strength_scale[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		//阴影分辨率预算（第六百五十九轮；0 = 关闭；门面 shadow_budget_set 的存值副本）
		uint64_t shadow_budget_bytes = 0;

		float shadow_texel_world = 0.0f;

		//最近一次阴影重绘的命令缓冲提交次数（第六百六十五轮；C2 基线，现口径等于片数）
		uint32_t last_shadow_submits = 0;

		//阴影批量提交开关（第六百六十九轮；默认关，开启且段数 > 1 时一次提交）
		bool shadow_batch_submit = false;

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
		light_buffers / light_allocations 按帧槽持有光源记录 SSBO（仅开启时创建）；
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

		//帧内簇数据整备复用缓冲（第二百七十三轮；避免逐帧分配）
		std::vector<uint32_t> cluster_counts_scratch;

		std::vector<uint32_t> cluster_offsets_scratch;

		std::vector<uint32_t> cluster_list_scratch;

		/*
		图形 set0 的簇数据存储缓冲（第二百八十八轮；按帧槽持有）
		binding 2 = 逐簇计数、3 = 逐簇排他偏移、4 = 光源列表；仅 Cluster 开启时惰性构建，
		列表按条目数增长重建（计数 / 偏移容量 = 簇数）。
		*/
		std::vector<VkBuffer> cluster_counts_buffers;

		std::vector<VmaAllocation> cluster_counts_allocations;

		std::vector<VkBuffer> cluster_offsets_buffers;

		std::vector<VmaAllocation> cluster_offsets_allocations;

		std::vector<VkBuffer> cluster_list_buffers;

		std::vector<VmaAllocation> cluster_list_allocations;

		//已建容量（簇数 / 列表条目数）；增长时才重建
		uint32_t cluster_counts_capacity = 0;

		uint32_t cluster_list_capacity = 0;

		//Cluster 关闭时的占位存储缓冲（set0 binding 1..4 共用；第二百八十八轮）
		VkBuffer cluster_placeholder_buffer = VK_NULL_HANDLE;

		VmaAllocation cluster_placeholder_allocation = VK_NULL_HANDLE;

		//最近一帧上传到图形 set0 的簇数据字节数（第二百八十八轮；关闭恒 0）
		uint32_t last_cluster_gpu_bytes = 0;

		/*
		按「混合档 × 覆盖率写入 × 双面渲染 × 深度写入」组合缓存的具名管线（第一百七十三轮扩为单表八组合）
		下标 = 档位序 × 8 + 组合位（bit2 = 关闭深度写入、bit1 = 覆盖率写入、bit0 = 双面渲染），0 表该组合尚未登记；
		材质共享同组合的具名管线（以默认场景着色器对登记），故同组合多个材质不再各自登记；
		管线所有权归门面（随 shutdown 的 extra_pipelines 统一释放），材质卸载**不**释放它。
		*/
		std::vector<uint64_t> material_pipelines;

		/*
		原生 UI（保留模式控件树 + GPU 绘制）
		input 为渲染层自持的帧输入聚合器；ui 为控件树与交互；ui_render 负责 UI 阶段的 GPU 绘制。
		两个 UI 管线由 ui_pipeline_ensure 惰性登记，着色器模块随 extra_pipelines 统一销毁。
		*/
		Input_Aggregator input;

		//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
		Render_Target_Context render_target;

		//阴影图默认边长（第五十二轮；固定默认 1024，兼顾精度与开销）
		static constexpr uint32_t SHADOW_SIZE = 1024;

		//阴影图实际边长（第五十四轮；可配 512 / 1024 / 2048）
		uint32_t shadow_size = SHADOW_SIZE;

		//上一帧阴影输入签名（第五十五轮；未就绪时为 0）
		uint64_t shadow_signature = 0;

		//上一帧阴影图是否可用（第五十五轮；重建目标 / 关闭时置假）
		bool shadow_pass_ready = false;

		//累计实际执行的阴影 pass 次数（第五十五轮；观测与测试）
		uint64_t shadow_pass_count = 0;

		//累计阴影重绘次数（第一百四十五轮）
		uint64_t shadow_redraws = 0;

		//累计阴影图复用次数（签名一致跳过重绘；第一百四十五轮）
		uint64_t shadow_reuses = 0;

		//最近一次阴影重绘的绘制项数（第一百四十五轮）
		uint32_t last_shadow_items = 0;

		//最近一帧是否重绘了阴影图（1 = 重绘、0 = 复用或未执行；第一百四十五轮）
		uint32_t shadow_last_redrawn = 0;

		//最近一帧参与投影的聚光灯阴影瓦片数（第五百二十轮；0..8）
		uint32_t spot_shadow_tiles = 0;

		//最近一次阴影 pass 的并行段数（第二百一十二轮；0 = 单线程录制）
		uint32_t last_shadow_parallel_segments = 0;

		//阴影管线句柄（第五十二轮；0 = 未登记）
		uint64_t shadow_pipeline = 0;

		//阴影目标句柄（第五十二轮；0 = 未创建）
		uint64_t shadow_target = 0;

		//聚光灯阴影瓦片数（第五百一十三轮由 4 扩为 8）
		static constexpr std::size_t SPOT_SHADOW_TILES = 8;

		/*
		聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张；第五百一十三轮扩为 8 张）
		与 shadow_target 同参数的独立阴影图，采样绑定 set 1 binding 9 的数组元素 0..7；
		元素 0 与旧单盏行为兼容，其余七张供多盏聚光灯各自投影。
		*/
		/*
		聚光阴影图集（第六百七十轮；C1）
		atlas_target = 单张 4S × 2S 目标（0 = 未创建）；atlas = 实际生效（创建成功且开关打开）；
		atlas_requested = 数据层请求（创建失败时保持请求，避免逐帧重试）。
		*/
		uint64_t spot_shadow_atlas_target = 0;

		bool spot_shadow_atlas = false;

		bool spot_shadow_atlas_requested = false;

		uint64_t spot_shadow_targets[SPOT_SHADOW_TILES] = {};

		//聚光灯阴影矩阵（第四百八十三轮；第五百零七轮扩为 4 份；第五百一十三轮扩为 8 份，关闭时为零矩阵）
		float spot_view_proj_tiles[SPOT_SHADOW_TILES][16] = {};

		//聚光灯阴影深度跨度（第四百八十四轮；第五百零七轮扩为 4 份；第五百一十三轮扩为 8 份，0 = 该瓦片无有效聚光灯）
		float spot_shadow_spans[SPOT_SHADOW_TILES] = {};

		/*
		逐瓦片有效阴影偏置（第五百二十一轮）
		默认等于 spot_shadow_bias；开启自校准后按「参考跨度 9 / 该瓦片跨度」缩放，
		使不同半径的聚光灯在相同参数下得到接近一致的世界法线偏移。
		*/
		float spot_shadow_bias_tiles[SPOT_SHADOW_TILES] = {};

		/*
		逐瓦片质量缩放（第七百零八轮；按前向槽位 0..7 写入）
		软度缩放 / 强度缩放，默认 1（与全局参数相同，画面逐字节不变）。
		*/
		float spot_shadow_softness_tiles[SPOT_SHADOW_TILES] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		float spot_shadow_strength_tiles[SPOT_SHADOW_TILES] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

		//聚光灯阴影偏置自校准开关（第五百二十一轮；默认关闭，画面与引入前逐字节一致）
		bool spot_shadow_bias_auto = false;

		//阴影是否启用（第五十二轮；默认关闭，保持既有画面逐字节不变）
		bool shadow_enabled = false;

		/*
		聚光灯阴影（第四百七十四轮）
		开启时阴影矩阵改为「第 1 盏聚光灯的透视视图投影」，复用方向光阴影图与阴影 pass；
		着色器以 cascade_params.x = 0 判定聚光模式（方向光阴影让位）。
		*/
		bool spot_shadow_enabled = false;

		/*
		聚光灯阴影质量参数（第四百七十六轮）
		与方向光阴影参数相互独立：偏置 0..0.05（默认 0.010）、软度 0..1（默认 0）、强度 0..1（默认 1）。
		*/
		float spot_shadow_bias = 0.010f;

		float spot_shadow_softness = 0.0f;

		float spot_shadow_strength = 1.0f;

		//聚光灯阴影序号（第四百七十九轮；指向已登记聚光灯槽位 0..7，越界视为无聚光阴影）
		int spot_shadow_index = 0;

		/*
		多盏聚光灯阴影（第五百零七轮）
		开启时前 4 盏聚光灯按声明顺序各占瓦片 0..3，各自独立阴影图；
		关闭时沿用 spot_shadow_index 的单盏语义（写入瓦片 0），画面与引入前一致。
		*/
		bool spot_shadow_all = false;

		/*
		逐盏聚光阴影开关（第六百四十八轮；A2 数据化）
		按前向槽位（0..7）控制该盏聚光灯是否参与阴影；默认全开（与引入前逐字节一致）。
		槽位超出 8（簇路径压缩口径）时该数组不适用，对应光源按旧口径参与投影。
		*/
		bool spot_shadow_light_enabled[8] = { true, true, true, true, true, true, true, true };

		/*
		阴影光源正交视锥（第一百零七轮）
		默认值与引入该参数前一致：半宽 / 半高 12、近 1、远 60。
		*/
		float shadow_half_extent = 12.0f;

		float shadow_near = 1.0f;

		float shadow_far = 60.0f;

		/*
		级联阴影（第一百二十一轮；第一百四十六轮扩为三级）
		cascade_count 为 1 时沿用上面单级口径（画面逐字节不变）；为 2 / 3 时阴影图按象限切 2 / 3 片：
		各片覆盖以相机为中心、半径 split × far_scale^(片序) 的包围球，
		片元按「与相机的距离」在 split、split × far_scale 处选择级联。
		每片占 shadow_size / 级数 边长，故片内分辨率随级数下降（级联的固有代价）。
		*/
		uint32_t cascade_count = 1;

		//阴影深度偏置（第五十二轮）
		float shadow_bias = 0.003f;

		//阴影软度（第五十三轮；0 = 单点硬边，≥0.5 = 3×3 PCF 平均）
		float shadow_softness = 0.0f;

		//阴影强度（第五十三轮；0 = 阴影无效、1 = 全遮蔽）
		float shadow_strength = 1.0f;

		//本帧「渲染到目标并同帧后处理呈现」的目标句柄（0 = 关闭；见 render_target_present_set）
		uint64_t present_target = 0;

		/*
		本帧「同帧后处理链」的额外中间目标句柄（下标 1..N-1；空 = 只有 present_target 一个目标）
		由 render_target_present_chain_set 设置；链上第 i 级取 targets[i % 目标数]，故链长 ≥ 2 时至少需 2 个目标。
		*/
		std::vector<uint64_t> present_extra;

		//后处理链级数上限（超出即整链拒绝；每级占用一次全屏 pass，超长链会带来隐性性能坍塌）

		//后处理管线句柄（0 = 尚未登记；首次使用时懒登记，不透明档；仅用于既有单级默认路径）
		uint64_t post_pipeline = 0;

		/*
		SSAA 超采样状态（倍率钳制 1..4；1 = 关闭）
		scene_target 为输出 × 倍率的场景目标，resolve_target 为输出尺寸的解析目标；
		extent_* / target_factor 记录建立时的参数，变化即重建；extent_failed 避免逐帧重复失败尝试。
		*/
		uint32_t ssaa_factor = 1;

		uint64_t ssaa_scene_target = 0;

		uint64_t ssaa_resolve_target = 0;

		uint32_t ssaa_extent_width = 0;

		uint32_t ssaa_extent_height = 0;

		uint32_t ssaa_target_factor = 0;

		//SSAA 降采样具名管线句柄（0 = 尚未登记）
		uint64_t ssaa_pipeline = 0;

		/*
		天空盒 / 简易 IBL（第一百二十五轮）
		默认关闭，故未启用时画面与引入前逐字节一致；启用后场景片元按法线 / 反射方向采样立方体贴图作补光，
		并由全屏天空项绘制背景。sky_texture 为用户设置的立方体贴图句柄（0 = 内置纯白立方体）。
		*/
		bool sky_enabled = false;

		uint64_t sky_texture = 0;

		uint64_t default_sky_texture = 0;

		uint64_t sky_pipeline = 0;

		//漫反射（环境补光）强度
		float sky_ambient = 0.0f;

		//镜面强度
		float sky_specular = 0.0f;

		/*
		后处理采样集绑定签名（第九十七轮）
		记录上次写入的目标视图组与采样器；未变化则跳过逐帧重写，避免更新在途描述符集。
		*/
		VkImageView post_bound_views[PRESENT_TARGET_MAX] = {};

		uint32_t post_bound_view_count = 0;

		VkSampler post_bound_sampler = VK_NULL_HANDLE;

		//后处理采样集 binding 2 已绑定的场景深度视图（第一百二十三轮）
		VkImageView post_bound_depth = VK_NULL_HANDLE;

		//后处理采样集 binding 4 已绑定的方向光阴影视图（第三百九十九轮）
		VkImageView post_bound_shadow = VK_NULL_HANDLE;

		bool post_bind_cached = false;

		//当前尺寸 / 倍率下超采样目标创建失败（避免逐帧重复尝试）
		bool ssaa_extent_failed = false;

		//设备是否支持 144 字节推送块（仅决定 extra 写入接口是否可用）
		bool post_extra_supported = false;

		//最近一次阴影 pass 的 GPU 耗时（毫秒；第一百二十八轮；未启用 / 无能力时 0）
		double shadow_gpu_ms = 0.0;

		//内置默认白纹理句柄
		uint64_t default_texture = 0;

		//二次元风格化：暗部色调（第一百七十八轮；RGB 0..1，默认 (1,1,1) = 关闭）
		float toon_shadow_tint[3] = { 1.0f, 1.0f, 1.0f };
	};
}
