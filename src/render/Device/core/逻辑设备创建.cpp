#include "../局部命名空间使用.h"
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

		/*
		启用的传统设备特性（默认全关；采样器各向异性按支持情况启用）
		采样器各向异性属「可选特性」，必须在设备创建时显式启用后采样器才能请求 anisotropyEnable；
		不支持时保持关闭并告警（采样器侧由门面按 anisotropy_enabled_get 归一为关闭）。
		*/
		VkPhysicalDeviceFeatures features{};
		//若物理设备支持采样器各向异性
		if (supported_features.features.samplerAnisotropy == VK_TRUE)
		{
			features.samplerAnisotropy = VK_TRUE;
			anisotropy_enabled = true;
		}
		//不支持则告警（不硬失败）
		else
			logger.warn("逻辑设备创建:: 物理设备不支持采样器各向异性（samplerAnisotropy），已保持关闭");
		/*
		记录设备极限（第九十七轮：另记采样器最大 LOD 偏置）
		各向异性上限仅在启用该特性时有意义；LOD 偏置上限始终记录，供门面钳制采样器参数。
		*/
		{
			//设备属性（含极限值）
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(physical_device, &properties);
			//最大 LOD 偏置绝对值（负值防御为 0）
			max_sampler_lod_bias = properties.limits.maxSamplerLodBias;
			if (!(max_sampler_lod_bias > 0.0f))
				max_sampler_lod_bias = 0.0f;
			//启用各向异性时记录其上限（否则保持 1）
			if (anisotropy_enabled)
			{
				max_sampler_anisotropy = properties.limits.maxSamplerAnisotropy;
				//防御：上限异常（非 > 1）时按 1 处理
				if (!(max_sampler_anisotropy > 1.0f))
					max_sampler_anisotropy = 1.0f;
			}
		}

		//启用的 Vulkan 1.3 特性（动态渲染 + 同步 2，经 pNext 链入）
		VkPhysicalDeviceVulkan13Features enabled_13{};
		enabled_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		enabled_13.dynamicRendering = VK_TRUE;
		enabled_13.synchronization2 = VK_TRUE;
		/*
		着色器丢弃降级（第二百一十八轮）
		片元着色器的 discard（Alpha 裁剪等）在当前 glslc 下会声明 SPIR-V 能力
		DemoteToHelperInvocation；未启用该特性时校验层报 VUID-VkShaderModuleCreateInfo-pCode-08740。
		设备支持则启用以保持校验层 0 消息，不支持则告警（着色器仍可运行）。
		*/
		if (supported_13.shaderDemoteToHelperInvocation == VK_TRUE)
			enabled_13.shaderDemoteToHelperInvocation = VK_TRUE;
		else
			logger.warn("逻辑设备创建:: 物理设备不支持着色器丢弃降级（DemoteToHelperInvocation），含 discard 的片元着色器可能触发校验告警");

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
		logger.info("逻辑设备创建:: 逻辑设备已建立（图形队列族 {}，呈现队列族 {}，采样器各向异性 {}，上限 ×{}）",
			graphics_family, present_family, anisotropy_enabled ? "开" : "关", max_sampler_anisotropy);
		return true;
	}
}