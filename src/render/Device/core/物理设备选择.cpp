#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

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

	//选取物理设备并确定图形队列族
	bool Device_Context::build_physical_device(void)
	{
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

		//当前最优设备、其得分与图形队列族索引
		VkPhysicalDevice best_device = VK_NULL_HANDLE;
		uint64_t best_score = 0;
		uint32_t best_family = 0;
		//遍历所有物理设备
		for (const auto& candidate : devices)
		{
			//查询该设备的图形队列族
			uint32_t family = 0;
			//若该设备不具备图形队列族则跳过
			if (!graphics_family_query(candidate, family))
				continue;
			//计算该设备得分
			uint64_t score = device_score(candidate);
			//若该设备得分更高则更新候选
			if (score > best_score)
			{
				best_score = score;
				best_device = candidate;
				best_family = family;
			}
		}
		//若没有任何设备具备图形队列族
		if (best_device == VK_NULL_HANDLE)
		{
			logger.error("物理设备选择:: 所有物理设备均不具备图形队列族");
			return false;
		}

		//记录选中设备与图形队列族
		physical_device = best_device;
		graphics_family = best_family;

		//打印选中设备信息
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		logger.info("物理设备选择:: 选中设备 {}（类型 {}，得分 {}，图形队列族 {}）",
			properties.deviceName,
			static_cast<int32_t>(properties.deviceType),
			best_score,
			graphics_family);

		return true;
	}
}
