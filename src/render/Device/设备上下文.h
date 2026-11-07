#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
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
	持有一整套 Vulkan 设备侧对象：实例、调试信使、物理设备、逻辑设备与图形 / 呈现队列。
	生命周期：build_instance →（表面就绪后）build_device → 使用 → destroy；destroy 幂等，析构兜底。
	Phase 5 因呈现队列族查询需求而两段化：呈现队列族必须用 vkGetPhysicalDeviceSurfaceSupportKHR
	查询，而该调用要求表面已存在，故实例先行、设备待表面就绪后再建。
	创建顺序：实例 →（表面就绪后）物理设备 → 逻辑设备；任一步失败即由调用方逆序回退。
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
		构建实例（第一段）
		instance_extensions：实例所需扩展清单（自 Phase 4 起由平台模块提供表面扩展）。
		enable_validation：是否请求校验层。仅 Debug 有效；Release 由调用方强制传假。
		*/
		bool build_instance(const std::vector<const char*>& instance_extensions, bool enable_validation);
		/*
		构建设备（第二段）
		surface：已创建的窗口表面。用于查询呈现队列族、判定设备能否呈现到该表面。
		内部先选取满足「图形队列族 + 呈现队列族 + VK_KHR_swapchain」的物理设备，再创建逻辑设备。
		*/
		bool build_device(VkSurfaceKHR surface);
		//销毁（幂等；严格按创建逆序释放）
		void destroy(void);
		//是否已构建完成（实例与逻辑设备均已建立）
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
		//获取呈现队列族索引
		uint32_t present_family_get(void) const;
		//获取呈现队列
		VkQueue present_queue_get(void) const;
		//获取校验层是否已启用
		bool validation_get(void) const;
		//获取采样器各向异性特性是否已启用（设备创建时按支持情况开启）
		bool anisotropy_enabled_get(void) const;
		//获取采样器最大各向异性倍数（设备上限；未启用该特性时为 1）
		float max_anisotropy_get(void) const;
		//获取采样器最大 LOD 偏置绝对值（设备上限；第九十七轮）
		float max_sampler_lod_bias_get(void) const;
	private:
		//选取物理设备并确定图形族与呈现族（实现见 core/物理设备选择.cpp）
		bool build_physical_device(VkSurfaceKHR surface);
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
		//呈现队列族索引
		uint32_t present_family = 0;
		//呈现队列
		VkQueue present_queue = VK_NULL_HANDLE;
		//校验层是否已启用
		bool validation_enabled = false;
		//采样器各向异性特性是否已启用
		bool anisotropy_enabled = false;
		//采样器最大各向异性倍数（设备上限）
		float max_sampler_anisotropy = 1.0f;
		//采样器最大 LOD 偏置绝对值（设备上限；第九十七轮）
		float max_sampler_lod_bias = 0.0f;
	};
}
