#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//创建逻辑设备与图形队列
	bool Device_Context::build_device(void)
	{
		/*
		队列族集合去重
		Phase 2 只有图形族；自 Phase 4 起图形与呈现可能落在同一族，届时在此登记
		呈现族并去重，保证同一族只声明一份（重复声明会触发校验层报错）。
		*/
		vector<uint32_t> unique_families;
		//登记图形队列族
		unique_families.push_back(graphics_family);

		//队列优先级（所有队列族共用同一份取值）
		float queue_priority = 1.0f;
		//队列创建信息（每族一份）
		vector<VkDeviceQueueCreateInfo> queue_infos(unique_families.size());
		for (std::size_t index = 0; index < unique_families.size(); ++index)
		{
			queue_infos[index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_infos[index].queueFamilyIndex = unique_families[index];
			queue_infos[index].queueCount = 1;
			queue_infos[index].pQueuePriorities = &queue_priority;
		}

		//启用的设备特性（Phase 2 全关，后续轮次按需开启）
		VkPhysicalDeviceFeatures features{};

		//逻辑设备创建信息
		VkDeviceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
		create_info.pQueueCreateInfos = queue_infos.data();
		create_info.pEnabledFeatures = &features;
		//Phase 2 不启用任何设备扩展
		create_info.enabledExtensionCount = 0;
		create_info.ppEnabledExtensionNames = nullptr;

		//创建逻辑设备
		if (!vk_result_check(vkCreateDevice(physical_device, &create_info, nullptr, &device), "vkCreateDevice"))
			return false;

		//获取图形队列（族内第 0 号队列）
		vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
		logger.info("逻辑设备创建:: 逻辑设备已建立（图形队列族 {}）", graphics_family);
		return true;
	}
}
