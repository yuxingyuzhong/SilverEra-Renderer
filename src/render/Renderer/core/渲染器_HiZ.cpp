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

namespace engine
{
	//是否支持 GPU 时间戳

	bool Renderer_Compute::hiz_enable_set(bool enabled)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//关闭只置标志
		if (!enabled)
		{
			impl->hiz_enabled = false;
			return true;
		}
		//已就绪直接开启
		if (impl->hiz.valid())
		{
			impl->hiz_enabled = true;
			return true;
		}
		//惰性构建资源
		if (!impl->hiz_ensure())
			return false;
		/*
		懒加载两条生成管线（第二百五十四轮）
		深度拷贝与逐级降采样；任一加载失败即保持关闭（避免「启用但生成被跳过」的假开启）。
		*/
		if (impl->hiz_depth_copy_pipeline == 0
			&& !self().compute_pipeline_load(impl->hiz_depth_copy_pipeline, "HiZ深度拷贝.comp.spv"))
		{
			logger.warn("渲染器:: Hi-Z 深度拷贝管线加载失败（开关保持关闭）");
			return false;
		}
		if (impl->hiz_downsample_pipeline == 0
			&& !self().compute_pipeline_load(impl->hiz_downsample_pipeline, "HiZ降采样.comp.spv"))
		{
			logger.warn("渲染器:: Hi-Z 降采样管线加载失败（开关保持关闭）");
			return false;
		}
		impl->hiz_enabled = true;
		return true;
	}

	bool Renderer_Compute::hiz_enable_get(void) const
	{
		return impl != nullptr && impl->hiz_enabled;
	}

	bool Renderer_Compute::hiz_supported(void) const
	{
		//未初始化或能力不足
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->hiz_capable && impl->compute.supported();
	}

	bool Renderer_Compute::hiz_state_get(uint32_t& width_out, uint32_t& height_out, uint32_t& level_count_out) const
	{
		//出参先归零
		width_out = 0;
		height_out = 0;
		level_count_out = 0;
		//资源未创建
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		width_out = impl->hiz.width_get();
		height_out = impl->hiz.height_get();
		level_count_out = impl->hiz.level_count();
		return true;
	}

	bool Renderer_Compute::hiz_level_extent(uint32_t level, uint32_t& width_out, uint32_t& height_out) const
	{
		//出参先归零
		width_out = 0;
		height_out = 0;
		//资源未创建
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		return impl->hiz.level_extent_get(level, width_out, height_out);
	}

	uint64_t Renderer_Compute::hiz_generation_count(void) const
	{
		//未初始化或无资源
		if (impl == nullptr || !impl->hiz.valid())
			return 0;
		return impl->hiz.generation_count();
	}

	bool Renderer_Compute::hiz_history_ready(uint32_t slot) const
	{
		//未初始化或无资源
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		return impl->hiz.history_valid(slot);
	}

	bool Renderer_Compute::hiz_history_sample_get(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const
	{
		//默认无有效句柄
		view_out = 0;
		sampler_out = 0;
		//未初始化 / 未启用 / 资源未建 / 该槽无历史
		if (impl == nullptr || !impl->initialized || !impl->hiz_enabled || !impl->hiz.valid()
			|| !impl->hiz.history_valid(slot))
			return false;
		VkImageView history_view = impl->hiz.depth_history_view(slot);
		VkSampler depth_sampler = impl->hiz.depth_sampler_get();
		//句柄齐备才算成功
		if (history_view == VK_NULL_HANDLE || depth_sampler == VK_NULL_HANDLE)
			return false;
		view_out = reinterpret_cast<uint64_t>(history_view);
		sampler_out = reinterpret_cast<uint64_t>(depth_sampler);
		return true;
	}
}
