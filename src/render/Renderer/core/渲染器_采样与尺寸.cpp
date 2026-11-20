#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "渲染器内部工具.h"

namespace engine
{
	//设置超采样抗锯齿倍率（SSAA）
	void Render_Device::ssaa_factor_set(uint32_t factor)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//钳制到 1..4（0 视为关闭）
		uint32_t clamped = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
		//同值重复设置为空操作
		if (clamped == state_ptr->ssaa_factor)
			return;
		state_ptr->ssaa_factor = clamped;
		//倍率变化：清失败标记，下一次 frame_render 按需重建目标
		state_ptr->ssaa_extent_failed = false;
		logger.info("渲染器:: SSAA 倍率已设为 {}（下一帧生效）", clamped);
	}

	//当前超采样倍率
	uint32_t Render_Device::ssaa_factor(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 1;
		return state_ptr->ssaa_factor;
	}

	//离屏目标默认 16F 是否开启（第 756 轮）
	bool Render_Device::hdr_targets(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->hdr_targets;
	}

	//当前生效的色调映射算子（第 776 轮；未初始化时为 0）
	uint32_t Render_Device::hdr_tone_operator(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return state_ptr->hdr_tone_operator;
	}

	/*
	设置 MSAA 采样数（第九十六轮）
	请求值按设备支持钳制后立即生效：等尽在途帧 → 重建交换链 MSAA 附件 → 重建全部渲染目标
	（句柄保持）→ 重建全部图形管线。失败时回退交换链采样数并保持原值。
	*/
	void Render_Device::msaa_samples_set(uint32_t samples)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//请求值归一（仅接受 2 / 4 / 8，其余按 1）
		if (samples != 2 && samples != 4 && samples != 8)
			samples = 1;
		//按设备对当前颜色 / 深度格式的支持钳制
		uint32_t supported = msaa_supported_max(state_ptr->device.physical_device_get(),
			state_ptr->swapchain.format_get(), state_ptr->swapchain.depth_format_get(), samples);
		//若请求不被支持则降级
		if (supported < samples)
			logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", samples, supported);
		samples = supported;
		//同值重复设置为空操作
		if (samples == state_ptr->msaa_samples)
			return;
		//等尽在途帧，确保旧附件不再被引用
		if (!state_ptr->frame.frames_wait())
		{
			logger.error("渲染器:: MSAA 切换失败：等待在途帧失败");
			return;
		}
		uint32_t previous = state_ptr->msaa_samples;
		//重建交换链 MSAA 附件（尺寸不变；旧资源进延迟销毁队列）
		VkExtent2D extent = state_ptr->swapchain.extent_get();
		state_ptr->swapchain.sample_count_set(samples);
		if (extent.width != 0 && extent.height != 0)
		{
			if (!state_ptr->swapchain_rebuild_deferred(extent.width, extent.height))
			{
				logger.error("渲染器:: MSAA 切换失败：交换链重建失败，保持 {}×", previous);
				state_ptr->swapchain.sample_count_set(previous);
				return;
			}
		}
		//重建跟随默认采样数的渲染目标（句柄不变）
		if (!owner->resource().render_target_sample_count_set(samples))
		{
			logger.warn("渲染器:: MSAA 切换时部分渲染目标重建失败");
		}
		//重建全部图形管线（采样数被烘焙进管线状态）
		if (!owner->resource().pipeline_rebuild(static_cast<uint32_t>(state_ptr->scene_color_format()),
			state_ptr->swapchain.depth_format_get(), samples))
		{
			logger.warn("渲染器:: MSAA 切换时图形管线重建失败");
		}
		state_ptr->msaa_samples = samples;
		state_ptr->config.msaa_samples = samples;
		/*
		当前绑定视图可能指向被重建目标的旧视图（已销毁），统一回落到默认白纹理；
		各帧槽后处理采样集在每帧渲染前按当前目标重写，无需在此处理。
		*/
		owner->resource().default_texture_rebind(state_ptr->config.frame_overlap);
		logger.info("渲染器:: MSAA 采样数已设为 {}×（下一帧生效）", state_ptr->msaa_samples);
	}

	//当前 MSAA 采样数
	uint32_t Render_Device::msaa_samples(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 1;
		return state_ptr->msaa_samples;
	}

	//设置 MSAA 深度解析开关（第一百九十轮）
	void Render_Device::msaa_depth_resolve_set(bool enabled)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//记录开关（下一帧起由帧模块据此决定是否取多重采样深度图像）
		state_ptr->msaa_depth_resolve = enabled;
		state_ptr->config.msaa_depth_resolve = enabled;
		logger.info("渲染器:: MSAA 深度解析已{}（下一帧生效）", enabled ? "开启" : "关闭");
	}

	/*
	逐材质色调编号通道开关（第 808 轮 · 8 位路径方案 B）
	首次开启时按需登记编号管线（极简 id 管线，R8_UINT ＋ 场景深度，LESS_OR_EQUAL 不写深度）；
	登记失败则保持关闭。默认关闭时初始化阶段不注册任何额外管线，资源计数与既有路径逐字节不变。
	*/
	void Render_Device::tone_id_pass_set(bool enabled)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//同值重复设置为空操作
		if (state_ptr->tone_id_pass == enabled)
			return;
		//开启且管线尚未登记时按需登记
		if (enabled && state_ptr->tone_id_pipeline == 0)
		{
			uint64_t handle = 0;
			if (!owner->resource().pipeline_load_ex(handle, "色调编号.vert.spv", "色调编号.frag.spv",
				Blend_Mode::Opaque, false, false, false, false, true, true,
				static_cast<uint32_t>(state_ptr->swapchain.depth_format_get()), 1,
				static_cast<uint32_t>(VK_FORMAT_R8_UINT), 0, true))
			{
				logger.error("渲染器:: 色调编号管线登记失败，通道保持关闭");
				return;
			}
			state_ptr->tone_id_pipeline = handle;
		}
		state_ptr->tone_id_pass = enabled;
		logger.info("渲染器:: 逐材质色调编号通道已{}（下一帧生效）", enabled ? "开启" : "关闭");
	}

	//当前逐材质色调编号通道开关（第 808 轮）
	bool Render_Device::tone_id_pass(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->tone_id_pass;
	}

	/*
	取指定采样数的编号管线（第 822 轮）
	单采样直接返回既有句柄；多重采样按目标采样数烘焙并在变化时重建。
	*/
	uint64_t Render_Device::tone_id_pipeline_for(uint32_t samples)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		//单采样走既有管线
		if (samples <= 1)
			return state_ptr->tone_id_pipeline;
		//采样数一致则复用
		if (state_ptr->tone_id_pipeline_msaa != 0 && state_ptr->tone_id_pipeline_msaa_samples == samples)
			return state_ptr->tone_id_pipeline_msaa;
		//采样数变化：先归还旧管线
		if (state_ptr->tone_id_pipeline_msaa != 0)
		{
			owner->resource().pipeline_unload(state_ptr->tone_id_pipeline_msaa);
			state_ptr->tone_id_pipeline_msaa = 0;
			state_ptr->tone_id_pipeline_msaa_samples = 0;
		}
		//按目标采样数登记（与单采样管线同口径，仅采样数不同）
		uint64_t handle = 0;
		if (!owner->resource().pipeline_load_ex(handle, "色调编号.vert.spv", "色调编号.frag.spv",
			Blend_Mode::Opaque, false, false, false, false, true, true,
			static_cast<uint32_t>(state_ptr->swapchain.depth_format_get()), samples,
			static_cast<uint32_t>(VK_FORMAT_R8_UINT), 0, true))
		{
			logger.error("渲染器:: 多重采样编号管线登记失败（采样数 {}）", samples);
			return 0;
		}
		state_ptr->tone_id_pipeline_msaa = handle;
		state_ptr->tone_id_pipeline_msaa_samples = samples;
		logger.info("渲染器:: 多重采样编号管线已登记（采样数 {}，句柄 {}）", samples, handle);
		return handle;
	}

	//当前 MSAA 深度解析开关（第一百九十轮）
	bool Render_Device::msaa_depth_resolve(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->msaa_depth_resolve;
	}

	//当前颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十九轮）
	uint32_t Render_Device::msaa_max_samples(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 1;
		return msaa_supported_max(state_ptr->device.physical_device_get(),
			state_ptr->swapchain.format_get(), state_ptr->swapchain.depth_format_get(), 8);
	}

	//是否支持采样器各向异性过滤
	bool Render_Device::sampler_anisotropy_supported(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->device.anisotropy_enabled_get();
	}

	//采样器 LOD 偏置绝对值上限（设备上限；第九十七轮）
	float Render_Device::sampler_max_lod_bias(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0.0f;
		return state_ptr->device.max_sampler_lod_bias_get();
	}

	//采样器最大各向异性倍数（设备上限）
	float Render_Device::sampler_max_anisotropy(void) const
	{
		//若尚未初始化则无能力可言
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 1.0f;
		return state_ptr->device.max_anisotropy_get();
	}

	//当前交换链宽度
	uint32_t Render_Device::extent_width(void) const
	{
		//若设备域状态未分配则返回零
		if (state_ptr == nullptr)
			return 0;
		return state_ptr->swapchain.extent_get().width;
	}

	//当前交换链高度
	uint32_t Render_Device::extent_height(void) const
	{
		//若设备域状态未分配则返回零
		if (state_ptr == nullptr)
			return 0;
		return state_ptr->swapchain.extent_get().height;
	}

	//当前交换链颜色格式（第 817 轮）
	uint32_t Render_Device::swapchain_format(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return static_cast<uint32_t>(state_ptr->swapchain.format_get());
	}

	//请求改变窗口客户区尺寸
	bool Render_Device::window_size_set(uint32_t width, uint32_t height)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
		{
			logger.error("渲染器:: 窗口尺寸设置失败：尚未初始化");
			return false;
		}
		//转交平台层（成功即置「交换链待重建」标志，重建在下一次 frame_render 内完成）
		return state_ptr->platform.window_size_set(width, height);
	}

}
