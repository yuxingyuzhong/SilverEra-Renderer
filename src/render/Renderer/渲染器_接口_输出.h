#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：输出
	*/
	class Renderer_Output : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		设置超采样抗锯齿倍率（SSAA；1 = 关闭，2..4 = N×N 超采样）
		取值按 1..4 钳制；自下一次 frame_render 起生效（内部按需重建超采样目标）。
		仅在「未设置同帧后处理呈现目标且未设置本帧离屏目标」时启用，优先顺序：后处理/离屏 > SSAA。
		未初始化时静默忽略。
		*/
		void ssaa_factor_set(uint32_t factor);
		//当前超采样倍率（未初始化时为 1）
		uint32_t ssaa_factor(void) const;
		/*
		设置多重采样抗锯齿采样数（MSAA；第九十六轮）
		仅接受 2 / 4 / 8（1 = 关闭；其余请求按 1 处理），并按设备对当前颜色 / 深度格式的支持上限钳制；
		自调用后下一次 frame_render 起生效（内部重建交换链 MSAA 附件、全部渲染目标与全部图形管线）。
		与 SSAA 可叠加（两者互不替代）；未初始化时静默忽略。
		*/
		void msaa_samples_set(uint32_t samples);
		//当前 MSAA 采样数（未初始化或关闭时为 1）
		uint32_t msaa_samples(void) const;
		/*
		设置 MSAA 深度解析开关（第一百九十轮；默认开启）
		开启时采样数 > 1 的场景 pass 结束时把多重采样深度按「样本 0」解析到单采样深度图像，
		使深度类后处理（景深 / 深度描边 / 屏幕空间遮蔽）可读到有效深度；
		自调用后下一次 frame_render 起生效（不重建任何资源）；未初始化时静默忽略。
		*/
		void msaa_depth_resolve_set(bool enabled);
		//当前 MSAA 深度解析开关（未初始化时为假）
		bool msaa_depth_resolve(void) const;
		/*
		当前颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十九轮）
		返回 1 / 2 / 4 / 8；未初始化或均不支持时返回 1。
		*/
		uint32_t msaa_max_samples(void) const;
		//是否支持采样器各向异性过滤（设备创建时按特性支持情况启用；未初始化时为假）
		bool sampler_anisotropy_supported(void) const;
		//采样器最大各向异性倍数（设备上限；未初始化或未启用该特性时为 1）
		float sampler_max_anisotropy(void) const;
		//采样器 LOD 偏置绝对值上限（设备 `maxSamplerLodBias`；未初始化时为 0）
		float sampler_max_lod_bias(void) const;
		//当前交换链尺寸（窗口缩放后可能变化）
		uint32_t extent_width(void) const;
		uint32_t extent_height(void) const;
		/*
		请求改变窗口客户区尺寸（像素）
		成功返回真并置「交换链待重建」标志，交换链重建在下一次 frame_render 内完成；
		使「尺寸变化 → 交换链重建」可由上层驱动、可被用例构造。未初始化 / 尺寸为零时返回假。
		*/
		bool window_size_set(uint32_t width, uint32_t height);
		/*
		全屏切换（第一百一十一轮）
		成功返回真并置「交换链待重建」标志；隐藏窗口下后端可能拒绝，失败返回假。
		未初始化时返回假。
		*/
		bool window_fullscreen_set(bool fullscreen);
		//当前是否处于全屏（未初始化时为假）
		bool window_fullscreen(void) const;
		//窗口显示缩放比例（DPI；未初始化或查询失败时为 1）
		float display_scale(void) const;

	};
}
