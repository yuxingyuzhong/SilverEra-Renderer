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
	/*
	内存上下文
	持有 VMA 分配器，并提供本层全部缓冲 / 图像资源的创建、销毁与一次性上传。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	资源的「创建与销毁同属一个模块」：句柄由调用方持有，但销毁一律回到本类。
	注意：销毁分配器不会连带销毁其下的缓冲 / 图像，那些必须由调用方先行销毁。
	*/
	class Memory_Context
	{
	public:
		//构造（不创建分配器）
		Memory_Context(void) = default;
		//析构（内部调用 destroy，保证分配器不泄漏）
		~Memory_Context(void);
		//不可拷贝、不可移动（持有 VMA 分配器与设备句柄）
		Memory_Context(const Memory_Context&) = delete;
		Memory_Context& operator=(const Memory_Context&) = delete;

		/*
		构建
		instance / physical_device / device：VMA 分配器的必需入参（来自设备模块）。
		graphics_family / graphics_queue：一次性上传的提交目标（来自设备模块）。
		*/
		bool build(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
			uint32_t graphics_family, VkQueue graphics_queue);
		//销毁（幂等；先销毁分配器，再清空记录的句柄）
		void destroy(void);
		//是否已构建完成（分配器与逻辑设备齐备）
		bool valid(void) const;

		/*
		创建缓冲
		host_visible 为真：主机可见，可直接写入 / 读回（也用作上传暂存）；
		                   sequential_write 为真表示只做顺序写入（上传暂存），据此选择更优的内存类型。
		host_visible 为假：设备本地，需另经 upload_buffer 上传。
		*/
		bool buffer_create(VkBuffer& buffer_out, VmaAllocation& alloc_out,
			VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool sequential_write = false);
		//销毁缓冲（缓冲与它的内存一起归还）
		void buffer_destroy(VkBuffer buffer, VmaAllocation alloc);
		//写入缓冲（仅对主机可见的缓冲有意义）
		bool buffer_write(VkBuffer buffer, VmaAllocation alloc,
			const void* data, VkDeviceSize size, VkDeviceSize offset);
		//读回缓冲（仅对主机可见的缓冲有意义；用于校验与调试）
		bool buffer_read(VkBuffer buffer, VmaAllocation alloc,
			void* data, VkDeviceSize size, VkDeviceSize offset);

		/*
		创建图像
		布局固定 VK_IMAGE_TILING_OPTIMAL；usage 须含 VK_IMAGE_USAGE_TRANSFER_DST_BIT
		才能经 upload_image 写入像素。
		*/
		bool image_create(VkImage& image_out, VmaAllocation& alloc_out,
			uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels = 1);
		//销毁图像（图像与它的内存一起归还）
		void image_destroy(VkImage image, VmaAllocation alloc);
		//创建图像视图（二维、单层、单 mip）
		bool image_view_create(VkImageView& view_out, VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t level_count = 1);
		//销毁图像视图
		void image_view_destroy(VkImageView view);

		/*
		一次性同步上传
		复用本类持有的上传命令池与完成栅栏（首次上传时创建，之后每次仅
		重置命令池 → 录制 → 提交 → 等待），不逐次创建 / 销毁命令池，也不依赖帧模块。
		*/
		bool upload_buffer(VkBuffer dst_buffer, const void* data, VkDeviceSize size, VkDeviceSize offset);
		/*
		上传像素到图像
		data_size 为源数据字节数；row_pixels 为一行的纹素数（传 0 表示按 width 紧排）。
		终态布局固定为 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL（采样用途）。
		*/
		bool upload_image(VkImage dst_image, const void* data, VkDeviceSize data_size,
			uint32_t width, uint32_t height, uint32_t row_pixels);
		/*
		生成 mip 链（第 0 级须已由 upload_image 写入并处于着色器可读布局）
		对第 1..mip_levels-1 级逐级 vkCmdBlitImage 降采样（线性过滤），
		一次提交完成全部级；结束后第 0 级恢复为着色器可读布局、其余级亦为着色器可读布局。
		mip_levels ≤ 1 时为空操作（返回真）。
		*/
		bool image_generate_mips(VkImage image, uint32_t width, uint32_t height, uint32_t mip_levels);

		//获取 VMA 分配器（供上层创建自有资源时复用）
		VmaAllocator allocator_get(void) const;
		//获取逻辑设备句柄
		VkDevice device_get(void) const;
	private:
		//创建 VMA 分配器（实现见 core/分配器创建.cpp）
		bool build_allocator(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device);
		//确保一次性上传的命令池与栅栏就绪（首次调用创建，之后复用；实现见 core/上传批次.cpp）
		bool upload_resources_ensure(void);
	private:
		//VMA 分配器
		VmaAllocator allocator = VK_NULL_HANDLE;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//实例
		VkInstance instance = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//一次性上传命令池（复用；首次上传时创建，destroy 时销毁）
		VkCommandPool upload_pool = VK_NULL_HANDLE;
		//一次性上传完成栅栏（复用；首次上传时创建，destroy 时销毁）
		VkFence upload_fence = VK_NULL_HANDLE;
	};
}
