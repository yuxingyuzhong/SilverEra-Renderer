#include "../局部命名空间使用.h"
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	创建缓冲
	host_visible 为真：主机可见（VMA 允许映射），并保持持久映射，
	                   因此写入 / 读回不必再自行挑选内存类型；
	                   其中 sequential_write 为真的（上传暂存）按「仅顺序写入」优化，
	                   为假的按「主机随机读写」处理。
	host_visible 为假：按设备本地分配，且申请专用内存（长命且体积大，不做子分配复用）。
	*/
	bool Memory_Context::buffer_create(VkBuffer& buffer_out, VmaAllocation& alloc_out,
		VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool sequential_write,
		VkMemoryPropertyFlags required_flags)
	{
		//前置：分配器与逻辑设备必须就绪
		if (!valid())
		{
			logger.error("缓冲创建:: 内存上下文尚未构建，无法创建缓冲");
			return false;
		}

		//填充缓冲创建信息（独占共享、无初始数据）
		VkBufferCreateInfo buffer_info{};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = size;
		buffer_info.usage = usage;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		//填充分配创建信息
		VmaAllocationCreateInfo alloc_info{};
		alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
		//内存类型的硬性要求（0 = 不限制）
		alloc_info.requiredFlags = required_flags;
		//若需主机可见
		if (host_visible)
		{
			//仅顺序写入者（上传暂存）按顺序写优化
			if (sequential_write)
				alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			//其余按主机可随机读写处理，并保持映射（读写取回同一个指针）
			else
				alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		//若为设备本地
		else
			alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		//创建缓冲与它的内存
		VkResult result = vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer_out, &alloc_out, nullptr);
		//若创建失败
		if (!vk_result_check(result, "缓冲创建:: 创建缓冲"))
		{
			buffer_out = VK_NULL_HANDLE;
			alloc_out = VK_NULL_HANDLE;
			return false;
		}

		logger.info("缓冲创建:: 缓冲已建立（{} 字节，{}）", size, host_visible ? "主机可见" : "设备本地");
		return true;
	}

	//取主机可见缓冲的持久映射指针（第二百五十二轮）
	bool Memory_Context::buffer_map(VkBuffer buffer, VmaAllocation alloc, void*& mapped_out)
	{
		mapped_out = nullptr;
		//前置：分配器、缓冲与分配记录必须有效
		if (!valid() || buffer == VK_NULL_HANDLE || alloc == VK_NULL_HANDLE)
			return false;
		VmaAllocationInfo info{};
		vmaGetAllocationInfo(allocator, alloc, &info);
		//该分配未映射
		if (info.pMappedData == nullptr)
			return false;
		mapped_out = info.pMappedData;
		return true;
	}

	//销毁缓冲
	void Memory_Context::buffer_destroy(VkBuffer buffer, VmaAllocation alloc)
	{
		//句柄不全即视为无需销毁（销毁幂等）
		if (buffer == VK_NULL_HANDLE || alloc == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
			return;

		//归还缓冲与它的内存
		vmaDestroyBuffer(allocator, buffer, alloc);
	}

	//写入缓冲
	bool Memory_Context::buffer_write(VkBuffer buffer, VmaAllocation alloc,
		const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		//前置：句柄齐备且源数据非空
		if (buffer == VK_NULL_HANDLE || alloc == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE || data == nullptr)
		{
			logger.error("缓冲创建:: 写入参数不完整，已跳过");
			return false;
		}

		//映射目标分配
		void* mapped = nullptr;
		VkResult result = vmaMapMemory(allocator, alloc, &mapped);
		//若映射失败
		if (!vk_result_check(result, "缓冲创建:: 映射缓冲（写）"))
			return false;

		//写入数据
		memcpy(static_cast<uint8_t*>(mapped) + offset, data, static_cast<size_t>(size));
		//把写入内容刷向设备可见（非主机一致的内存类型必需，一致的为无操作）
		vmaFlushAllocation(allocator, alloc, offset, size);
		//解除映射（持久映射下不释放映射本体）
		vmaUnmapMemory(allocator, alloc);
		return true;
	}

	//读回缓冲
	bool Memory_Context::buffer_read(VkBuffer buffer, VmaAllocation alloc,
		void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		//前置：句柄齐备且接收缓冲非空
		if (buffer == VK_NULL_HANDLE || alloc == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE || data == nullptr)
		{
			logger.error("缓冲创建:: 读回参数不完整，已跳过");
			return false;
		}

		//映射目标分配
		void* mapped = nullptr;
		VkResult result = vmaMapMemory(allocator, alloc, &mapped);
		//若映射失败
		if (!vk_result_check(result, "缓冲创建:: 映射缓冲（读）"))
			return false;

		//使设备侧写入对主机可见（非主机一致的内存类型必需，一致的为无操作）
		vmaInvalidateAllocation(allocator, alloc, offset, size);
		//读回数据
		memcpy(data, static_cast<const uint8_t*>(mapped) + offset, static_cast<size_t>(size));
		//解除映射（持久映射下不释放映射本体）
		vmaUnmapMemory(allocator, alloc);
		return true;
	}
}