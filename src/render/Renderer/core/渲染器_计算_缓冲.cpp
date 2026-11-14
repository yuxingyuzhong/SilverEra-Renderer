#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	//是否支持 GPU 时间戳

	bool Render_Compute::gpu_timestamp_supported(void) const
	{
		//若尚未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		//转调帧模块能力标志
		return owner->impl->frame.gpu_timestamp_supported_get();
	}

	bool Render_Compute::readback_supported(void) const
	{
		//若尚未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		//交换链图像具备传输源用法才可回读
		return owner->impl->swapchain.color_transfer_src_get();
	}

	bool Render_Compute::compute_supported(void) const
	{
		//若尚未初始化或无健康检查管线
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || state().compute_health_handle == 0)
			return false;
		return state().compute.supported();
	}

	bool Render_Compute::compute_health_check(uint32_t magic, uint32_t& value_out)
	{
		//若尚未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return false;
		//不支持计算路径
		if (!compute_supported())
			return false;
		return state().compute.self_check_run(magic, value_out);
	}

	VkBuffer Renderer_Impl_Holder::Impl::compute_buffer_native(uint64_t handle) const
	{
		//计算域尚未登记（半初始化）
		if (compute_domain == nullptr)
			return VK_NULL_HANDLE;
		//句柄无效
		if (handle == 0)
			return VK_NULL_HANDLE;
		//线性查找（缓冲数量有限）
		for (const Compute_Buffer_Record& record : compute_domain->state().compute_buffers)
			if (record.handle == handle)
				return record.buffer;
		return VK_NULL_HANDLE;
	}

	bool Renderer_Impl_Holder::Impl::compute_buffer_create_usage(Compute_Buffer_Handle& handle_out, uint32_t size,
		const void* data, VkBufferUsageFlags extra_usage)
	{
		//失败路径一律复位句柄
		handle_out = 0;
		//计算域尚未登记（半初始化）
		if (compute_domain == nullptr)
			return false;
		//计算域状态
		Compute_State& compute_state = compute_domain->state();
		//尺寸非法
		if (size == 0 || size > COMPUTE_BUFFER_MAX_BYTES)
			return false;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		//主机可见存储缓冲（恒可作拷贝源）＋ 追加用法
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | extra_usage;
		if (!memory.buffer_create(buffer, allocation, size, usage, true, false))
			return false;
		//写入初值（可选）
		if (data != nullptr && !memory.buffer_write(buffer, allocation, data, size, 0))
		{
			memory.buffer_destroy(buffer, allocation);
			return false;
		}
		Compute_Buffer_Record record;
		record.handle = compute_state.next_compute_buffer_handle++;
		record.buffer = buffer;
		record.allocation = allocation;
		record.size = size;
		record.host_visible = true;
		handle_out = record.handle;
		compute_state.compute_buffers.push_back(record);
		return true;
	}

	bool Render_Compute::compute_buffer_load(Compute_Buffer_Handle& handle_out, uint32_t size, const void* data, bool device_local)
	{
		//失败路径一律复位句柄（与材质句柄约定一致）
		handle_out = 0;
		//未初始化或不支持计算，或尺寸非法
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported()
			|| size == 0 || size > COMPUTE_BUFFER_MAX_BYTES)
			return false;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		//存储缓冲用法：恒可作拷贝源（回读），设备本地另需拷贝目标（上传）
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (device_local)
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		//主机可见（默认）或设备本地
		if (!owner->impl->memory.buffer_create(buffer, allocation, size, usage, !device_local, false))
		{
			logger.error("渲染器:: 计算存储缓冲创建失败（{} 字节）", size);
			return false;
		}
		//写入初值（可选）：主机可见直接映射，设备本地经暂存上传
		if (data != nullptr)
		{
			bool seeded = device_local
				? owner->impl->memory.upload_buffer(buffer, data, size, 0)
				: owner->impl->memory.buffer_write(buffer, allocation, data, size, 0);
			//初值写入失败即回收缓冲
			if (!seeded)
			{
				owner->impl->memory.buffer_destroy(buffer, allocation);
				return false;
			}
		}
		Compute_Buffer_Record record;
		record.handle = state().next_compute_buffer_handle++;
		record.buffer = buffer;
		record.allocation = allocation;
		record.size = size;
		record.host_visible = !device_local;
		handle_out = record.handle;
		state().compute_buffers.push_back(record);
		return true;
	}

	void Render_Compute::compute_buffer_unload(Compute_Buffer_Handle handle)
	{
		//未初始化
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return;
		for (std::size_t index = 0; index < state().compute_buffers.size(); ++index)
		{
			//命中即移交延迟队列并移出列表（避免在途命令缓冲仍引用该缓冲）
			if (state().compute_buffers[index].handle == handle)
			{
				Deferred_Entry entry{};
				Deferred_Memory_Resource resource{};
				resource.buffer = state().compute_buffers[index].buffer;
				resource.buffer_allocation = state().compute_buffers[index].allocation;
				entry.memory_resources.push_back(resource);
				//延迟 = 在途帧数 + 1（与网格 / 纹理卸载同口径）
				owner->impl->frame.deferred_push(std::move(entry), owner->impl->frame.in_flight_count_get() + 1);
				state().compute_buffers.erase(state().compute_buffers.begin() + static_cast<std::ptrdiff_t>(index));
				return;
			}
		}
	}

	bool Render_Compute::compute_buffer_write(Compute_Buffer_Handle handle, const void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或数据为空
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || data == nullptr || size == 0)
			return false;
		for (Compute_Buffer_Record& record : state().compute_buffers)
		{
			//命中句柄并做越界检查
			if (record.handle == handle)
			{
				if (static_cast<uint64_t>(offset) + size > record.size)
					return false;
				//设备本地缓冲经暂存上传，主机可见缓冲直接映射
				if (!record.host_visible)
					return owner->impl->memory.upload_buffer(record.buffer, data, size, offset);
				return owner->impl->memory.buffer_write(record.buffer, record.allocation, data, size, offset);
			}
		}
		return false;
	}

	bool Render_Compute::compute_buffer_read(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或输出为空
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || data == nullptr || size == 0)
			return false;
		for (Compute_Buffer_Record& record : state().compute_buffers)
		{
			//命中句柄并做越界检查
			if (record.handle == handle)
			{
				if (static_cast<uint64_t>(offset) + size > record.size)
					return false;
				return owner->impl->memory.buffer_read(record.buffer, record.allocation, data, size, offset);
			}
		}
		return false;
	}

	bool Render_Compute::compute_readback(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或输出为空
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || data == nullptr || size == 0)
			return false;
		//查找目标缓冲并做越界检查
		Compute_Buffer_Record* target = nullptr;
		for (Compute_Buffer_Record& record : state().compute_buffers)
		{
			//命中句柄
			if (record.handle == handle)
			{
				target = &record;
				break;
			}
		}
		//句柄无效或越界
		if (target == nullptr || static_cast<uint64_t>(offset) + size > target->size)
			return false;
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_allocation = VK_NULL_HANDLE;
		//暂存缓冲（主机可见，仅拷贝目标）
		if (!owner->impl->memory.buffer_create(staging, staging_allocation, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false))
		{
			logger.error("渲染器:: 计算回读暂存缓冲创建失败（{} 字节）", size);
			return false;
		}
		VkBuffer source = target->buffer;
		//一次性提交：计算→传输屏障 → 拷到暂存 → 传输→主机屏障
		bool copied = state().compute.single_submit([&](VkCommandBuffer command_buffer) -> bool
		{
			if (!state().compute.barrier(command_buffer, Compute_Barrier::ComputeToTransfer))
				return false;
			VkBufferCopy region{};
			region.srcOffset = offset;
			region.dstOffset = 0;
			region.size = size;
			vkCmdCopyBuffer(command_buffer, source, staging, 1, &region);
			return state().compute.barrier(command_buffer, Compute_Barrier::TransferToHost);
		});
		//拷贝成功才映射读回
		bool read = copied && owner->impl->memory.buffer_read(staging, staging_allocation, data, size, 0);
		owner->impl->memory.buffer_destroy(staging, staging_allocation);
		return read;
	}

	Compute_Stats Render_Compute::compute_stats_get(void) const
	{
		//未初始化：计数为零、计时 −1
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
		{
			Compute_Stats empty;
			return empty;
		}
		return state().compute.stats_get();
	}

	bool Render_Compute::compute_timestamp_supported(void) const
	{
		//未初始化或不支持计算路径
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized || !compute_supported())
			return false;
		return state().compute.timestamp_supported();
	}

	Compute_Capabilities Render_Compute::compute_capabilities_get(void) const
	{
		//未初始化：各项为假
		if ((owner == nullptr || owner->impl == nullptr) || !owner->impl->initialized)
			return Compute_Capabilities{};
		return state().compute_capabilities;
	}
}
