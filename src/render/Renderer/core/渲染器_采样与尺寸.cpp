#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
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
	void Renderer_Output::ssaa_factor_set(uint32_t factor)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//钳制到 1..4（0 视为关闭）
		uint32_t clamped = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
		//同值重复设置为空操作
		if (clamped == impl->ssaa_factor)
			return;
		impl->ssaa_factor = clamped;
		//倍率变化：清失败标记，下一次 frame_render 按需重建目标
		impl->ssaa_extent_failed = false;
		logger.info("渲染器:: SSAA 倍率已设为 {}（下一帧生效）", clamped);
	}

	//当前超采样倍率
	uint32_t Renderer_Output::ssaa_factor(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return impl->ssaa_factor;
	}

	/*
	设置 MSAA 采样数（第九十六轮）
	请求值按设备支持钳制后立即生效：等尽在途帧 → 重建交换链 MSAA 附件 → 重建全部渲染目标
	（句柄保持）→ 重建全部图形管线。失败时回退交换链采样数并保持原值。
	*/
	void Renderer_Output::msaa_samples_set(uint32_t samples)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//请求值归一（仅接受 2 / 4 / 8，其余按 1）
		if (samples != 2 && samples != 4 && samples != 8)
			samples = 1;
		//按设备对当前颜色 / 深度格式的支持钳制
		uint32_t supported = msaa_supported_max(impl->device.physical_device_get(),
			impl->swapchain.format_get(), impl->swapchain.depth_format_get(), samples);
		//若请求不被支持则降级
		if (supported < samples)
			logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", samples, supported);
		samples = supported;
		//同值重复设置为空操作
		if (samples == impl->msaa_samples)
			return;
		//等尽在途帧，确保旧附件不再被引用
		if (!impl->frame.frames_wait())
		{
			logger.error("渲染器:: MSAA 切换失败：等待在途帧失败");
			return;
		}
		uint32_t previous = impl->msaa_samples;
		//重建交换链 MSAA 附件（尺寸不变；旧资源进延迟销毁队列）
		VkExtent2D extent = impl->swapchain.extent_get();
		impl->swapchain.sample_count_set(samples);
		if (extent.width != 0 && extent.height != 0)
		{
			if (!impl->swapchain_rebuild_deferred(extent.width, extent.height))
			{
				logger.error("渲染器:: MSAA 切换失败：交换链重建失败，保持 {}×", previous);
				impl->swapchain.sample_count_set(previous);
				return;
			}
		}
		//重建跟随默认采样数的渲染目标（句柄不变）
		if (!impl->render_target.sample_count_set(samples))
		{
			logger.warn("渲染器:: MSAA 切换时部分渲染目标重建失败");
		}
		//重建全部图形管线（采样数被烘焙进管线状态）
		if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(),
			impl->swapchain.depth_format_get(), samples))
		{
			logger.warn("渲染器:: MSAA 切换时图形管线重建失败");
		}
		impl->msaa_samples = samples;
		impl->config.msaa_samples = samples;
		/*
		当前绑定视图可能指向被重建目标的旧视图（已销毁），统一回落到默认白纹理；
		各帧槽后处理采样集在每帧渲染前按当前目标重写，无需在此处理。
		*/
		{
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			if (impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
			{
				impl->bound_view = default_view;
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.texture_bind(slot, default_sampler, default_view);
			}
		}
		logger.info("渲染器:: MSAA 采样数已设为 {}×（下一帧生效）", impl->msaa_samples);
	}

	//当前 MSAA 采样数
	uint32_t Renderer_Output::msaa_samples(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return impl->msaa_samples;
	}

	//设置 MSAA 深度解析开关（第一百九十轮）
	void Renderer_Output::msaa_depth_resolve_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//记录开关（下一帧起由帧模块据此决定是否取多重采样深度图像）
		impl->msaa_depth_resolve = enabled;
		impl->config.msaa_depth_resolve = enabled;
		logger.info("渲染器:: MSAA 深度解析已{}（下一帧生效）", enabled ? "开启" : "关闭");
	}

	//当前 MSAA 深度解析开关（第一百九十轮）
	bool Renderer_Output::msaa_depth_resolve(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->msaa_depth_resolve;
	}

	//当前颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十九轮）
	uint32_t Renderer_Output::msaa_max_samples(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return msaa_supported_max(impl->device.physical_device_get(),
			impl->swapchain.format_get(), impl->swapchain.depth_format_get(), 8);
	}

	//是否支持采样器各向异性过滤
	bool Renderer_Output::sampler_anisotropy_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->device.anisotropy_enabled_get();
	}

	//采样器 LOD 偏置绝对值上限（设备上限；第九十七轮）
	float Renderer_Output::sampler_max_lod_bias(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->device.max_sampler_lod_bias_get();
	}

	//采样器最大各向异性倍数（设备上限）
	float Renderer_Output::sampler_max_anisotropy(void) const
	{
		//若尚未初始化则无能力可言
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->device.max_anisotropy_get();
	}

	//当前交换链宽度
	uint32_t Renderer_Output::extent_width(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().width;
	}

	//当前交换链高度
	uint32_t Renderer_Output::extent_height(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().height;
	}

	//请求改变窗口客户区尺寸
	bool Renderer_Output::window_size_set(uint32_t width, uint32_t height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 窗口尺寸设置失败：尚未初始化");
			return false;
		}
		//转交平台层（成功即置「交换链待重建」标志，重建在下一次 frame_render 内完成）
		return impl->platform.window_size_set(width, height);
	}

}
