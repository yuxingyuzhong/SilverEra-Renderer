#include "src/render/Device/局部命名空间使用.h"
#include "校验层配置.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//获取校验层名称
	const char* validation_layer_name_get(void)
	{
		return "VK_LAYER_KHRONOS_validation";
	}

	//校验层是否可用
	bool validation_layer_available(void)
	{
		//查询可用实例层数量
		uint32_t layer_count = 0;
		//若查询失败
		if (!vk_result_check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr), "vkEnumerateInstanceLayerProperties"))
			return false;
		//若运行时未安装任何实例层
		if (layer_count == 0)
			return false;

		//收集可用实例层清单
		vector<VkLayerProperties> layers(layer_count);
		//若清单获取失败
		if (!vk_result_check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()), "vkEnumerateInstanceLayerProperties"))
			return false;

		//逐项比对校验层名称
		for (const auto& layer : layers)
			//若名称命中则校验层可用
			if (string(layer.layerName) == validation_layer_name_get())
				return true;

		//未命中任何实例层
		return false;
	}

	//组装实例扩展清单
	vector<const char*> instance_extensions_assemble(const vector<const char*>& required, bool enable_validation)
	{
		//结果清单（先容纳调用方要求的扩展）
		vector<const char*> extensions = required;
		//若需启用校验层
		if (enable_validation)
			//追加调试工具扩展（调试信使依赖它）
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		return extensions;
	}

	//调试消息回调
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_types,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data)
	{
		//消息正文（校验层保证非空，此处兜底）
		const char* text = (callback_data != nullptr && callback_data->pMessage != nullptr)
			? callback_data->pMessage
			: "(无消息正文)";

		//按严重级别分派到引擎日志
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			logger.error("[Vulkan 校验] {}", text);
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			logger.warn("[Vulkan 校验] {}", text);
		else
			logger.info("[Vulkan 校验] {}", text);

		//返回值恒为假（返回真会中止触发该消息的调用）
		return VK_FALSE;
	}

	//构造调试信使创建信息
	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info_create(void)
	{
		//调试信使创建信息
		VkDebugUtilsMessengerCreateInfoEXT info{};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		/*
		订阅的消息严重级别
		仅订阅警告与错误：信息级（VERBOSE / INFO）会产生大量噪音，
		淹没引擎自身日志，故不订阅。
		*/
		info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		//订阅的消息类型（常规 / 校验 / 性能 全开）
		info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		//消息回调
		info.pfnUserCallback = debug_message_callback;
		//用户数据（暂未使用）
		info.pUserData = nullptr;
		return info;
	}

	//创建调试信使
	bool debug_messenger_create(VkInstance instance, VkDebugUtilsMessengerEXT* messenger_out)
	{
		//先置空输出，避免失败时残留旧值
		*messenger_out = VK_NULL_HANDLE;
		/*
		取调试信使创建入口
		vkCreateDebugUtilsMessengerEXT 属扩展函数，实例级入口须经
		vkGetInstanceProcAddr 动态获取；取不到说明运行时不支持该扩展
		（K19：降级告警，不视为硬错误，实例本身仍然可用）。
		*/
		auto create_func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
		//若创建入口不可用
		if (create_func == nullptr)
		{
			logger.warn("校验层配置:: vkCreateDebugUtilsMessengerEXT 入口不可用，Vulkan 校验消息将无法转投日志");
			return false;
		}

		//调试信使创建信息
		VkDebugUtilsMessengerCreateInfoEXT info = debug_messenger_info_create();
		//创建调试信使
		VkResult result = create_func(instance, &info, nullptr, messenger_out);
		//若创建失败
		if (!vk_result_check(result, "vkCreateDebugUtilsMessengerEXT"))
		{
			*messenger_out = VK_NULL_HANDLE;
			return false;
		}

		logger.info("校验层配置:: 调试信使已建立");
		return true;
	}

	//销毁调试信使
	void debug_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
	{
		/*
		取调试信使销毁入口
		与创建同源，须经 vkGetInstanceProcAddr 动态获取；取不到则静默跳过
		（此时信使本就不可能成功建立，销毁无意义）。
		*/
		auto destroy_func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
		//若销毁入口不可用
		if (destroy_func == nullptr)
			return;
		destroy_func(instance, messenger, nullptr);
	}
}
