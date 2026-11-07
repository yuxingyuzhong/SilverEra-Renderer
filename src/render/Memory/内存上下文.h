#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
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
		required_flags 为对内存类型的硬性要求（0 = 不限制；第二百五十二轮用于要求主机一致内存）。
		*/
		bool buffer_create(VkBuffer& buffer_out, VmaAllocation& alloc_out,
			VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, bool sequential_write = false,
			VkMemoryPropertyFlags required_flags = 0);
		//销毁缓冲（缓冲与它的内存一起归还）
		void buffer_destroy(VkBuffer buffer, VmaAllocation alloc);
		//写入缓冲（仅对主机可见的缓冲有意义）
		bool buffer_write(VkBuffer buffer, VmaAllocation alloc,
			const void* data, VkDeviceSize size, VkDeviceSize offset);
		//读回缓冲（仅对主机可见的缓冲有意义；用于校验与调试）
		bool buffer_read(VkBuffer buffer, VmaAllocation alloc,
			void* data, VkDeviceSize size, VkDeviceSize offset);
		/*
		取主机可见缓冲的持久映射指针（第二百五十二轮）
		缓冲须以 host_visible 创建（分配时已带 MAPPED 标志）；失败或未映射时 mapped_out 置空并返回假。
		*/
		bool buffer_map(VkBuffer buffer, VmaAllocation alloc, void*& mapped_out);

		/*
		创建图像
		布局固定 VK_IMAGE_TILING_OPTIMAL；usage 须含 VK_IMAGE_USAGE_TRANSFER_DST_BIT
		才能经 upload_image 写入像素。
		samples 为采样数（第九十六轮；默认 1，2/4/8 供 MSAA 附件使用）。
		*/
		bool image_create(VkImage& image_out, VmaAllocation& alloc_out,
			uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
			uint32_t mip_levels = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
			uint32_t array_layers = 1, VkImageCreateFlags flags = 0);
		//销毁图像（图像与它的内存一起归还）
		void image_destroy(VkImage image, VmaAllocation alloc);
		/*
		创建图像视图（二维 / 立方体；级数由调用方给出，默认 1；第一百二十五轮增视图类型与层数）
		view_type 为 VK_IMAGE_VIEW_TYPE_2D 或 VK_IMAGE_VIEW_TYPE_CUBE；layer_count 为覆盖的数组层数（立方体为 6）。
		base_mip_level 为起始级（第二百五十三轮；默认 0，> 0 时用于 Hi-Z 金字塔的逐级视图）。
		*/
		bool image_view_create(VkImageView& view_out, VkImage image, VkFormat format, VkImageAspectFlags aspect,
			uint32_t level_count = 1, VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D, uint32_t layer_count = 1,
			uint32_t base_mip_level = 0);
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
			uint32_t width, uint32_t height, uint32_t row_pixels, uint32_t layer = 0);
		/*
		上传像素到图像的子区域（第一百五十一轮新增；第一百五十八轮补 mip 级）
		x / y 为区域左上角，width / height 为区域尺寸；data 为 RGBA8 紧排（每行 width 个纹素）。
		目标级 mip_level 须已处于 SHADER_READ_ONLY_OPTIMAL（由 upload_image 或 image_generate_mips 建立）；
		调用前后保持该布局。区域须完整落在该级内（由调用方保证）。
		*/
		bool upload_image_region(VkImage dst_image, const void* data, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, uint32_t mip_level = 0);
		/*
		从图像读回像素（第二十七轮新增）
		读取第 0 级、整幅图像到 data（RGBA8 紧排，data_size 须为 宽×高×4）；
		读回前后图像均保持 SHADER_READ_ONLY_OPTIMAL（复用一次性上传命令池同步完成）。
		调用方须保证此刻无在途帧引用该图像。
		*/
		bool image_readback(VkImage image, uint32_t width, uint32_t height, void* data, VkDeviceSize data_size);
		/*
		从图像读回指定子区域（第二十八轮新增；第一百五十八轮补 mip 级）
		x / y 为左上角，region_width / region_height 为区域尺寸（须落在该级内，由调用方保证）；
		读回前后图像均保持 SHADER_READ_ONLY_OPTIMAL。
		*/
		bool image_readback_region(VkImage image, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, void* data, VkDeviceSize data_size,
			uint32_t mip_level = 0);
		/*
		生成 mip 链（第 0 级须已由 upload_image 写入并处于着色器可读布局）
		对第 1..mip_levels-1 级逐级 vkCmdBlitImage 降采样（线性过滤），
		一次提交完成全部级；结束后第 0 级恢复为着色器可读布局、其余级亦为着色器可读布局。
		mip_levels ≤ 1 时为空操作（返回真）。
		*/
		bool image_generate_mips(VkImage image, uint32_t width, uint32_t height, uint32_t mip_levels);
		/*
		从指定级起重生成 mip 链（第一百五十八轮新增）
		以 base_level 级（尺寸 base_width × base_height、须处于着色器可读布局）为源，
		逐级向下生成 base_level+1 .. mip_levels-1（级总数仍为 mip_levels）；
		用于「只更新某一级子区域」后刷新其下游级。base_level ≥ mip_levels-1 时为空操作。
		*/
		bool image_generate_mips_from(VkImage image, uint32_t base_level,
			uint32_t base_width, uint32_t base_height, uint32_t mip_levels);

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
