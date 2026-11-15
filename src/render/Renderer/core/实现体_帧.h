#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：帧
	struct Renderer_Impl_帧
	{
		//清屏色（RGBA；由 scene_apply / clear_color_set 设置，frame_render 每帧取用）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };

		/*
		光照参数（第二十九轮方向光 / 第三十一轮点光源与高光 / 第三十二轮多光源；随场景 UBO 逐帧下发）
		默认值保证「未提供法线的网格」画面与旧版完全一致：
		方向 (0,0,-1)（朝 -Z 传播）、颜色白、强度 1、环境光 0、无点光源 → 照度为 1。
		*/
		float light_direction[3] = { 0.0f, 0.0f, -1.0f };

		/*
		距离衰减模式（第三十六轮；对点光源与聚光灯共同生效）
		0 = 线性（clamp(1 − d / radius)，默认，与引入该开关前一致）；1 = 平方反比（1 / (1 + (d / radius)²)）。
		随场景 UBO 的 `light_color.w` 逐帧下发。
		*/
		uint32_t attenuation_mode = 0;

		//是否发生过「Cluster 关闭且超过 UBO 上限」的截断告警（第二百八十轮）
		bool light_truncated = false;

		/*
		光源选择配置与诊断（第六百四十五轮；A1 / A4）
		light_select_mode = 0 声明顺序（默认）、1 重要性选择；light_budget = 0 跟随路径上限；
		light_dropped 为最近一次场景应用中被预算 / 选择淘汰的光源数（供 Frame_Stats 导出）。
		*/
		uint32_t light_select_mode = 0;

		//曝光补偿 EV（第六百六十轮；0 = 关闭；写场景 UBO 237）
		float exposure = 0.0f;

		/*
		级联纹素钳制与自动淡化（第六百六十一轮）
		shadow_texel_max = 纹素世界尺寸上限（0 = 关闭）；cascade_blend_auto = 自动淡化建议开关（默认关）；
		cascade_blend_effective = 本帧实际写入 UBO 290 的淡化比例；shadow_texel_world = 本帧生效纹素（多级取最大）。
		*/
		float shadow_texel_max = 0.0f;

		//帧模块
		Frame_Context frame;

		//最近一次完成金字塔生成的帧槽（第二百五十六轮；剔除读取该槽的上一帧金字塔）
		uint32_t last_hiz_slot = 0;

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
		//逐盏聚光灯阴影优先级（第七百零九轮；按光源槽位 0..31，默认 0 = 全部同分）
		float spot_shadow_priority[32] = {};

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
		static constexpr std::size_t SCENE_BLOCK_FLOATS = 980;

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
	};
}
