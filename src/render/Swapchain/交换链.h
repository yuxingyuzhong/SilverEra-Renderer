#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
/*
VMA 头
深度图像随交换链重建一并创建 / 销毁，故需 VmaAllocation 类型。
*/
#include <vk_mem_alloc.h>

//游戏引擎命名空间
namespace engine
{
	//内存上下文（前置声明；仅作引用入参，其定义由实现文件包含）
	class Memory_Context;

	/*
	交换链旧资源
	重建时把旧交换链及其附属资源（图像视图 / 两类信号量 / 深度图像与内存与视图）整体移交，
	不再 vkDeviceWaitIdle 立即销毁，而由上层压入帧模块的延迟销毁队列，待在途帧用尽后释放（§6.4）。
	*/
	struct Swapchain_Old_Resources
	{
		//旧交换链（建新链时作为 oldSwapchain 传入，其后延时销毁）
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		//旧交换链图像（句柄由交换链持有，此处仅作索引记录）
		std::vector<VkImage> images;
		//旧图像视图
		std::vector<VkImageView> image_views;
		//旧图像可用信号量
		std::vector<VkSemaphore> image_available_semaphores;
		//旧呈现完成信号量
		std::vector<VkSemaphore> render_finished_semaphores;
		//旧深度图像
		std::vector<VkImage> depth_images;
		//旧深度图像内存
		std::vector<VmaAllocation> depth_allocations;
		//旧深度图像视图
		std::vector<VkImageView> depth_views;
		//旧 MSAA 颜色图像 / 内存 / 视图（第九十六轮；未启用时为空）
		std::vector<VkImage> msaa_color_images;
		std::vector<VmaAllocation> msaa_color_allocations;
		std::vector<VkImageView> msaa_color_views;
		//旧 MSAA 深度图像 / 内存 / 视图（第九十六轮；未启用时为空）
		std::vector<VkImage> msaa_depth_images;
		std::vector<VmaAllocation> msaa_depth_allocations;
		std::vector<VkImageView> msaa_depth_views;
	};

