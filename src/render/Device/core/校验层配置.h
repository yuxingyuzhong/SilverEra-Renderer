#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>

//游戏引擎命名空间
namespace engine
{
	//获取校验层名称
	const char* validation_layer_name_get(void);

	//校验层是否可用（枚举实例层清单判定）
	bool validation_layer_available(void);

	/*
	组装实例扩展清单
	required：调用方要求的扩展（如平台表面扩展），原样保序置于结果头部；
	enable_validation：为真且校验层可用时追加调试工具扩展 VK_EXT_debug_utils。
	*/
	std::vector<const char*> instance_extensions_assemble(const std::vector<const char*>& required, bool enable_validation);

	//构造调试信使创建信息（可挂到 VkInstanceCreateInfo::pNext，随实例创建一并提交）
	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info_create(void);

	//创建调试信使（扩展入口不可用则记警告并返回假，不视为硬错误）
	bool debug_messenger_create(VkInstance instance, VkDebugUtilsMessengerEXT* messenger_out);

	//销毁调试信使（入口不可用时静默跳过）
	void debug_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT messenger);

	/*
	调试消息回调
	校验层消息按严重级别转投引擎日志（错误 / 警告 / 其余），返回值恒为假
	—— 校验层规范约定返回真会中止触发该消息的调用，本层一律不中止。
	*/
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_types,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data);
}
