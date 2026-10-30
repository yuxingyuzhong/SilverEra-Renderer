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

	/*
	线程组数换算（第二百四十二轮）
	按「元素数 / 本地工作组大小」向上取整；elements 为 0 或 local_size 为 0 时返回 0（不调度）。
	供调用方把实例 / 像素 / 簇数量换算成 vkCmdDispatch 的组数，避免各处重复写 ceil 公式。
	*/
	constexpr uint32_t compute_group_count(uint32_t elements, uint32_t local_size)
	{
		//空输入不调度
		if (elements == 0 || local_size == 0)
			return 0;
		return (elements + local_size - 1) / local_size;
	}

	/*
	屏障用途（第二百四十二轮）
	把常见的 阶段/访问 组合收敛成枚举，调用方不再逐处手写 VkMemoryBarrier2：
	ComputeToHost = 计算写入 → 主机读（回读）；
	HostToCompute = 主机写 → 计算读（上传后立即调度）；
	ComputeToCompute = 计算写 → 计算读写（多段 dispatch 之间）；
	ComputeToGraphics = 计算写 → 顶点/片元读（GPU 剔除结果供间接绘制）；
	GraphicsToCompute = 图形写（深度/颜色）→ 计算读（Hi-Z 生成读深度）。
	*/
	enum class Compute_Barrier
	{
		ComputeToHost,
		HostToCompute,
		ComputeToCompute,
		ComputeToGraphics,
		GraphicsToCompute,
	};

	/*
	计算资源句柄（第二百四十三轮）
	与材质句柄同风格：0 = 无效，自 1 起由门面分配；槽不复用（卸载即销毁）。
	*/
	using Compute_Buffer_Handle = uint64_t;
	using Compute_Pipeline_Handle = uint64_t;

	//单个计算存储缓冲的字节上限（256 MB；防止误传导致分配失败或溢出）
	constexpr uint32_t COMPUTE_BUFFER_MAX_BYTES = 256u * 1024u * 1024u;
	//计算描述符集 set 0 的存储缓冲绑定数量上限（与 Compute_Config.storage_buffer_count 默认值一致）
	constexpr uint32_t COMPUTE_BINDING_MAX = 4;
}
