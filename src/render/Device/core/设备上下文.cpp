#include "../局部命名空间使用.h"
#include "校验层配置.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构
	Device_Context::~Device_Context(void)
	{
		//释放已创建的 Vulkan 对象
		destroy();
	}

	//构建
	bool Device_Context::build(const vector<const char*>& instance_extensions, bool enable_validation)
	{
		//若实例创建失败
		if (!build_instance(instance_extensions, enable_validation))
		{
			logger.error("设备上下文:: 实例创建失败，构建中止");
			//回退已创建对象
			destroy();
			return false;
		}
		//若物理设备选取失败
		if (!build_physical_device())
		{
			logger.error("设备上下文:: 物理设备选取失败，构建中止");
			//回退已创建对象
			destroy();
			return false;
		}
		//若逻辑设备创建失败
		if (!build_device())
		{
			logger.error("设备上下文:: 逻辑设备创建失败，构建中止");
			//回退已创建对象
			destroy();
			return false;
		}

		//记录构建完成
		logger.info("设备上下文:: 设备基础设施构建完成");
		return true;
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
	}

	//是否已构建完成
	bool Device_Context::valid(void) const
	{
		return instance != VK_NULL_HANDLE
			&& physical_device != VK_NULL_HANDLE
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

	//获取校验层是否已启用
	bool Device_Context::validation_get(void) const
	{
		return validation_enabled;
	}
}
