#include "src/render/Memory/局部命名空间使用.h"
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	创建图像
	布局固定为 VK_IMAGE_TILING_OPTIMAL（设备本地、供采样），初始布局为未定义；
	层数 / 采样数固定为 1；**级数由调用方给出**（默认 1，即无 mip 链；> 1 时须再调 image_generate_mips）。
	*/
	bool Memory_Context::image_create(VkImage& image_out, VmaAllocation& alloc_out,
		uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, uint32_t mip_levels)
	{
		//前置：分配器与逻辑设备必须就绪
		if (!valid())
		{
			logger.error("图像创建:: 内存上下文尚未构建，无法创建图像");
			return false;
		}
		//前置：级数必须为正
		if (mip_levels == 0)
		{
			logger.error("图像创建:: mip 级数必须为正");
			return false;
		}

		//填充图像创建信息
		VkImageCreateInfo image_info{};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = format;
		image_info.extent = { width, height, 1 };
		image_info.mipLevels = mip_levels;
		image_info.arrayLayers = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = usage;
		image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		//填充分配创建信息（图像一律设备本地，长命且体积大）
		VmaAllocationCreateInfo alloc_info{};
		alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
		alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		//创建图像与它的内存
		VkResult result = vmaCreateImage(allocator, &image_info, &alloc_info, &image_out, &alloc_out, nullptr);
		//若创建失败
		if (!vk_result_check(result, "图像创建:: 创建图像"))
		{
			image_out = VK_NULL_HANDLE;
			alloc_out = VK_NULL_HANDLE;
			return false;
		}

		logger.info("图像创建:: 图像已建立（{}x{}，mip 级数 {}）", width, height, mip_levels);
		return true;
	}

	//销毁图像
	void Memory_Context::image_destroy(VkImage image, VmaAllocation alloc)
	{
		//句柄不全即视为无需销毁（销毁幂等）
		if (image == VK_NULL_HANDLE || alloc == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
			return;

		//归还图像与它的内存
		vmaDestroyImage(allocator, image, alloc);
	}

	//创建图像视图（二维；级数由调用方给出，默认 1）
	bool Memory_Context::image_view_create(VkImageView& view_out, VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t level_count)
	{
		//前置：图像句柄与逻辑设备必须齐备
		if (image == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
		{
			logger.error("图像创建:: 创建图像视图的前置句柄不完整");
			return false;
		}
		//前置：级数必须为正
		if (level_count == 0)
		{
			logger.error("图像创建:: 图像视图的 mip 级数必须为正");
			return false;
		}

		//填充视图创建信息（二维、自第 0 级起覆盖 level_count 级、单层）
		VkImageViewCreateInfo view_info{};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = image;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = format;
		view_info.subresourceRange.aspectMask = aspect;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = level_count;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;

		//创建视图
		VkResult result = vkCreateImageView(device, &view_info, nullptr, &view_out);
		//若创建失败
		if (!vk_result_check(result, "图像创建:: 创建图像视图"))
		{
			view_out = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}

	//销毁图像视图
	void Memory_Context::image_view_destroy(VkImageView view)
	{
		//句柄与逻辑设备不全即视为无需销毁（销毁幂等）
		if (view == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
			return;

		//归还视图（图像本身由 image_destroy 负责）
		vkDestroyImageView(device, view, nullptr);
	}
}
