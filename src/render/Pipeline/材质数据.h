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
	};
}
