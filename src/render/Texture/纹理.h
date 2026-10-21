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
//纹理公共语义类型（Vulkan-free）：采样器状态与寻址模式
#include "Application/Renderer/src/render/Texture/纹理数据.h"

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
		//mip 级数（≥ 1；创建时按 floor(log2(max(宽,高))) + 1 生成完整 mip 链）
		uint32_t mip_levels = 1;
		//第 0 级尺寸（像素；第二十七轮新增，供读回计算容量）
		uint32_t width = 0;
		uint32_t height = 0;
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

		//从内存像素创建纹理（RGBA8 紧排；句柄自 1 起，0 表无效）
		bool texture_create_from_memory(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height);
		/*
		从文件创建纹理（解码 PNG / JPG / BMP 等为 RGBA8 后走内存像素路径）
		解码由外部库 stb_image 承担（唯一定义 STB_IMAGE_IMPLEMENTATION 的编译单元见 core/纹理解码.cpp）；
		文件不存在、为空、解码失败或解码尺寸非法时返回假，且**不留下任何半初始化纹理记录**。
		*/
		bool texture_create_from_file(uint64_t& handle_out, const std::string& path);
		//程序生成棋盘格纹理（RGBA8，两种灰度交替）
		bool texture_create_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size);
		//销毁纹理
		void texture_destroy(uint64_t handle);
		/*
		分离纹理资源（摘出记录但**不销毁**，归还责任移交调用方）
		供门面在运行时卸载路径把图像与其视图压入帧延迟销毁队列；
		成功返回真并写出被摘出的记录；句柄无效或记录已空时返回假（不写出任何资源）。
		*/
		bool texture_detach(uint64_t handle, Texture_Record& record_out);
		//取图像视图与采样器（供描述符写入；句柄无效返回假）
		bool texture_bind_get(uint64_t handle, VkImageView& view_out, VkSampler& sampler_out) const;
		/*
		读回纹理第 0 级像素（第二十七轮新增）
		pixels 为 RGBA8 紧排（逐行自上而下），capacity 须不小于 宽×高×4；
		读回前后图像均保持 SHADER_READ_ONLY_OPTIMAL。调用方须保证无在途帧引用该纹理（门面先 frames_wait）。
		*/
		bool texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回纹理第 0 级的指定子区域（第二十八轮新增）
		x / y 为左上角，区域须完整落在纹理内（越界整次拒绝）；pixels 为该区域 RGBA8 紧排。
		*/
		bool texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		设置共享采样器状态（寻址模式 / 是否启用 mip 过滤）
		状态变化时**销毁并重建共享采样器**，并把全部既有纹理记录改指新句柄；
		注意：已写入的**描述符集仍持有旧采样器句柄**，故调用方（门面）须在切换后重新绑定纹理，
		否则采样仍按旧参数进行。
		*/
		void sampler_state_set(const Texture_Sampler_State& state);
	private:
		/*
		取共享采样器
		按**当前采样器状态**（sampler_state）创建：线性过滤；寻址模式由状态给出；
		mip 过滤由状态决定（启用 = mipmapMode 线性且 maxLod 放开，关闭 = maxLod 钳到 0）；
		全模块共用一个实例，首次调用创建，之后复用（实现见 core/采样器创建.cpp）。
		*/
		bool sampler_create(VkSampler& sampler_out);
		//内存模块（图像创建 / 上传 / 销毁在此完成）
		Memory_Context* memory = nullptr;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//共享采样器（全模块共用，destroy 时统一归还）
		VkSampler shared_sampler = VK_NULL_HANDLE;
		//共享采样器状态（默认与本版次之前行为一致：重复寻址 + 不采样 mip）
		Texture_Sampler_State sampler_state;
		//纹理记录集合（下标 = 句柄 - 1）
		std::vector<Texture_Record> records;
	};
}
