#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 与 VMA 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露这两者的类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
//网格数据（Vertex / Mesh_Data / Mesh_Handle 共用型，Vulkan-free）
#include "Application/Renderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	//内存上下文（前置声明；仅作引用入参，其定义由实现文件包含）
	class Memory_Context;

	/*
	网格记录
	顶点与索引共用一个设备本地缓冲：顶点区自 0 起，索引区自 index_offset 起（= 顶点区字节数）；
	如此一次分配、一次上传即可同时落位顶点与索引，减少 VMA 分配与 GPU 同步次数。
	*/
	struct Mesh_Record
	{
		//顶点 / 索引共用缓冲
		VkBuffer buffer = VK_NULL_HANDLE;
		//缓冲内存
		VmaAllocation allocation = VK_NULL_HANDLE;
		//索引区在缓冲内的字节偏移（= 顶点区字节数；对齐满足 uint32 索引要求）
		VkDeviceSize index_offset = 0;
		//索引数
		uint32_t index_count = 0;
		//记录是否有效
		bool valid = false;
	};

	/*
	网格上下文
	持有网格记录集合，负责顶点 / 索引缓冲的创建设备本地缓冲与上传、销毁。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	句柄自 1 起（0 表无效），下标 = 句柄 - 1。
	*/
	class Mesh_Context
	{
	public:
		//构造（不创建任何网格）
		Mesh_Context(void) = default;
		//析构（内部调用 destroy，保证网格不泄漏）
		~Mesh_Context(void);
		//不可拷贝、不可移动（持有 VMA 句柄）
		Mesh_Context(const Mesh_Context&) = delete;
		Mesh_Context& operator=(const Mesh_Context&) = delete;

		//构建（记录内存模块；不创建任何网格）
		bool build(Memory_Context& memory);
		//销毁（幂等；销毁全部网格缓冲）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		//创建网格（顶点 / 索引合入一个设备本地缓冲后上传；句柄自 1 起，0 表无效）
		bool mesh_create(Mesh_Handle& handle_out, const Mesh_Data& data);
		//销毁网格
		void mesh_destroy(Mesh_Handle handle);
		/*
		分离网格资源（摘出记录但**不销毁**，归还责任移交调用方）
		供门面在运行时卸载路径把缓冲压入帧延迟销毁队列；
		成功返回真并写出被摘出的记录；句柄无效或记录已空时返回假（不写出任何资源）。
		*/
		bool mesh_detach(Mesh_Handle handle, Mesh_Record& record_out);
		//取网格缓冲、索引区偏移与索引数（句柄无效返回假）
		bool mesh_record_get(Mesh_Handle handle, VkBuffer& buffer_out, VkDeviceSize& index_offset_out, uint32_t& index_count_out) const;

		//生成三角形（顶点 3 / 索引 3）
		static Mesh_Data triangle_generate(void);
		//生成轴对齐立方体（边长 2*half_extent；24 顶点 / 36 索引）
		static Mesh_Data cube_generate(float half_extent);
	private:
		//内存模块（缓冲创建与上传在此完成）
		Memory_Context* memory = nullptr;
		//网格记录集合（下标 = 句柄 - 1）
		std::vector<Mesh_Record> records;
	};
}
