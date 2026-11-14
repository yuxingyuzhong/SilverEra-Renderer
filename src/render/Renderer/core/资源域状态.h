#pragma once
//依赖：core/渲染器实现.h 已提供模块类型

//游戏引擎命名空间
namespace engine
{
	//材质记录（管线 ＋ 纹理 ＋ 逐物体 PBR；有效标记用于句柄槽复用）
	struct Material_Record
	{
		//图形管线句柄（0 = 默认场景管线）
		uint64_t pipeline = 0;
		//纹理句柄（0 = 内置默认白纹理）
		uint64_t texture = 0;
		//是否覆盖全局 PBR 参数（第六十八轮；false 时沿用场景 UBO 的 pbr_params）
		bool pbr_override = false;
		//逐物体材质粗糙度缩放（0.05..4；仅 pbr_override 为真时生效）
		float pbr_roughness = 1.0f;
		//逐物体材质金属度（0..1；仅 pbr_override 为真时生效）
		float pbr_metallic = 0.0f;
		//是否覆盖全局卡通参数（第四百零八轮；false 时沿用场景 UBO 的全局卡通参数）
		bool toon_override = false;
		//逐物体色阶数（0..8；仅 toon_override 为真时生效）
		float toon_steps = 0.0f;
		//逐物体边缘光强度（0..1；仅 toon_override 为真时生效）
		float toon_rim = 0.0f;
		//逐物体卡通高光阈值（0..1；仅 toon_override 为真时生效）
		float toon_specular = 0.0f;
		//逐物体卡通暗部色调（各分量 0..1；默认 (1,1,1) 不色偏；第四百零九轮）
		float toon_shadow_tint[3] = { 1.0f, 1.0f, 1.0f };
		//是否覆盖反照率色调（第六十九轮；false 时用白色）
		bool tint_override = false;
		//逐物体反照率色调（RGBA，各分量 0..1；仅 tint_override 为真时生效）
		float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//逐物体自发光强度（第七十轮；0 = 无自发光）
		float emissive = 0.0f;
		//法线贴图句柄（第一百二十四轮；0 = 无）
		uint64_t normal_texture = 0;
		//粗糙度贴图句柄（第一百二十四轮；0 = 无）
		uint64_t roughness_texture = 0;
		//法线强度（第一百二十四轮；0 = 不启用）
		float normal_strength = 0.0f;
		//自发光颜色贴图句柄（第一百六十八轮；0 = 无）
		uint64_t emissive_texture = 0;
		//环境光遮蔽贴图句柄（第一百六十九轮；0 = 无）
		uint64_t ao_texture = 0;
		//金属度贴图句柄（第一百六十九轮；0 = 无）
		uint64_t metallic_texture = 0;
		//贴图 UV 缩放（第一百七十一轮；默认 (1,1)）
		float uv_scale[2] = { 1.0f, 1.0f };
		//贴图 UV 偏移（第一百七十一轮；默认 (0,0)）
		float uv_offset[2] = { 0.0f, 0.0f };
		//贴图 UV 旋转（第一百八十三轮；弧度，默认 0 = 不旋转）
		float uv_rotation = 0.0f;
		//Alpha 裁剪阈值（第一百八十三轮；默认 0 = 关闭）
		float alpha_cutoff = 0.0f;
		//覆盖率写入（第一百二十六轮；仅 MSAA > 1× 时生效）
		bool alpha_to_coverage = false;
		//双面渲染（第一百七十二轮；true 时关闭背面剔除）
		bool double_sided = false;
		//深度写入（第一百七十三轮；false 时不写深度）
		bool depth_write = true;
		//清漆强度与粗糙度（第二百三十三轮；默认 0 = 关闭 / 0.15）
		float clear_coat = 0.0f;
		float clear_coat_roughness = 0.15f;
		//记录是否有效
		bool valid = false;
	};

	//资源域状态（从 Impl 迁出的成员，名字保持不变以减少改写）
	struct Resource_State
	{
		//网格模块
		Mesh_Context mesh;

		//纹理模块
		Texture_Context texture;

		//内置默认白纹理句柄
		uint64_t default_texture = 0;

		/*
		天空盒 / 简易 IBL（第一百二十五轮）
		默认关闭，故未启用时画面与引入前逐字节一致；sky_texture 为用户设置的立方体贴图句柄（0 = 内置纯白立方体）。
		*/
		bool sky_enabled = false;

		uint64_t sky_texture = 0;

		uint64_t default_sky_texture = 0;

		uint64_t sky_pipeline = 0;

		//漫反射（环境补光）强度
		float sky_ambient = 0.0f;

		//镜面强度
		float sky_specular = 0.0f;

		//材质集合（下标 = 句柄 - 1）
		std::vector<Material_Record> materials;

		/*
		按「混合档 × 覆盖率写入 × 双面渲染 × 深度写入」组合缓存的具名管线（第一百七十三轮扩为单表八组合）
		下标 = 档位序 × 8 + 组合位（bit2 = 关闭深度写入、bit1 = 覆盖率写入、bit0 = 双面渲染），0 表该组合尚未登记；
		材质共享同组合的具名管线（以默认场景着色器对登记），故同组合多个材质不再各自登记；
		管线所有权归门面（随 shutdown 的 extra_pipelines 统一释放），材质卸载**不**释放它。
		*/
		std::vector<uint64_t> material_pipelines;

		/*
		当前材质五槽贴图视图
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

		//逐物体材质覆盖开关与参数（第六十八轮；false 时沿用场景 UBO 的全局 pbr_params）
		bool material_pbr_active = false;

		//逐物体材质粗糙度缩放（0.05..4）
		float material_pbr_roughness = 1.0f;

		//逐物体材质金属度（0..1）
		float material_pbr_metallic = 0.0f;

		//逐物体卡通参数覆盖开关（第四百零八轮；false 时沿用场景 UBO 的全局卡通参数）
		bool material_toon_active = false;

		//逐物体色阶数（0..8）
		float material_toon_steps = 0.0f;

		//逐物体边缘光强度（0..1）
		float material_toon_rim = 0.0f;

		//逐物体卡通高光阈值（0..1）
		float material_toon_specular = 0.0f;

		//逐物体卡通暗部色调（各分量 0..1；默认 (1,1,1) 不色偏；第四百零九轮）
		float material_toon_tint[3] = { 1.0f, 1.0f, 1.0f };

		/*
		当前绑定采样源（第一百七十四轮）
		逐绘制项采样集按此快照写入；采样器状态切换后可直接以该视图重写 set1。
		*/
		VkImageView bound_view = VK_NULL_HANDLE;

		//当前绑定采样器（逐绘制项采样集写用；第一百七十四轮）
		VkSampler bound_sampler = VK_NULL_HANDLE;

		//当前绘制所选管线句柄（0 = 默认场景管线）
		uint64_t bound_pipeline_request = 0;

		//顶点着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule vert_module = VK_NULL_HANDLE;

		//片元着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule frag_module = VK_NULL_HANDLE;

		//逐绘制项采样集批量写入的输入缓冲（第二百二十一轮；复用，避免每帧分配）
		std::vector<Pipeline_Context::Draw_Set_Views> draw_set_inputs;
	};
}