	/*
	交换链上下文
	持有 VkSwapchainKHR、其图像视图与「每交换链图像」的图像可用 / 呈现完成信号量与深度图像，
	并提供能力查询与重建。
	生命周期：build → 使用 → destroy / rebuild；destroy 幂等，析构兜底。
	同步原语归属（§5.2 表）：图像可用信号量与呈现完成信号量均属「每交换链图像」，
	数量 = 交换链图像数，随交换链重建而变化，故由本模块持有与重建
	（帧栅栏属「每帧槽」，由帧模块持有）。
	深度图像亦属「每交换链图像」，随交换链重建重建（U5）。
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
		memory：内存模块（深度图像创建 / 销毁依赖它）。
		graphics_family / present_family：用于选择图像共享方式（同族独占 / 异族并发）。
		width / height：窗口客户区尺寸（currentExtent 为 UINT32_MAX 时据此决定交换链范围）。
		vsync：为假且支持 MAILBOX 时用 MAILBOX，否则用 FIFO。
		*/
		bool build(VkPhysicalDevice physical_device, Memory_Context& memory, VkDevice device, VkSurfaceKHR surface,
			uint32_t graphics_family, uint32_t present_family,
			uint32_t width, uint32_t height, bool vsync);
		//销毁（幂等；先等设备空闲，再逆序销毁信号量 / 图像视图 / 交换链）
		void destroy(void);
		//是否已构建完成（交换链与图像齐备）
		bool valid(void) const;
		/*
		重建（按新尺寸重查能力并重建；0×0 直接返回假，K7）
		无整帧停顿：不再 vkDeviceWaitIdle，而是把旧交换链及其附属资源整体移交 old_out（不销毁），
		并以旧交换链作 oldSwapchain 建新链；old_out 由调用方压入帧模块的延迟销毁队列。
		重建失败时回滚（恢复旧资源，交换链仍可用）并返回假。
		*/
		bool rebuild(uint32_t width, uint32_t height, Swapchain_Old_Resources& old_out);

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
		//获取指定索引的呈现完成信号量（每交换链图像一个；提交信号 / present 等待用）
		VkSemaphore render_finished_semaphore_get(uint32_t index) const;
		//获取呈现完成信号量数量（= 交换链图像数）
		uint32_t render_finished_count_get(void) const;
		//获取指定索引的深度图像
		VkImage depth_image_get(uint32_t index) const;
		//获取指定索引的深度图像视图
		VkImageView depth_view_get(uint32_t index) const;
		//获取深度格式（已选定的设备支持格式）
		VkFormat depth_format_get(void) const;
		//交换链图像是否具备传输源用法（像素回读的前提）
		bool color_transfer_src_get(void) const;
		/*
		MSAA 采样数（第九十六轮）
		set 须在 build / rebuild 之前调用；1 = 关闭（不创建多重采样附件）。
		*/
		void sample_count_set(uint32_t count);
		uint32_t sample_count_get(void) const;
		//指定交换链索引的 MSAA 颜色图像 / 视图（关闭时为 0）
		VkImage msaa_color_image_get(uint32_t index) const;
		VkImageView msaa_color_view_get(uint32_t index) const;
		//指定交换链索引的 MSAA 深度图像视图（关闭时为 0）
		VkImageView msaa_depth_view_get(uint32_t index) const;
	private:
		/*
		创建交换链与图像视图、图像可用信号量（实现见 core/交换链重建.cpp）
		old_swapchain 非空时作为 VkSwapchainCreateInfoKHR::oldSwapchain 传入（重建复用旧链资源）。
		*/
		bool swapchain_create(uint32_t width, uint32_t height, VkSwapchainKHR old_swapchain);
		/*
		析出全部交换链侧资源到旧资源集合（不销毁）
		把成员中的交换链 / 图像视图 / 两类信号量 / 深度图像与内存与视图整体移入 old_out，
		并复位依赖这些资源的派生状态（格式 / 深度格式 / 范围 / 首次转换标志 / 传输源标志），
		使本对象处于「资源已抽出、待新建」的可重建态。
		*/
		void swapchain_resources_extract(Swapchain_Old_Resources& old_out);
		//把旧资源集合原样恢复回成员（重建失败回滚用）
		void swapchain_resources_restore(Swapchain_Old_Resources& old_in);
		//销毁交换链侧全部资源（信号量 / 图像视图 / 深度图像 / 交换链；实现见 core/交换链.cpp）
		void swapchain_resources_destroy(void);
		//选择深度格式（实现见 core/交换链重建.cpp）
		bool depth_format_select(void);
		//创建深度图像与视图并完成首帧前的一次布局转换（实现见 core/交换链重建.cpp）
		bool depth_resources_create(void);
		//销毁深度图像与视图（实现见 core/交换链重建.cpp）
		void depth_resources_destroy(void);
		//创建 MSAA 颜色 / 深度附件（第九十六轮；sample_count > 1 时调用；实现见 core/交换链重建.cpp）
		bool msaa_resources_create(void);
		//销毁 MSAA 颜色 / 深度附件（第九十六轮；实现见 core/交换链重建.cpp）
		void msaa_resources_destroy(void);
		//深度图像一次性布局转换（UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL；实现见 core/交换链重建.cpp）
		bool depth_transition(void);
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
		//内存模块（深度图像创建 / 销毁）
		Memory_Context* memory = nullptr;
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
		//深度格式（已选定的设备支持格式）
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//深度图像是否已完成首帧前的一次布局转换
		bool depth_initialized = false;
		//交换链图像是否具备传输源用法（像素回读前提；建链时按表面能力确定）
		bool color_transfer_src = false;
		//MSAA 采样数（第九十六轮；1 = 关闭）
		uint32_t sample_count = 1;
		//交换链范围
		VkExtent2D extent = { 0, 0 };
		//交换链图像
		std::vector<VkImage> images;
		//交换链图像视图
		std::vector<VkImageView> image_views;
		//图像可用信号量（每交换链图像一个）
		std::vector<VkSemaphore> image_available_semaphores;
		//呈现完成信号量（每交换链图像一个；提交信号 / present 等待用）
		std::vector<VkSemaphore> render_finished_semaphores;
		//深度图像（每交换链图像一张）
		std::vector<VkImage> depth_images;
		//深度图像内存
		std::vector<VmaAllocation> depth_allocations;
		//深度图像视图
		std::vector<VkImageView> depth_views;
		//MSAA 颜色图像 / 内存 / 视图（第九十六轮；每交换链图像一张；关闭时为空）
		std::vector<VkImage> msaa_color_images;
		std::vector<VmaAllocation> msaa_color_allocations;
		std::vector<VkImageView> msaa_color_views;
		//MSAA 深度图像 / 内存 / 视图（第九十六轮；每交换链图像一张；关闭时为空）
		std::vector<VkImage> msaa_depth_images;
		std::vector<VmaAllocation> msaa_depth_allocations;
		std::vector<VkImageView> msaa_depth_views;
	};
}
