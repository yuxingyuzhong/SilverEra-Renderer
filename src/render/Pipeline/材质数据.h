#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//混合档（Vulkan-free；与管线数据.h 同构）
#include "Application/Renderer/src/render/Pipeline/管线数据.h"

/*
材质公共语义类型（Vulkan-free）
本头与「网格数据.h」「管线数据.h」「纹理数据.h」同构：只放渲染层公共面需要暴露的引擎语义类型，
不含任何 Vulkan / VMA / SDL3 / glm 类型；由门面公共头（渲染器.h）引用，
避免公共头为了一个结构而引入层内实现细节头（管线.h 含 Vulkan 类型）。
*/
namespace engine
{
	//材质句柄（0 表示无效）
	using Material_Handle = uint64_t;

	/*
	材质描述（Vulkan-free）
	聚合「图形管线 ＋ 纹理 ＋ 混合档」，供上层以数据驱动方式提交绘制，替代逐项散装绑定
	（pipeline_bind_set ＋ texture_bind_set 的散装调用）。
	pipeline：具名管线句柄。为 0 时由门面按 blend 决定实际管线（Opaque 档沿用默认场景管线，
	          非 Opaque 档以默认场景着色器对登记一条具名管线）；非 0 时沿用该具名管线，blend 被忽略
	          （混合档在 pipeline_load 登记时已确定）。
	texture：纹理句柄。为 0 时回落内置默认白纹理。
	blend：混合档。仅当 pipeline == 0 时生效。
	逐物体 PBR（第六十八轮）：pbr_override 为真时，该材质绑定的后续绘制按本材质的
	粗糙度缩放 / 金属度着色（随实例属性下发），否则沿用场景 UBO 的全局 pbr_params。
	注意：采样器为纹理模块共享并烘焙进各帧槽 set1，故材质不携带采样器状态；
	      采样器状态仍由渲染器门面 texture_sampler_state_set 全局控制。
	*/
	struct Material_Data
	{
		//图形管线句柄（0 = 由门面按 blend 决定）
		uint64_t pipeline = 0;
		//纹理句柄（0 = 内置默认白纹理）
		uint64_t texture = 0;
		//混合档（仅 pipeline == 0 时生效）
		Blend_Mode blend = Blend_Mode::Opaque;
		//是否覆盖全局 PBR 参数（第六十八轮；false 时沿用场景全局参数）
		bool pbr_override = false;
		//逐物体材质粗糙度缩放（合法域 (0.05, 4]；仅 pbr_override 为真时生效）
		float pbr_roughness = 1.0f;
		//逐物体材质金属度（合法域 [0, 1]；仅 pbr_override 为真时生效）
		float pbr_metallic = 0.0f;
		//是否覆盖反照率色调（第六十九轮；false 时用白色，画面不变）
		bool tint_override = false;
		//逐物体反照率色调（RGBA，各分量 0..1；仅 tint_override 为真时生效）
		float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//逐物体自发光强度（第七十轮；0 = 无自发光，恒生效）
		float emissive = 0.0f;
		/*
		PBR 贴图化（第一百二十四轮）
		normal_texture：法线贴图句柄（0 = 无，按几何法线着色）。
		roughness_texture：粗糙度贴图句柄（0 = 无，按 1 处理）。
		normal_strength：法线强度（0 = 不启用；合法域 (0, 4]）。
		无贴图路径（三者皆默认）与引入贴图前逐字节一致。
		*/
		uint64_t normal_texture = 0;
		uint64_t roughness_texture = 0;
		float normal_strength = 0.0f;
		/*
		自发光贴图（第一百六十八轮）
		emissive_texture：自发光颜色贴图句柄（0 = 无，按纯白乘子处理，画面与引入前逐字节一致）；
		与标量 emissive 相乘：自发光 = 反照率 × 强度 × 贴图颜色。
		*/
		uint64_t emissive_texture = 0;
		/*
		环境光遮蔽与金属度贴图（第一百六十九轮）
		ao_texture：环境光遮蔽贴图句柄（0 = 无，按 1 处理）；取红通道乘到环境光基底与天空 IBL 漫反射上。
		metallic_texture：金属度贴图句柄（0 = 无，按 1 处理）；取红通道乘到金属度上（仅 PBR 开启时参与着色）。
		两者默认（无贴图）与引入前逐字节一致。
		*/
		uint64_t ao_texture = 0;
		uint64_t metallic_texture = 0;
		/*
		贴图 UV 变换（第一百七十一轮）
		全部材质贴图（反照率 / 法线 / 粗糙度 / 自发光 / 遮蔽 / 金属度）共用同一变换：
		uv' = uv × uv_scale + uv_offset；默认 scale (1,1) 与 offset (0,0) 时 uv' = uv，画面逐字节不变。
		*/
		float uv_scale[2] = { 1.0f, 1.0f };
		float uv_offset[2] = { 0.0f, 0.0f };
		/*
		覆盖率写入（第一百二十六轮）
		true 时该材质的共享管线启用 alphaToCoverage（仅 MSAA 采样数 > 1 时生效）；
		false（默认）保持既有 alpha 混合，1× 下两者逐字节一致。
		*/
		bool alpha_to_coverage = false;
	};
}
