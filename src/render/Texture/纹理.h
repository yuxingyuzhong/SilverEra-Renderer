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

//游戏引擎命名空间
namespace engine
{
	//内存上下文（前置声明；仅作引用入参，其定义由实现文件包含）
	class Memory_Context;

	//纹理记录（图像 + 视图 + 采样器）
	struct Texture_Record
	{
		//图像
		VkImage image = VK_NULL_HANDLE;
		//图像内存
		VmaAllocation allocation = VK_NULL_HANDLE;
		//图像视图
		VkImageView view = VK_NULL_HANDLE;
		//采样器
		VkSampler sampler = VK_NULL_HANDLE;
		//记录是否有效
		bool valid = false;
	};

	/*
	纹理上下文
	持有纹理记录集合，负责图像创建 / 上传 / 视图与采样器创建及其销毁。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	句柄自 1 起（0 表无效），下标 = 句柄 - 1。
	*/
	class Texture_Context
	{
	public:
		//构造（不创建任何纹理）
		Texture_Context(void) = default;
		//析构（内部调用 destroy，保证纹理不泄漏）
		~Texture_Context(void);
		//不可拷贝、不可移动（持有 VMA 句柄）
		Texture_Context(const Texture_Context&) = delete;
		Texture_Context& operator=(const Texture_Context&) = delete;

		//构建（记录内存模块与逻辑设备）
		bool build(Memory_Context& memory, VkDevice device);
		//销毁（幂等；销毁全部纹理）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		//从内存像素创建纹理（RGBA8 紧排；句柄自 1 起，0 表无效）—— 预留的文件加载入口
		bool texture_create_from_memory(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height);
		//程序生成棋盘格纹理（RGBA8，两种灰度交替）
		bool texture_create_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size);
		//销毁纹理
		void texture_destroy(uint64_t handle);
		//取图像视图与采样器（供描述符写入；句柄无效返回假）
		bool texture_bind_get(uint64_t handle, VkImageView& view_out, VkSampler& sampler_out) const;
	private:
		//创建采样器（线性过滤 / REPEAT 寻址 / mipmap 关闭；实现见 core/采样器创建.cpp）
		bool sampler_create(VkSampler& sampler_out);
		//内存模块（图像创建 / 上传 / 销毁在此完成）
		Memory_Context* memory = nullptr;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//纹理记录集合（下标 = 句柄 - 1）
		std::vector<Texture_Record> records;
	};
}
