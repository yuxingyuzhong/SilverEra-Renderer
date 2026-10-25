#include "src/render/Mesh/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（缓冲创建 / 上传 / 销毁）
#include "src/render/Memory/内存上下文.h"
//memcpy
#include <cstring>

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
				//归还顶点 / 索引共用缓冲
				memory->buffer_destroy(record.buffer, record.allocation);
				//复位记录
				record.buffer = VK_NULL_HANDLE;
				record.allocation = VK_NULL_HANDLE;
				record.index_offset = 0;
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

		//顶点区字节数
		VkDeviceSize vertex_bytes = sizeof(Vertex) * data.vertices.size();
		//索引区字节数
		VkDeviceSize index_bytes = sizeof(uint32_t) * data.indices.size();
		//顶点 / 索引共用缓冲的总字节数
		VkDeviceSize total_bytes = vertex_bytes + index_bytes;

		//创建顶点 / 索引共用缓冲（设备本地；同时具备顶点 / 索引 / 传输目标用途）
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (!memory->buffer_create(buffer, allocation, total_bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false))
			return false;

		/*
		按「顶点区 + 索引区」紧排组织主机侧连续数据
		两段合并为一次 upload_buffer 提交，故须先拼成一段连续主机内存；
		索引区偏移 = 顶点区字节数（20 字节步长的整数倍，天然满足 uint32 索引对齐）。
		*/
		std::vector<uint8_t> combined(static_cast<std::size_t>(total_bytes));
		std::memcpy(combined.data(), data.vertices.data(), static_cast<std::size_t>(vertex_bytes));
		std::memcpy(combined.data() + vertex_bytes, data.indices.data(), static_cast<std::size_t>(index_bytes));

		//一次性上传（顶点与索引合并在同一缓冲、同一提交内落位）
		if (!memory->upload_buffer(buffer, combined.data(), total_bytes, 0))
		{
			memory->buffer_destroy(buffer, allocation);
			return false;
		}

		//登记网格记录
		Mesh_Record record;
		record.buffer = buffer;
		record.allocation = allocation;
		record.index_offset = vertex_bytes;
		record.index_count = static_cast<uint32_t>(data.indices.size());
		record.vertex_count = static_cast<uint32_t>(data.vertices.size());
		/*
		局部包围盒：按顶点位置逐轴取极值
		只在创建时算一次（句柄有效期间只读）；顶点位置来自 Vertex::position（vec3）。
		*/
		record.bounds_min[0] = record.bounds_max[0] = data.vertices[0].position[0];
		record.bounds_min[1] = record.bounds_max[1] = data.vertices[0].position[1];
		record.bounds_min[2] = record.bounds_max[2] = data.vertices[0].position[2];
		for (const Vertex& vertex : data.vertices)
		{
			for (std::size_t axis = 0; axis < 3; ++axis)
			{
				//若该轴坐标更小则更新下界
				if (vertex.position[axis] < record.bounds_min[axis])
					record.bounds_min[axis] = vertex.position[axis];
				//若该轴坐标更大则更新上界
				if (vertex.position[axis] > record.bounds_max[axis])
					record.bounds_max[axis] = vertex.position[axis];
			}
		}
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
		//内存模块就绪时归还顶点 / 索引共用缓冲
		if (memory != nullptr)
			//归还共用缓冲
			memory->buffer_destroy(record.buffer, record.allocation);
		//复位记录
		record.buffer = VK_NULL_HANDLE;
		record.allocation = VK_NULL_HANDLE;
		record.index_offset = 0;
		record.index_count = 0;
		record.valid = false;
	}

	//分离网格资源（摘出记录不销毁，归还责任移交调用方）
	bool Mesh_Context::mesh_detach(Mesh_Handle handle, Mesh_Record& record_out)
	{
		//默认无资源摘出
		record_out = Mesh_Record{};
		//句柄为零或越界即视为无需分离
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		Mesh_Record& record = records[handle - 1];
		//若记录已无效
		if (!record.valid)
			return false;
		//写出被摘出的记录（阻塞缓冲与内存）
		record_out = record;
		//复位原记录（归还责任已移交调用方）
		record = Mesh_Record{};
		return true;
	}

	//当前有效网格数量（第一百一十三轮）
	uint32_t Mesh_Context::mesh_count(void) const
	{
		//逐条统计有效记录（数量小，无需额外计数成员）
		uint32_t count = 0;
		for (const Mesh_Record& record : records)
			if (record.valid)
				++count;
		return count;
	}

	//取网格缓冲、索引区偏移与索引数
	//取网格顶点 / 索引数量（只读；句柄无效返回假且出参归零）
	bool Mesh_Context::mesh_counts_get(Mesh_Handle handle, uint32_t& vertex_count_out, uint32_t& index_count_out) const
	{
		//出参先复位
		vertex_count_out = 0;
		index_count_out = 0;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取网格记录
		const Mesh_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出数量
		vertex_count_out = record.vertex_count;
		index_count_out = record.index_count;
		return true;
	}

	bool Mesh_Context::mesh_record_get(Mesh_Handle handle, VkBuffer& buffer_out, VkDeviceSize& index_offset_out, uint32_t& index_count_out) const
	{
		//默认输出为空
		buffer_out = VK_NULL_HANDLE;
		index_offset_out = 0;
		index_count_out = 0;
		//若句柄为零或越界
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		const Mesh_Record& record = records[handle - 1];
		//若记录无效
		if (!record.valid)
			return false;
		//写出缓冲、索引区偏移与索引数
		buffer_out = record.buffer;
		index_offset_out = record.index_offset;
		index_count_out = record.index_count;
		return true;
	}

	//取网格局部包围盒
	bool Mesh_Context::mesh_bounds_get(Mesh_Handle handle, float min_out[3], float max_out[3]) const
	{
		//若句柄为零或越界
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		const Mesh_Record& record = records[handle - 1];
		//若记录无效
		if (!record.valid)
			return false;
		//若出参非空则写出包围盒
		if (min_out != nullptr)
			for (std::size_t axis = 0; axis < 3; ++axis)
				min_out[axis] = record.bounds_min[axis];
		if (max_out != nullptr)
			for (std::size_t axis = 0; axis < 3; ++axis)
				max_out[axis] = record.bounds_max[axis];
		return true;
	}
}
