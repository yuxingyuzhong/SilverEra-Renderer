#include "src/render/Device/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//创建设备：选取物理设备后建立逻辑设备与图形 / 呈现队列
	bool Device_Context::build_device(VkSurfaceKHR surface)
	{
		//先选取物理设备并确定图形族与呈现族
		if (!build_physical_device(surface))
		{
			logger.error("逻辑设备创建:: 物理设备选取失败");
			return false;
		}

		/*
		队列族集合去重
		Phase 5 图形族与呈现族可能落在同一族，此时只声明一份（重复声明会触发校验层报错）。
		*/
		vector<uint32_t> unique_families;
		//登记图形队列族
		unique_families.push_back(graphics_family);
		//若呈现族与图形族不同则追加
		if (present_family != graphics_family)
			unique_families.push_back(present_family);

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

		/*
	查询 Vulkan 1.3 特性支持
	本层统一走动态渲染（vkCmdBeginRendering）与同步 2（vkQueueSubmit2），
	故需 dynamicRendering 与 synchronization2 两个特性；
	用同一结构体先查询后启用，不支持则明确报错，不做传统 RenderPass / vkQueueSubmit 回退。
	*/
	VkPhysicalDeviceVulkan13Features supported_13{};
	supported_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceFeatures2 supported_features{};
	supported_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	supported_features.pNext = &supported_13;
	vkGetPhysicalDeviceFeatures2(physical_device, &supported_features);
	//若物理设备不支持动态渲染
	if (supported_13.dynamicRendering != VK_TRUE)
	{
		logger.error("逻辑设备创建:: 物理设备不支持 Vulkan 1.3 动态渲染（dynamicRendering），本层无法继续");
		return false;
	}
	//若物理设备不支持同步 2
	if (supported_13.synchronization2 != VK_TRUE)
	{
		logger.error("逻辑设备创建:: 物理设备不支持 Vulkan 1.3 同步 2（synchronization2），本层无法继续");
		return false;
	}

	//启用的传统设备特性（全关）
	VkPhysicalDeviceFeatures features{};
	//启用的 Vulkan 1.3 特性（动态渲染 + 同步 2，经 pNext 链入）
	VkPhysicalDeviceVulkan13Features enabled_13{};
	enabled_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	enabled_13.dynamicRendering = VK_TRUE;
	enabled_13.synchronization2 = VK_TRUE;

		//启用的设备扩展（呈现必需 VK_KHR_swapchain）
		const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		//逻辑设备创建信息
		VkDeviceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.pNext = &enabled_13;
		create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
		create_info.pQueueCreateInfos = queue_infos.data();
		create_info.pEnabledFeatures = &features;
		create_info.enabledExtensionCount = 1;
		create_info.ppEnabledExtensionNames = device_extensions;

		//创建逻辑设备
		if (!vk_result_check(vkCreateDevice(physical_device, &create_info, nullptr, &device), "vkCreateDevice"))
			return false;

		//获取图形队列（族内第 0 号队列）
		vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
		//获取呈现队列（族内第 0 号队列）
		vkGetDeviceQueue(device, present_family, 0, &present_queue);
		logger.info("逻辑设备创建:: 逻辑设备已建立（图形队列族 {}，呈现队列族 {}）", graphics_family, present_family);
		return true;
	}
}
