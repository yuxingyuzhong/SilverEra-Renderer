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
	交换链上下文
	持有 VkSwapchainKHR、其图像视图与「每交换链图像」的图像可用信号量，并提供能力查询与重建。
	生命周期：build → 使用 → destroy / rebuild；destroy 幂等，析构兜底。
	同步原语归属（§5.2 表）：图像可用信号量属「每交换链图像」，数量 = 交换链图像数，
	随交换链重建而变化，故由本模块持有与重建（呈现完成信号量与帧栅栏属「每帧槽」，由帧模块持有）。
	*/
	class Swapchain_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Swapchain_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Swapchain_Context(void);
		//不可拷贝、不可移动（持有 Vulkan 句柄）
		Swapchain_Context(const Swapchain_Context&) = delete;
		Swapchain_Context& operator=(const Swapchain_Context&) = delete;

		/*
		构建
		graphics_family / present_family：用于选择图像共享方式（同族独占 / 异族并发）。
		width / height：窗口客户区尺寸（currentExtent 为 UINT32_MAX 时据此决定交换链范围）。
		vsync：为假且支持 MAILBOX 时用 MAILBOX，否则用 FIFO。
		*/
		bool build(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
			uint32_t graphics_family, uint32_t present_family,
			uint32_t width, uint32_t height, bool vsync);
		//销毁（幂等；先等设备空闲，再逆序销毁信号量 / 图像视图 / 交换链）
		void destroy(void);
		//是否已构建完成（交换链与图像齐备）
		bool valid(void) const;
		//重建（按新尺寸重查能力并重建；0×0 直接返回假，K7）
		bool rebuild(uint32_t width, uint32_t height);

		//获取交换链图像（按索引）
		VkImage image_get(uint32_t index) const;
		//获取交换链图像视图（按索引）
		VkImageView image_view_get(uint32_t index) const;
		//获取交换链图像数
		uint32_t image_count_get(void) const;
		//获取表面格式
		VkFormat format_get(void) const;
		//获取交换链范围
		VkExtent2D extent_get(void) const;
		//获取交换链句柄
		VkSwapchainKHR handle_get(void) const;
		//获取指定索引的图像可用信号量（每交换链图像一个）
		VkSemaphore image_available_semaphore_get(uint32_t index) const;
		//获取图像可用信号量数量（= 交换链图像数）
		uint32_t image_available_count_get(void) const;
	private:
		//创建交换链与图像视图、图像可用信号量（实现见 core/交换链重建.cpp）
		bool swapchain_create(uint32_t width, uint32_t height);
		//销毁交换链侧全部资源（信号量 / 图像视图 / 交换链；实现见 core/交换链.cpp）
		void swapchain_resources_destroy(void);
		//查询表面能力（实现见 core/交换链查询.cpp）
		bool capabilities_query(VkSurfaceCapabilitiesKHR& capabilities_out) const;
		//选择表面格式与色彩空间（实现见 core/交换链查询.cpp）
		bool format_select(VkSurfaceFormatKHR& format_out) const;
		//选择呈现模式（实现见 core/交换链查询.cpp）
		VkPresentModeKHR present_mode_select(bool vsync) const;
		//选择交换链范围（实现见 core/交换链查询.cpp）
		VkExtent2D extent_select(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) const;
	private:
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//表面
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//呈现队列族索引
		uint32_t present_family = 0;
		//垂直同步开关（重建沿用）
		bool vsync = true;
		//交换链句柄
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		//表面格式
		VkFormat format = VK_FORMAT_UNDEFINED;
		//交换链范围
		VkExtent2D extent = { 0, 0 };
		//交换链图像
		std::vector<VkImage> images;
		//交换链图像视图
		std::vector<VkImageView> image_views;
		//图像可用信号量（每交换链图像一个）
		std::vector<VkSemaphore> image_available_semaphores;
	};
}
