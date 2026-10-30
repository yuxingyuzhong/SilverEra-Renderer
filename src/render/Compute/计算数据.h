#pragma once
#include <cstdint>
//计算管线公共语义类型（Vulkan-free）：配置与统计（第二百四十一轮）

//游戏引擎命名空间
namespace engine
{
	/*
	计算管线配置
	与图形管线配置独立：计算不做颜色 / 深度烘焙，只关心描述符集数量与推送常量尺寸。
	*/
	struct Compute_Config
	{
		//每帧槽描述符集数量（与在途帧数一致，默认 2）
		uint32_t frame_overlap = 2;
		//存储缓冲绑定数量（set 0：binding 0..count-1；默认 4，留给实例 / 剔除 / 光源 / 簇列表）
		uint32_t storage_buffer_count = 4;
		//推送常量字节数（与图形同尺寸 128，复用既有偏移约定，避免两套偏移表）
		uint32_t push_size = 128;
	};

	/*
	计算统计（第二百四十一轮先留字段；第二百四十五轮接入时间戳）
	dispatch_count = 累计调度次数；elapsed_ms = 最近一次计时（-1 表示本机不支持时间戳）。
	*/
	struct Compute_Stats
	{
		uint64_t dispatch_count = 0;
		float elapsed_ms = -1.0f;
	};
}
