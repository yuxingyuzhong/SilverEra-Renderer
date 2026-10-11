#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
//交换链模块对外头（帧需取其图像、范围与图像可用信号量）
#include "Application/Renderer/src/render/Swapchain/交换链.h"

//游戏引擎命名空间
namespace engine
{
	/*
	帧槽
	每帧槽持有：命令池、主命令缓冲、完成栅栏、呈现完成信号量。
	数量 = 在途帧数 frame_overlap，与交换链图像数无关（§5.2 / §6.2）。
	*/
	struct Frame_Slot
	{
		//命令池（每帧经 vkResetCommandPool 重置）
		VkCommandPool command_pool = VK_NULL_HANDLE;
		//主命令缓冲（逐帧录制）
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		//完成栅栏（主机侧等待该槽前一次提交完成）
		VkFence in_flight_fence = VK_NULL_HANDLE;
		//呈现完成信号量（提交信号 / present 等待）
		VkSemaphore render_finished = VK_NULL_HANDLE;
	};

	/*
	延迟销毁条目
	记录一组「被交换链重建牵连」的旧对象，标记可在某帧号及以后释放（§6.4）。
	Phase 5 重建走 vkDeviceWaitIdle 立即销毁，本机制在场备用，尚未启用。
	*/
	struct Deferred_Entry
	{
		//待销毁的旧交换链（可为空）
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		//待销毁的旧图像视图
		std::vector<VkImageView> image_views;
		//待销毁的旧图像可用信号量
		std::vector<VkSemaphore> semaphores;
		//可在该帧号及以后释放
		uint64_t release_frame = 0;
	};

	/*
	帧上下文
	持有帧槽集合，负责取图像、命令录制、提交与呈现、延迟销毁（§5.2 / §6.2）。
	生命周期：build → render × N → destroy；destroy 幂等，析构兜底。
	同步原语归属（§5.2 表）：呈现完成信号量与帧栅栏属「每帧槽」，数量 = frame_overlap；
	图像可用信号量属「每交换链图像」，由交换链模块持有（数量 = 交换链图像数）。
	*/
	class Frame_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Frame_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Frame_Context(void);
		//不可拷贝、不可移动（持有 Vulkan 句柄与队列）
		Frame_Context(const Frame_Context&) = delete;
		Frame_Context& operator=(const Frame_Context&) = delete;

		/*
		构建
		创建 frame_overlap 个帧槽（命令池 / 命令缓冲 / 栅栏 / 呈现完成信号量）。
		swapchain：已构建的交换链（帧链路取图像依赖它）。
		*/
		bool build(VkDevice device, VkPhysicalDevice physical_device,
			uint32_t graphics_family, VkQueue graphics_queue,
			uint32_t present_family, VkQueue present_queue,
			Swapchain_Context& swapchain, uint32_t frame_overlap);
		//销毁（幂等；先等设备空闲，再逆序销毁帧槽）
		void destroy(void);
		//是否已构建完成（至少一个帧槽）
		bool valid(void) const;

		/*
		渲染一帧（取图像 → 录制 → 提交 → 呈现）
		swapchain：当前交换链；rebuild_needed_out：置真表示本帧遇到过期 / 次优，需上层重建；
		clear_color：清屏色（RGBA，线性 0~1）。返回假视为硬错误。
		*/
		bool render(Swapchain_Context& swapchain, bool& rebuild_needed_out, const float clear_color[4]);
		//已推进帧数
		uint32_t frame_count_get(void) const;
	private:
		//创建单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		bool frame_slot_create(Frame_Slot& slot);
		//销毁单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		void frame_slot_destroy(Frame_Slot& slot);
		//录制命令缓冲（实现见 core/命令录制.cpp）
		void command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
			VkExtent2D extent, const float clear_color[4]);
		//提交并呈现（实现见 core/提交呈现.cpp）
		bool submit_present(Frame_Slot& slot, uint32_t image_index, VkSemaphore image_available,
			Swapchain_Context& swapchain, bool& rebuild_needed_out);
		//压入延迟销毁条目（实现见 core/延迟销毁.cpp）
		void deferred_push(Deferred_Entry entry, uint32_t delay_frames);
		//释放到期的延迟销毁条目（实现见 core/延迟销毁.cpp）
		void deferred_flush(void);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//呈现队列族索引
		uint32_t present_family = 0;
		//呈现队列
		VkQueue present_queue = VK_NULL_HANDLE;
		//在途帧数
		uint32_t frame_overlap = 0;
		//帧槽集合（数量 = frame_overlap）
		std::vector<Frame_Slot> slots;
		//图像可用信号量轮转游标
		uint32_t acquire_cursor = 0;
		//已推进帧数
		uint32_t frame_count = 0;
		//延迟销毁队列
		std::vector<Deferred_Entry> deferred_entries;
		//当前帧序号（延迟销毁的释放判据）
		uint64_t frame_serial = 0;
	};
}
