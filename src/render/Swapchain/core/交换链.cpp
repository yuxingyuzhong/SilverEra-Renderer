#include "src/render/Swapchain/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（深度图像创建 / 销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构
	Swapchain_Context::~Swapchain_Context(void)
	{
		//释放已创建的交换链资源
		destroy();
	}

	//构建
	bool Swapchain_Context::build(VkPhysicalDevice physical_device, Memory_Context& memory, VkDevice device, VkSurfaceKHR surface,
		uint32_t graphics_family, uint32_t present_family,
		uint32_t width, uint32_t height, bool vsync)
	{
		//前置：设备与表面必须齐备
		if (physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
		{
			logger.error("交换链:: 构建设备句柄或表面不完整");
			return false;
		}
		//前置：内存上下文必须已构建（深度图像依赖它）
		if (!memory.valid())
		{
			logger.error("交换链:: 内存上下文尚未构建，无法建立交换链（深度图像依赖）");
			return false;
		}
		//若窗口尺寸为零
		if (width == 0 || height == 0)
		{
			logger.warn("交换链:: 窗口尺寸为零，跳过交换链创建（K7）");
			return false;
		}

		//记录设备、表面与内存模块
		this->physical_device = physical_device;
		this->memory = &memory;
		this->device = device;
		this->surface = surface;
		this->graphics_family = graphics_family;
		this->present_family = present_family;
		this->vsync = vsync;

		//创建交换链与图像视图、图像可用信号量、深度图像
		if (!swapchain_create(width, height))
		{
			logger.error("交换链:: 创建失败，回退");
			//回退可能已创建的部分资源
			destroy();
			return false;
		}

		logger.info("交换链:: 交换链已建立（{} 张图像，{}×{}）",
			static_cast<uint32_t>(images.size()), extent.width, extent.height);
		return true;
	}

	//销毁
	void Swapchain_Context::destroy(void)
	{
		//若逻辑设备已记录
		if (device != VK_NULL_HANDLE)
		{
			//先等设备空闲，避免销毁仍在使用的对象（§5.4）
			vkDeviceWaitIdle(device);
			//销毁交换链侧资源
			swapchain_resources_destroy();
		}

		//复位记录
		physical_device = VK_NULL_HANDLE;
		memory = nullptr;
		device = VK_NULL_HANDLE;
		surface = VK_NULL_HANDLE;
		graphics_family = 0;
		present_family = 0;
		vsync = true;
		format = VK_FORMAT_UNDEFINED;
		depth_format = VK_FORMAT_UNDEFINED;
		depth_initialized = false;
		extent = { 0, 0 };
	}

	//是否已构建完成
	bool Swapchain_Context::valid(void) const
	{
		return swapchain != VK_NULL_HANDLE && !images.empty();
	}

	//重建
	bool Swapchain_Context::rebuild(uint32_t width, uint32_t height)
	{
		//前置：交换链侧记录必须齐备
		if (device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
		{
			logger.error("交换链:: 重建失败，交换链尚未构建");
			return false;
		}
		//若请求尺寸为零（最小化）则不得重建（K7）
		if (width == 0 || height == 0)
		{
			logger.warn("交换链:: 重建请求尺寸为零，跳过重建（K7）");
			return false;
		}

		//等待设备空闲，使旧交换链与在途帧不再被引用（§5.3）
		if (!vk_result_check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle"))
			return false;
		//销毁旧资源（图像视图 / 交换链 / 旧图像可用信号量）
		swapchain_resources_destroy();
		//按新尺寸重建
		if (!swapchain_create(width, height))
		{
			logger.error("交换链:: 重建失败");
			//清理可能已创建的半成品资源
			swapchain_resources_destroy();
			return false;
		}

		logger.info("交换链:: 交换链已重建（{} 张图像，{}×{}）",
			static_cast<uint32_t>(images.size()), extent.width, extent.height);
		return true;
	}

	//销毁交换链侧全部资源
	void Swapchain_Context::swapchain_resources_destroy(void)
	{
		//若逻辑设备不可用
		if (device == VK_NULL_HANDLE)
			return;

		//销毁图像可用信号量
		for (VkSemaphore semaphore : image_available_semaphores)
			//若句柄有效
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		//销毁呈现完成信号量
		for (VkSemaphore semaphore : render_finished_semaphores)
			//若句柄有效
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		//销毁深度图像与视图（随交换链一并重建，U5）
		depth_resources_destroy();
		//销毁图像视图
		for (VkImageView view : image_views)
			//若句柄有效
			if (view != VK_NULL_HANDLE)
				vkDestroyImageView(device, view, nullptr);
		//销毁交换链本体
		if (swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device, swapchain, nullptr);

		//清空容器与句柄
		image_available_semaphores.clear();
		render_finished_semaphores.clear();
		image_views.clear();
		images.clear();
		swapchain = VK_NULL_HANDLE;
	}

	//获取交换链图像
	VkImage Swapchain_Context::image_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= images.size())
			return VK_NULL_HANDLE;
		return images[index];
	}

	//获取交换链图像视图
	VkImageView Swapchain_Context::image_view_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= image_views.size())
			return VK_NULL_HANDLE;
		return image_views[index];
	}

	//获取交换链图像数
	uint32_t Swapchain_Context::image_count_get(void) const
	{
		return static_cast<uint32_t>(images.size());
	}

	//获取表面格式
	VkFormat Swapchain_Context::format_get(void) const
	{
		return format;
	}

	//获取交换链范围
	VkExtent2D Swapchain_Context::extent_get(void) const
	{
		return extent;
	}

	//获取交换链句柄
	VkSwapchainKHR Swapchain_Context::handle_get(void) const
	{
		return swapchain;
	}

	//获取指定索引的图像可用信号量
	VkSemaphore Swapchain_Context::image_available_semaphore_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= image_available_semaphores.size())
			return VK_NULL_HANDLE;
		return image_available_semaphores[index];
	}

	//获取图像可用信号量数量
	uint32_t Swapchain_Context::image_available_count_get(void) const
	{
		return static_cast<uint32_t>(image_available_semaphores.size());
	}

	//获取指定索引的呈现完成信号量
	VkSemaphore Swapchain_Context::render_finished_semaphore_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= render_finished_semaphores.size())
			return VK_NULL_HANDLE;
		return render_finished_semaphores[index];
	}

	//获取呈现完成信号量数量
	uint32_t Swapchain_Context::render_finished_count_get(void) const
	{
		return static_cast<uint32_t>(render_finished_semaphores.size());
	}

	//获取指定索引的深度图像
	VkImage Swapchain_Context::depth_image_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= depth_images.size())
			return VK_NULL_HANDLE;
		return depth_images[index];
	}

	//获取指定索引的深度图像视图
	VkImageView Swapchain_Context::depth_view_get(uint32_t index) const
	{
		//越界则返回空句柄
		if (index >= depth_views.size())
			return VK_NULL_HANDLE;
		return depth_views[index];
	}

	//获取深度格式
	VkFormat Swapchain_Context::depth_format_get(void) const
	{
		return depth_format;
	}
}
