#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（旧深度图像 / 视图经内存模块归还）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	/*
	压入延迟销毁条目
	记录一组旧资源并标记「release_frame 帧及以后」才可释放。
	旧资源来自交换链重建移交（旧交换链 ＋ 图像视图 ＋ 两类信号量 ＋ 深度图像与内存与视图）；
	重建已不再 vkDeviceWaitIdle，故这些可能仍被在途帧引用的对象必须延后到在途帧用尽后再销毁（§6.4）。
	*/
	void Frame_Context::deferred_push(Deferred_Entry entry, uint32_t delay_frames)
	{
		//标记释放帧号（当前帧计数 + 延迟帧数）
		entry.release_frame = frame_count + delay_frames;
		//入队
		deferred_entries.push_back(std::move(entry));
	}

	//释放单个延迟条目的资源（调用方须保证其已不再被在途帧引用）
	void Frame_Context::deferred_entry_release(Deferred_Entry& entry)
	{
		//旧深度图像 / 视图经内存模块归还（逐张成对）
		if (memory != nullptr)
		{
			for (std::size_t index = 0; index < entry.resources.depth_views.size(); ++index)
			{
				//归还该张深度图像视图
				memory->image_view_destroy(entry.resources.depth_views[index]);
				//归还该张深度图像与内存（索引越界时跳过，防御残缺条目）
				if (index < entry.resources.depth_images.size() && index < entry.resources.depth_allocations.size())
					memory->image_destroy(entry.resources.depth_images[index], entry.resources.depth_allocations[index]);
			}
		}
		//销毁旧图像视图
		for (VkImageView view : entry.resources.image_views)
			//若视图句柄有效
			if (view != VK_NULL_HANDLE)
				vkDestroyImageView(device, view, nullptr);
		//销毁旧图像可用信号量
		for (VkSemaphore semaphore : entry.resources.image_available_semaphores)
			//若信号量句柄有效
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		//销毁旧呈现完成信号量
		for (VkSemaphore semaphore : entry.resources.render_finished_semaphores)
			//若信号量句柄有效
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		//销毁旧交换链
		if (entry.resources.swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device, entry.resources.swapchain, nullptr);
	}

	//释放到期的延迟销毁条目
	void Frame_Context::deferred_flush(void)
	{
		//若无待释放条目
		if (deferred_entries.empty())
			return;
		//若逻辑设备不可用（无法销毁）
		if (device == VK_NULL_HANDLE)
			return;

		//逐项检查并释放到期条目
		for (auto it = deferred_entries.begin(); it != deferred_entries.end();)
		{
			//若尚未到释放帧号则跳过
			if (it->release_frame > frame_count)
			{
				++it;
				continue;
			}
			//释放该条目资源
			deferred_entry_release(*it);
			//移除该条目
			it = deferred_entries.erase(it);
		}
	}

	//立即释放全部延迟销毁条目（销毁期调用）
	void Frame_Context::deferred_release_all(void)
	{
		//设备可用时逐项释放（关闭前已等设备空闲，故无需再按帧号判据）
		if (device != VK_NULL_HANDLE)
		{
			for (Deferred_Entry& entry : deferred_entries)
				deferred_entry_release(entry);
		}
		//清空队列
		deferred_entries.clear();
	}
}
