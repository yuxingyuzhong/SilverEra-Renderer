#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>

//游戏引擎命名空间
namespace engine
{
	/*
	设备上下文
	持有一整套 Vulkan 设备侧对象：实例、调试信使、物理设备、逻辑设备与图形队列。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	创建顺序：实例 → 物理设备 → 逻辑设备；任一步失败即逆序回退，不留下半初始化状态。
	*/
	class Device_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Device_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Device_Context(void);
		//不可拷贝、不可移动（持有 Vulkan 句柄与队列）
		Device_Context(const Device_Context&) = delete;
		Device_Context& operator=(const Device_Context&) = delete;

		/*
		构建
		instance_extensions：实例所需扩展清单。Phase 2 尚无平台模块，由调用方按需给出；
		                     自 Phase 4 起由平台模块提供表面相关扩展（如 VK_KHR_surface）。
		enable_validation：是否请求校验层。仅 Debug 有效；Release 由调用方强制传假。
		*/
		bool build(const std::vector<const char*>& instance_extensions, bool enable_validation);
		//销毁（幂等；严格按创建逆序释放）
		void destroy(void);
		//是否已构建完成（实例、物理设备、逻辑设备三者齐备）
		bool valid(void) const;

		//获取实例句柄
		VkInstance instance_get(void) const;
		//获取物理设备句柄
		VkPhysicalDevice physical_device_get(void) const;
		//获取逻辑设备句柄
		VkDevice device_get(void) const;
		//获取图形队列族索引
		uint32_t graphics_family_get(void) const;
		//获取图形队列
		VkQueue graphics_queue_get(void) const;
		//获取校验层是否已启用
		bool validation_get(void) const;
	private:
		//创建实例（含校验层与调试信使；实现见 core/实例创建.cpp）
		bool build_instance(const std::vector<const char*>& instance_extensions, bool enable_validation);
		//选取物理设备并确定图形队列族（实现见 core/物理设备选择.cpp）
		bool build_physical_device(void);
		//创建逻辑设备与图形队列（实现见 core/逻辑设备创建.cpp）
		bool build_device(void);
	private:
		//实例
		VkInstance instance = VK_NULL_HANDLE;
		//调试信使（未启用校验层时为 VK_NULL_HANDLE）
		VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//校验层是否已启用
		bool validation_enabled = false;
	};
}
