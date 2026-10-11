#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（缓冲创建 / 上传 / 销毁）
#include "Application/Renderer/src/render/Memory/内存上下文.h"

namespace engine
{
	//析构
	Mesh_Context::~Mesh_Context(void)
	{
		//释放全部网格缓冲
		destroy();
	}

	//构建
	bool Mesh_Context::build(Memory_Context& memory)
	{
		//前置：内存上下文必须已构建
		if (!memory.valid())
		{
			logger.error("网格上下文:: 内存上下文尚未构建，无法建立网格上下文");
			return false;
		}

		//记录内存模块
		this->memory = &memory;
		return true;
	}

	//销毁
	void Mesh_Context::destroy(void)
	{
		//若内存模块已记录
		if (memory != nullptr)
			//释放全部网格缓冲
			for (auto& record : records)
			{
				//归还顶点缓冲
				memory->buffer_destroy(record.vertex_buffer, record.vertex_allocation);
				//归还索引缓冲
				memory->buffer_destroy(record.index_buffer, record.index_allocation);
				//复位记录
				record.vertex_buffer = VK_NULL_HANDLE;
				record.vertex_allocation = VK_NULL_HANDLE;
				record.index_buffer = VK_NULL_HANDLE;
				record.index_allocation = VK_NULL_HANDLE;
				record.index_count = 0;
				record.valid = false;
			}

		//清空记录并复位
		records.clear();
		memory = nullptr;
	}

	//是否已构建完成
	bool Mesh_Context::valid(void) const
	{
		return memory != nullptr;
	}

	//创建网格
	bool Mesh_Context::mesh_create(Mesh_Handle& handle_out, const Mesh_Data& data)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：内存模块必须就绪
		if (memory == nullptr || !memory->valid())
		{
			logger.error("网格缓冲:: 内存上下文尚未构建，无法创建网格");
			return false;
		}
		//前置：顶点与索引均不得为空
		if (data.vertices.empty() || data.indices.empty())
		{
			logger.error("网格缓冲:: 顶点或索引为空，无法创建网格");
			return false;
		}
		//校验索引不越界
		for (uint32_t index : data.indices)
			//若索引越界
			if (index >= data.vertices.size())
			{
				logger.error("网格缓冲:: 索引 {} 越界（顶点数 {}）", index, static_cast<uint32_t>(data.vertices.size()));
				return false;
			}

		//顶点缓冲字节数
		VkDeviceSize vertex_bytes = sizeof(Vertex) * data.vertices.size();
		//创建顶点缓冲（设备本地，可作传输目标）
		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		VmaAllocation vertex_allocation = VK_NULL_HANDLE;
		if (!memory->buffer_create(vertex_buffer, vertex_allocation, vertex_bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false))
			return false;
		//上传顶点数据
		if (!memory->upload_buffer(vertex_buffer, data.vertices.data(), vertex_bytes, 0))
		{
			memory->buffer_destroy(vertex_buffer, vertex_allocation);
			return false;
		}

		//索引缓冲字节数
		VkDeviceSize index_bytes = sizeof(uint32_t) * data.indices.size();
		//创建索引缓冲（设备本地，可作传输目标）
		VkBuffer index_buffer = VK_NULL_HANDLE;
		VmaAllocation index_allocation = VK_NULL_HANDLE;
		if (!memory->buffer_create(index_buffer, index_allocation, index_bytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false))
		{
			memory->buffer_destroy(vertex_buffer, vertex_allocation);
			return false;
		}
		//上传索引数据
		if (!memory->upload_buffer(index_buffer, data.indices.data(), index_bytes, 0))
		{
			memory->buffer_destroy(vertex_buffer, vertex_allocation);
			memory->buffer_destroy(index_buffer, index_allocation);
			return false;
		}

		//登记网格记录
		Mesh_Record record;
		record.vertex_buffer = vertex_buffer;
		record.vertex_allocation = vertex_allocation;
		record.index_buffer = index_buffer;
		record.index_allocation = index_allocation;
		record.index_count = static_cast<uint32_t>(data.indices.size());
		record.valid = true;
		records.push_back(record);

		//句柄自 1 起
		handle_out = static_cast<Mesh_Handle>(records.size());
		logger.info("网格缓冲:: 网格已建立（句柄 {}，顶点 {}，索引 {}）",
			handle_out, static_cast<uint32_t>(data.vertices.size()), record.index_count);
		return true;
	}

	//销毁网格
	void Mesh_Context::mesh_destroy(Mesh_Handle handle)
	{
		//句柄为零或越界即视为无需销毁（销毁幂等）
		if (handle == 0 || handle > records.size())
			return;
		//获取目标记录
		Mesh_Record& record = records[handle - 1];
		//若记录已无效
		if (!record.valid)
			return;
		//内存模块就绪时归还缓冲
		if (memory != nullptr)
		{
			//归还顶点缓冲
			memory->buffer_destroy(record.vertex_buffer, record.vertex_allocation);
			//归还索引缓冲
			memory->buffer_destroy(record.index_buffer, record.index_allocation);
		}
		//复位记录
		record.vertex_buffer = VK_NULL_HANDLE;
		record.vertex_allocation = VK_NULL_HANDLE;
		record.index_buffer = VK_NULL_HANDLE;
		record.index_allocation = VK_NULL_HANDLE;
		record.index_count = 0;
		record.valid = false;
	}

	//取网格缓冲与索引数
	bool Mesh_Context::mesh_record_get(Mesh_Handle handle, VkBuffer& vertex_buffer_out, VkBuffer& index_buffer_out, uint32_t& index_count_out) const
	{
		//默认输出为空
		vertex_buffer_out = VK_NULL_HANDLE;
		index_buffer_out = VK_NULL_HANDLE;
		index_count_out = 0;
		//若句柄为零或越界
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		const Mesh_Record& record = records[handle - 1];
		//若记录无效
		if (!record.valid)
			return false;
		//写出句柄与索引数
		vertex_buffer_out = record.vertex_buffer;
		index_buffer_out = record.index_buffer;
		index_count_out = record.index_count;
		return true;
	}
}
