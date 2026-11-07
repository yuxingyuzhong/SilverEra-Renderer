#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//获取日志系统
#include "Engine/EngineFoundation/src/tools/Logging/日志系统运行包.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>

namespace engine
{
	//VkResult 可读名称（未收录的结果码回退为「VkResult(数值)」）
	inline std::string vk_result_name_get(VkResult result)
	{
		switch (result)
		{
		case VK_SUCCESS:                            return "VK_SUCCESS";
		case VK_NOT_READY:                          return "VK_NOT_READY";
		case VK_TIMEOUT:                            return "VK_TIMEOUT";
		case VK_EVENT_SET:                          return "VK_EVENT_SET";
		case VK_EVENT_RESET:                        return "VK_EVENT_RESET";
		case VK_INCOMPLETE:                         return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:           return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:         return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:        return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:                  return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:            return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:            return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:        return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:          return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:          return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:             return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:         return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_UNKNOWN:                      return "VK_ERROR_UNKNOWN";
		case VK_ERROR_SURFACE_LOST_KHR:             return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:     return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_SUBOPTIMAL_KHR:                     return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR:              return "VK_ERROR_OUT_OF_DATE_KHR";
		default:                                    return std::format("VkResult({})", static_cast<int32_t>(result));
		}
	}

	//是否为「可接受」的返回值（不算失败，但调用方必须自行分支处理，不能当成功用）
	inline bool vk_result_acceptable(VkResult result)
	{
		return result == VK_ERROR_OUT_OF_DATE_KHR
			|| result == VK_SUBOPTIMAL_KHR
			|| result == VK_TIMEOUT;
	}

	/*
	硬错误检查
	本层所有 Vulkan 调用的唯一检查出口（避免「有的地方查、有的地方不查」）。
	返回真仅代表调用成功；非成功码按「可接受 / 硬错误」分流记日志：
	  可接受 → 只记 debug（调用方自己会分支处理，比如交换链重建）
	  硬错误 → 记 error，并带上调用点描述与结果码名称
	*/
	inline bool vk_result_check(VkResult result, const char* operation)
	{
		//若调用成功
		if (result == VK_SUCCESS)
			return true;
		//若为可接受返回值
		if (vk_result_acceptable(result))
		{
			logger.debug("Vulkan 调用返回可接受结果码：{} → {}", operation, vk_result_name_get(result));
			return false;
		}
		//硬错误
		logger.error("Vulkan 调用失败：{} → {}", operation, vk_result_name_get(result));
		return false;
	}
}
