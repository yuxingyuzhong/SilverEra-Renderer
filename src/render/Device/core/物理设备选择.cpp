#include "src/render/Device/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//查询物理设备的图形队列族索引
	static bool graphics_family_query(VkPhysicalDevice physical_device, uint32_t& family_out)
	{
		//查询队列族数量
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
		//若不存在任何队列族
		if (family_count == 0)
			return false;

		//收集队列族属性
		vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

		//查找首个具备图形能力的队列族
		for (uint32_t index = 0; index < family_count; ++index)
			//若该队列族支持图形操作
			if (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				family_out = index;
				return true;
			}

		//未找到图形队列族
		return false;
	}

	/*
	查询物理设备的呈现队列族索引
	逐个队列族调用 vkGetPhysicalDeviceSurfaceSupportKHR，命中首个支持呈现到该表面的族即返回。
	允许与图形族相同（同一族既可图形又可呈现，去重归逻辑设备创建处理）。
	*/
	static bool present_family_query(VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t& family_out)
	{
		//查询队列族数量
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
		//若不存在任何队列族
		if (family_count == 0)
			return false;

		//逐个队列族查询对本表面的呈现支持
		for (uint32_t index = 0; index < family_count; ++index)
		{
			//该族是否支持呈现到表面
			VkBool32 supported = VK_FALSE;
			//若查询失败则跳过该族
			if (!vk_result_check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR"))
				continue;
			//若该族支持呈现
			if (supported == VK_TRUE)
			{
				family_out = index;
				return true;
			}
		}

		//未找到呈现队列族
		return false;
	}

	//查询物理设备是否支持指定设备扩展（枚举设备扩展清单判定）
	static bool device_extension_supported(VkPhysicalDevice physical_device, const char* extension_name)
	{
		//查询设备扩展数量
		uint32_t extension_count = 0;
		//若查询失败
		if (!vk_result_check(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr), "vkEnumerateDeviceExtensionProperties"))
			return false;
		//若不存在任何设备扩展
		if (extension_count == 0)
			return false;

		//收集设备扩展清单
		vector<VkExtensionProperties> extensions(extension_count);
		//若清单获取失败
		if (!vk_result_check(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()), "vkEnumerateDeviceExtensionProperties"))
			return false;

		//逐项比对扩展名
		for (const auto& extension : extensions)
			//若名称命中则扩展受支持
			if (string(extension.extensionName) == extension_name)
				return true;

		//未命中任何扩展
		return false;
	}

	/*
	物理设备打分
	类型权重占绝对主导，显存容量加成封顶 9999（小于最小类型权重差 10000），
	从而避免「大显存的核显 / CPU 设备反超独显」。
	*/
	static uint64_t device_score(VkPhysicalDevice physical_device)
	{
		//设备属性
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		//设备类型权重
		uint64_t type_weight = 500;
		switch (properties.deviceType)
		{
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   type_weight = 30000; break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: type_weight = 20000; break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    type_weight = 10000; break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:            type_weight = 1000;  break;
		default:                                     type_weight = 500;   break;
		}

		//设备内存信息
		VkPhysicalDeviceMemoryProperties memory_properties{};
		vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
		//本地显存总字节数
		uint64_t local_bytes = 0;
		//累加设备本地内存堆容量
		for (uint32_t index = 0; index < memory_properties.memoryHeapCount; ++index)
			//若该堆为设备本地内存
			if (memory_properties.memoryHeaps[index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				local_bytes += memory_properties.memoryHeaps[index].size;

		//显存容量加成（换算为 MB 并封顶 9999）
		uint64_t memory_bonus = local_bytes / (1024ull * 1024ull);
		if (memory_bonus > 9999)
			memory_bonus = 9999;

		return type_weight + memory_bonus;
	}

	//选取物理设备并确定图形族与呈现族
	bool Device_Context::build_physical_device(VkSurfaceKHR surface)
	{
		//前置：表面必须已创建（呈现队列族查询依赖它）
		if (surface == VK_NULL_HANDLE)
		{
			logger.error("物理设备选择:: 表面句柄为空，无法查询呈现队列族");
			return false;
		}

		//查询可用物理设备数量
		uint32_t device_count = 0;
		if (!vk_result_check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "vkEnumeratePhysicalDevices"))
			return false;
		//若不存在任何物理设备
		if (device_count == 0)
		{
			logger.error("物理设备选择:: 未发现任何支持 Vulkan 的物理设备");
			return false;
		}

		//收集物理设备清单
		vector<VkPhysicalDevice> devices(device_count);
		if (!vk_result_check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()), "vkEnumeratePhysicalDevices"))
			return false;

		//当前最优设备、其得分与图形 / 呈现队列族索引
		VkPhysicalDevice best_device = VK_NULL_HANDLE;
		uint64_t best_score = 0;
		uint32_t best_graphics_family = 0;
		uint32_t best_present_family = 0;
		//遍历所有物理设备
		for (const auto& candidate : devices)
		{
			//查询该设备的图形队列族
			uint32_t graphics = 0;
			//若该设备不具备图形队列族则跳过
			if (!graphics_family_query(candidate, graphics))
				continue;
			//查询该设备对本表面的呈现队列族
			uint32_t present = 0;
			//若该设备不具备呈现队列族则跳过
			if (!present_family_query(candidate, surface, present))
				continue;
			//若该设备不支持交换链扩展则跳过
			if (!device_extension_supported(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				continue;
			//计算该设备得分
			uint64_t score = device_score(candidate);
			//若该设备得分更高则更新候选
			if (score > best_score)
			{
				best_score = score;
				best_device = candidate;
				best_graphics_family = graphics;
				best_present_family = present;
			}
		}
		//若没有设备同时满足图形族、呈现族与交换链扩展
		if (best_device == VK_NULL_HANDLE)
		{
			logger.error("物理设备选择:: 所有物理设备均不满足「图形队列族 + 呈现队列族 + VK_KHR_swapchain」");
			return false;
		}

		//记录选中设备与两个队列族
		physical_device = best_device;
		graphics_family = best_graphics_family;
		present_family = best_present_family;

		//打印选中设备信息
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		logger.info("物理设备选择:: 选中设备 {}（类型 {}，得分 {}，图形队列族 {}，呈现队列族 {}）",
			properties.deviceName,
			static_cast<int32_t>(properties.deviceType),
			best_score,
			graphics_family,
			present_family);

		return true;
	}
}
