#include "../局部命名空间使用.h"
#include "校验层配置.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//创建实例
	bool Device_Context::build_instance(const vector<const char*>& instance_extensions, bool enable_validation)
	{
		/*
		校验层开关折算
		只有「请求开启」且「运行时确有该层」才真正启用：请求不存在的层会让
		vkCreateInstance 直接失败，故此处先探测再决定，探测不中则降级告警（K19）。
		*/
		bool use_validation = enable_validation && validation_layer_available();
		//若请求开启校验层但运行时未安装
		if (enable_validation && !use_validation)
			logger.warn("实例创建:: 请求开启 Vulkan 校验层，但运行时未安装 VK_LAYER_KHRONOS_validation，已降级关闭");
		//记录校验层启用状态
		validation_enabled = use_validation;

		/*
		目标 Vulkan 版本
		Phase 5 起启用 1.3 的动态渲染（dynamicRendering）特性，实例与设备统一按 1.3 请求；
		运行时不支持 1.3 时 vkCreateInstance 会失败并给出明确日志，不做静默降级（设计 §11）。
		*/
		uint32_t api_version = VK_API_VERSION_1_3;

		//应用信息
		VkApplicationInfo app_info{};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "白银纪元";
		app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		app_info.pEngineName = "SilverEra Renderer";
		app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		app_info.apiVersion = api_version;

		//实例扩展清单（调用方要求的扩展 ＋ 校验层调试扩展）
		vector<const char*> extensions = instance_extensions_assemble(instance_extensions, use_validation);

		/*
		校验层名称
		必须声明在函数作用域：create_info.ppEnabledLayerNames 要指向它，
		直接指向临时量或千万不要写悬垂指针。
		*/
		const char* layer_name = validation_layer_name_get();

		/*
		实例创建信息
		校验层开启时把调试信使信息挂到 pNext：这样「实例创建/销毁本身」产生的
		校验消息也能被捕获（此刻信使尚未建立）。
		*/
		VkInstanceCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create_info.pApplicationInfo = &app_info;
		create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		create_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

		//调试信使创建信息（仅在启用校验层时挂接）
		VkDebugUtilsMessengerCreateInfoEXT debug_info{};
		//若启用校验层
		if (use_validation)
		{
			debug_info = debug_messenger_info_create();
			create_info.pNext = &debug_info;
			create_info.enabledLayerCount = 1;
			create_info.ppEnabledLayerNames = &layer_name;
		}
		//若未启用校验层
		else
		{
			create_info.enabledLayerCount = 0;
			create_info.ppEnabledLayerNames = nullptr;
		}

		//创建实例
		if (!vk_result_check(vkCreateInstance(&create_info, nullptr, &instance), "vkCreateInstance"))
			return false;
		logger.info("实例创建:: 实例已建立（Vulkan {}.{}.{}）",
			VK_API_VERSION_MAJOR(api_version),
			VK_API_VERSION_MINOR(api_version),
			VK_API_VERSION_PATCH(api_version));

		//若校验层已启用
		if (use_validation)
			//补建调试信使（失败仅告警，不回退实例 —— K19）
			debug_messenger_create(instance, &debug_messenger);

		return true;
	}
}