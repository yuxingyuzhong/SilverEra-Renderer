#pragma once
//阴影目标与帧项语义类型
#include "Engine/EngineRenderer/src/render/Frame/帧上下文.h"

//游戏引擎命名空间
namespace engine
{
	//设备域阴影资源与帧内诊断状态
	struct Device_Shadow_State
	{
		/*
		阴影 pass 诊断（第六百四十七轮；A4 补齐）
		last_shadow_passes = 最近一帧实际执行的阴影 pass 数（方向光级联 ＋ 聚光瓦片；复用帧为 0）；
		last_spot_shadow_passes = 其中聚光瓦片 pass 数。
		*/
		uint32_t last_shadow_passes = 0;

		uint32_t last_spot_shadow_passes = 0;

		float shadow_texel_world = 0.0f;

		//最近一次阴影重绘的命令缓冲提交次数（第六百六十五轮；C2 基线，现口径等于片数）
		uint32_t last_shadow_submits = 0;

		//阴影图默认边长（第五十二轮；固定默认 1024，兼顾精度与开销）
		static constexpr uint32_t SHADOW_SIZE = 1024;

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

		//最近一次阴影 pass 的 GPU 耗时（毫秒；第一百二十八轮；未启用 / 无能力时 0）
		double shadow_gpu_ms = 0.0;

	};
}
