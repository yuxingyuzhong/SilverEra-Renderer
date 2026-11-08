#include "../局部命名空间使用.h"
#include "校验层配置.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构
	Device_Context::~Device_Context(void)
	{
		//释放已创建的 Vulkan 对象
		destroy();
	}

	//销毁
	void Device_Context::destroy(void)
	{
		//若逻辑设备已创建
		if (device != VK_NULL_HANDLE)
		{
			logger.debug("设备上下文:: 销毁逻辑设备");
			vkDestroyDevice(device, nullptr);
			//清除逻辑设备及其队列记录
			device = VK_NULL_HANDLE;
			graphics_queue = VK_NULL_HANDLE;
			graphics_family = 0;
			present_queue = VK_NULL_HANDLE;
			present_family = 0;
		}
		//若调试信使已创建
		if (debug_messenger != VK_NULL_HANDLE)
		{
			logger.debug("设备上下文:: 销毁调试信使");
			debug_messenger_destroy(instance, debug_messenger);
			debug_messenger = VK_NULL_HANDLE;
		}
		//若实例已创建
		if (instance != VK_NULL_HANDLE)
		{
			logger.debug("设备上下文:: 销毁实例");
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}

		//清除物理设备记录
		physical_device = VK_NULL_HANDLE;
		//清除校验层记录
		validation_enabled = false;
		//清除采样器各向异性特性记录（R-DEV-04：destroy 须复位全部派生标志）
		anisotropy_enabled = false;
		//复位采样器最大各向异性倍数
		max_sampler_anisotropy = 1.0f;
	}

	//是否已构建完成
	bool Device_Context::valid(void) const
	{
		return instance != VK_NULL_HANDLE
			&& device != VK_NULL_HANDLE;
	}

	//获取实例句柄
	VkInstance Device_Context::instance_get(void) const
	{
		return instance;
	}

	//获取物理设备句柄
	VkPhysicalDevice Device_Context::physical_device_get(void) const
	{
		return physical_device;
	}

	//获取逻辑设备句柄
	VkDevice Device_Context::device_get(void) const
	{
		return device;
	}

	//获取图形队列族索引
	uint32_t Device_Context::graphics_family_get(void) const
	{
		return graphics_family;
	}

	//获取图形队列
	VkQueue Device_Context::graphics_queue_get(void) const
	{
		return graphics_queue;
	}

	//获取呈现队列族索引
	uint32_t Device_Context::present_family_get(void) const
	{
		return present_family;
	}

	//获取呈现队列
	VkQueue Device_Context::present_queue_get(void) const
	{
		return present_queue;
	}

	//获取校验层是否已启用
	bool Device_Context::validation_get(void) const
	{
		return validation_enabled;
	}

	//获取采样器各向异性特性是否已启用
	bool Device_Context::anisotropy_enabled_get(void) const
	{
		return anisotropy_enabled;
	}

	//获取采样器最大各向异性倍数（设备上限）
	float Device_Context::max_anisotropy_get(void) const
	{
		return max_sampler_anisotropy;
	}

	//获取采样器最大 LOD 偏置绝对值（设备上限）
	float Device_Context::max_sampler_lod_bias_get(void) const
	{
		return max_sampler_lod_bias;
	}
}