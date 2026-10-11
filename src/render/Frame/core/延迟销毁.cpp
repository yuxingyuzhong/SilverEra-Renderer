#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	压入延迟销毁条目
	记录一组旧对象并标记「release_frame 帧及以后」才可释放。
	Phase 5 交换链重建走 vkDeviceWaitIdle 立即销毁，本入口保留机制，供后续无停顿重建扩用（§6.4）。
	*/
	void Frame_Context::deferred_push(Deferred_Entry entry, uint32_t delay_frames)
	{
		//标记释放帧号（当前帧序号 + 延迟帧数）
		entry.release_frame = frame_serial + delay_frames;
		//入队
		deferred_entries.push_back(std::move(entry));
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
			if (it->release_frame > frame_serial)
			{
				++it;
				continue;
			}
			//销毁旧图像视图
			for (VkImageView view : it->image_views)
				//若视图句柄有效
				if (view != VK_NULL_HANDLE)
					vkDestroyImageView(device, view, nullptr);
			//销毁旧图像可用信号量
			for (VkSemaphore semaphore : it->semaphores)
				//若信号量句柄有效
				if (semaphore != VK_NULL_HANDLE)
					vkDestroySemaphore(device, semaphore, nullptr);
			//销毁旧交换链
			if (it->swapchain != VK_NULL_HANDLE)
				vkDestroySwapchainKHR(device, it->swapchain, nullptr);
			//移除该条目
			it = deferred_entries.erase(it);
		}
	}
}
