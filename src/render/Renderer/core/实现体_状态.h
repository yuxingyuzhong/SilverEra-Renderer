#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：状态
	struct Renderer_Impl_状态
	{
		Renderer_Config config;

		//补光（第四十七轮）：第二盏方向光；强度 0 = 关闭
		float fill_direction[3] = { 0.0f, 0.0f, -1.0f };

		float fill_color[3] = { 1.0f, 1.0f, 1.0f };

		float fill_intensity = 0.0f;

		bool cascade_blend_auto = false;

		float cascade_blend_effective = 0.0f;

		//间接绘制开关（第二百五十二轮；默认关）
		bool gpu_indirect_enabled = false;

		//自校准偏置的参考锥远平面（第五百二十一轮；radius 9 时与手动偏差等价）
		static constexpr float SPOT_BIAS_REFERENCE_SPAN = 9.0f;

		float cascade_far_scale = 3.0f;

		float cascade_split_distance = 12.0f;

		//级联接缝淡化带宽比例（第一百四十八轮；0 = 硬切换，0.5 = 阈值两侧各半）
		float cascade_blend = 0.0f;

		/*
		各片光源空间深度跨度（近平面到远平面；第一百二十一轮）
		片元着色器的法线偏移按「偏置 × 深度跨度」换算到世界量级，逐片跨度不同，故逐片记录。
		单级时恒为 59（与引入级联前的硬编码口径一致）。
		*/
		float cascade_depth_span[3] = { 59.0f, 59.0f, 59.0f };

		//MSAA 深度解析开关（第一百九十轮；默认开启，仅影响采样数 > 1 且走后处理链的路径）
		bool msaa_depth_resolve = true;

		/*
		泛光参数（阈值 / 软膝 / 强度 / 保留；默认 0.6 / 0.1 / 1.0 / 0）
		由 bloom_param_set 更新；后处理链各级以推送常量（offset 96）下发。
		*/
		float bloom_param[4] = { 0.6f, 0.1f, 1.0f, 0.0f };

		//是否启用静态合批（第一百二十七轮；默认开启）
		bool static_batching = true;

		/*
		间接命令缓冲（第二百五十二轮；长度 = 在途帧数，主机可见，逐帧写入合批命令）
		mapped 为持久映射指针（由缓冲创建时的 MAPPED 标志获得），录制时由帧模块直接写入。
		*/
		std::vector<VkBuffer> indirect_buffers;

		std::vector<VmaAllocation> indirect_allocations;

		std::vector<void*> indirect_mapped;

		/*
		当前绑定视图（写入各帧槽 set1 的采样源；持裸句柄）
		纹理与渲染目标共用同一绑定语义：无论来源是纹理还是渲染目标，均以「实际绑定的图像视图」记录，
		故采样器状态切换后可直接以该视图重写 set1（处置 R-FACADE-06：不再按句柄回落后覆盖目标绑定）。
		*/
		VkImageView bound_view = VK_NULL_HANDLE;

		//当前绑定采样器（逐绘制项采样集写用；第一百七十四轮）
		VkSampler bound_sampler = VK_NULL_HANDLE;

		//当前混合档（与管线模块内记录保持一致；交换链重建时随重建传入）
		Blend_Mode blend_mode = Blend_Mode::Opaque;

		//是否已初始化
		bool initialized = false;
	};
}
