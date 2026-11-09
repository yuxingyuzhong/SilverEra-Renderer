#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：场景
	struct Renderer_Impl_场景
	{
		float light_color[3] = { 1.0f, 1.0f, 1.0f };

		float light_intensity = 1.0f;

		float light_ambient = 0.0f;

		//环境光颜色（第四十轮；默认白，与引入该参数前一致）
		float ambient_color[3] = { 1.0f, 1.0f, 1.0f };

		//半球环境光（第四十七轮）：rgb = 地面颜色、w = 混合强度（0 = 关闭，只用 ambient_color）
		float ambient_ground[4] = { 1.0f, 1.0f, 1.0f, 0.0f };

		//方向光 / 补光的逐光高光缩放（第五十轮；1 = 跟随全局，0 = 不产生高光）
		float directional_specular = 1.0f;

		float fill_specular = 1.0f;

		//镜面高光（第三十一轮；对全部光源生效）
		float specular_intensity = 0.0f;

		float specular_shininess = 16.0f;

		/*
		距离雾（第三十八轮）
		密度为 0 表示关闭（默认，画面与引入雾前一致）；开启后按「相机 → 片元」距离在起始 / 结束距离间平滑混入雾色。
		*/
		float fog_color[3] = { 0.05f, 0.05f, 0.10f };

		float fog_density = 0.0f;

		float fog_start = 1.0f;

		float fog_end = 10.0f;

		//雾模式（第一百四十三轮：0 = 线性区间、1 = 指数平方；写入 fog_range.z）
		uint32_t fog_mode = 0;

		//雾高度衰减系数（第一百四十三轮；写入 fog_range.w；0 = 关闭）
		float fog_height_falloff = 0.0f;

		uint32_t light_budget = 0;

		uint32_t light_dropped = 0;

		//视锥剔除管线句柄（懒加载；第二百四十八轮；0 = 尚未加载）
		Compute_Pipeline_Handle gpu_cull_pipeline = 0;

		//GPU 实例剔除开关（第二百四十九轮；默认关）
		bool gpu_cull_enabled = false;

		//最近一次 GPU 剔除耗时（毫秒；第二百四十九轮）
		double last_gpu_cull_ms = 0.0;

		//GPU 压缩累计派发次数（第二百五十一轮）
		uint64_t gpu_cull_dispatches = 0;

		std::vector<VkBuffer> light_buffers;

		std::vector<VmaAllocation> light_allocations;

		//光源记录组装缓冲（复用，避免逐帧分配）
		std::vector<Light_Record> light_records_scratch;

		//相机（生成每帧视图 × 投影矩阵）
		Camera camera;

		float outline_width = 0.0f;

		float outline_color[3] = { 0.0f, 0.0f, 0.0f };

		std::array<float, 4> outline_push = { 0.0f, 0.0f, 0.0f, 0.0f };

		//远区光源空间视图投影矩阵（cascade_count >= 2 时有效；每帧更新）
		float light_view_proj_far[16] = {};

		//第三级光源空间视图投影矩阵（cascade_count == 3 时有效；每帧更新）
		float light_view_proj_far2[16] = {};

		/*
		PBR-lite 镜面开关与粗糙度缩放（第六十四轮）
		关闭时完全沿用 Blinn-Phong（画面逐字节不变）；粗糙度缩放默认 1（合法 0.05..4）。
		*/
		bool pbr_enabled = false;

		float pbr_roughness = 1.0f;

		//PBR 金属度（第六十五轮；0 = 介电、1 = 纯金属；默认 0）
		float pbr_metallic = 0.0f;

		/*
		PBR 掠射高光能量修正（第六百七十二轮；D1 第一步）
		默认关闭；开启且 PBR 开启时镜面乘入射余弦，修正掠射角过亮。
		*/
		bool pbr_energy = false;

		//PBR 粗糙度绝对口径（第七百轮；材质 / 全局粗糙度直接作为 GGX 粗糙度）
		bool pbr_roughness_absolute = false;

		//场景顶点着色器声明的顶点输入位置位集（第九十七轮；0 = 未解析 → 按全量处理）
		uint32_t scene_input_locations = 0;

		double last_cull_ms = 0.0;

		double cull_total_ms = 0.0;

		uint64_t last_culled_items = 0;

		//是否启用视锥剔除（第二十五轮；默认开启；关闭时全部绘制项照常登记）
		bool cull_enabled = true;

		//并行派发按用途累计（第二百二十四轮；实例剔除 / 差异扫描 / 缓存回写）
		uint64_t cull_parallel_dispatches = 0;

		/*
		文字描边参数（第一百四十轮；经推送常量 offset 80 下发）
		text_outline_uv_width = 描边半宽（图集 UV；0 = 关闭），text_outline_color = 描边色（0..1）。
		*/
		float text_outline_uv_width = 0.0f;

		float text_outline_color[3] = { 0.0f, 0.0f, 0.0f };

		//逐物体材质粗糙度缩放（0.05..4）
		float material_pbr_roughness = 1.0f;

		//逐物体材质金属度（0..1）
		float material_pbr_metallic = 0.0f;

		//逐物体色阶数（0..8）
		float material_toon_steps = 0.0f;

		//逐物体边缘光强度（0..1）
		float material_toon_rim = 0.0f;

		//逐物体卡通高光阈值（0..1）
		float material_toon_specular = 0.0f;

		//逐物体卡通暗部色调（各分量 0..1；默认 (1,1,1) 不色偏；第四百零九轮）
		float material_toon_tint[3] = { 1.0f, 1.0f, 1.0f };

		//二次元风格化：色阶数（第一百七十四轮；0 = 关闭，1..8 = 量化档数）
		uint32_t toon_steps = 0;

		//二次元风格化：边缘光强度（第一百七十四轮；0 = 关闭，0..1）
		float toon_rim = 0.0f;

		//二次元风格化：卡通高光阈值（第一百七十九轮；0 = 关闭，0..1）
		float toon_specular = 0.0f;

		//逐瓦片软度缩放起始浮点偏移（第七百零八轮）
		static constexpr std::size_t SCENE_SPOT_SOFTNESS_BASE = 940;

		//逐瓦片强度缩放起始浮点偏移（第七百零八轮）
		static constexpr std::size_t SCENE_SPOT_STRENGTH_BASE = 948;

		//每组高 8 槽占用的浮点数（位置 / 颜色 / 聚光方向 / 锥 / 光泽度）
		static constexpr std::size_t SCENE_HIGH_GROUP_FLOATS = 160;
	};
}
