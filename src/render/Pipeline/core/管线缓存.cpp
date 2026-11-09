#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	bool Pipeline_Context::cache_set(const void* data, std::size_t size)
	{
		//前置：逻辑设备必须就绪
		if (device == VK_NULL_HANDLE)
		{
			logger.error("管线:: 设置管线缓存时逻辑设备为空");
			return false;
		}
		//销毁旧缓存（已创建的图形管线不受影响）
		if (pipeline_cache != VK_NULL_HANDLE)
		{
			vkDestroyPipelineCache(device, pipeline_cache, nullptr);
			pipeline_cache = VK_NULL_HANDLE;
		}
		//以外部数据重建缓存
		VkPipelineCacheCreateInfo cache_info{};
		cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		cache_info.initialDataSize = (data != nullptr && size != 0) ? size : 0;
		cache_info.pInitialData = cache_info.initialDataSize != 0 ? data : nullptr;
		if (!vk_result_check(vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache（外部数据）"))
		{
			//失败时重建空缓存，保证后续管线创建仍可用
			cache_info.initialDataSize = 0;
			cache_info.pInitialData = nullptr;
			vk_result_check(vkCreatePipelineCache(device, &cache_info, nullptr, &pipeline_cache), "vkCreatePipelineCache（空）");
			return false;
		}
		logger.info("管线:: 管线缓存已载入（{} 字节）", static_cast<uint32_t>(size));
		return true;
	}

	bool Pipeline_Context::cache_get(std::vector<uint8_t>& bytes_out) const
	{
		//出参先清空
		bytes_out.clear();
		//前置：缓存与设备必须就绪
		if (device == VK_NULL_HANDLE || pipeline_cache == VK_NULL_HANDLE)
			return false;
		//先取所需字节数
		std::size_t size = 0;
		if (!vk_result_check(vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr), "vkGetPipelineCacheData（长度）"))
			return false;
		//空缓存无可保存内容
		if (size == 0)
			return false;
		//取回内容
		bytes_out.resize(size);
		if (!vk_result_check(vkGetPipelineCacheData(device, pipeline_cache, &size, bytes_out.data()), "vkGetPipelineCacheData"))
		{
			bytes_out.clear();
			return false;
		}
		//按实际长度收敛
		bytes_out.resize(size);
		return true;
	}
}
