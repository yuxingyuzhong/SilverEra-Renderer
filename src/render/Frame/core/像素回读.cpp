#include "src/render/Frame/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（回读缓冲创建 / 销毁 / 主机读回）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	/*
	确保回读缓冲就绪
	已存在且容量足够时直接复用；否则先归还旧缓冲再按需新建。
	主机可见（随机读写，非顺序写优化），用途 TRANSFER_DST（作为拷贝目标）。
	*/
	bool Frame_Context::readback_buffer_ensure(VkDeviceSize size)
	{
		//前置：内存模块必须可用
		if (memory == nullptr)
		{
			logger.error("像素回读:: 内存上下文不可用，无法准备回读缓冲");
			return false;
		}
		//若已有缓冲且容量足够
		if (readback_buffer != VK_NULL_HANDLE && readback_size >= size)
			return true;

		//归还旧缓冲（尺寸不足时重建；尚未创建时为空操作）
		readback_buffer_destroy();

		//创建回读缓冲（主机可见的传输目标）
		if (!memory->buffer_create(readback_buffer, readback_alloc, size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false))
		{
			logger.error("像素回读:: 回读缓冲创建失败（{} 字节）", size);
			readback_buffer = VK_NULL_HANDLE;
			readback_alloc = VK_NULL_HANDLE;
			readback_size = 0;
			return false;
		}
		//记录当前容量
		readback_size = size;
		return true;
	}

	//销毁回读缓冲
	void Frame_Context::readback_buffer_destroy(void)
	{
		//内存模块可用且句柄有效时归还缓冲
		if (memory != nullptr && readback_buffer != VK_NULL_HANDLE)
			memory->buffer_destroy(readback_buffer, readback_alloc);
		//复位句柄与容量
		readback_buffer = VK_NULL_HANDLE;
		readback_alloc = VK_NULL_HANDLE;
		readback_size = 0;
	}

	/*
	帧后读出像素
	拷贝与渲染录在同一命令缓冲内，故本帧槽栅栏发出即表示像素已写入回读缓冲；
	等栅栏后再经内存模块读回（非主机一致的内存由内存模块负责使缓存失效）。
	*/
	bool Frame_Context::readback_read(Frame_Slot& slot, Frame_Readback_Request& request, VkExtent2D extent)
	{
		//前置：请求与回读缓冲必须齐备
		if (request.pixels == nullptr || readback_buffer == VK_NULL_HANDLE)
		{
			logger.error("像素回读:: 回读目标或回读缓冲不完整");
			return false;
		}

		//本帧像素字节数（RGBA8 紧排）
		VkDeviceSize byte_count = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
		//若目标缓冲容量不足
		if (request.capacity < byte_count)
		{
			logger.error("像素回读:: 回读目标容量不足（需 {} 字节，有 {} 字节）", byte_count, request.capacity);
			return false;
		}

		//等待本帧槽完成（渲染与拷贝同批提交，栅栏发出即数据就绪）
		if (!vk_result_check(vkWaitForFences(device, 1, &slot.in_flight_fence, VK_TRUE, UINT64_MAX),
			"像素回读:: 等待帧完成"))
			return false;

		//把像素读回调用方缓冲
		if (!memory->buffer_read(readback_buffer, readback_alloc, request.pixels, byte_count, 0))
		{
			logger.error("像素回读:: 回读缓冲读取失败");
			return false;
		}

		//回填尺寸与字节数
		request.width = extent.width;
		request.height = extent.height;
		request.bytes = byte_count;
		return true;
	}
}
