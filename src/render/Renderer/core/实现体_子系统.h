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
