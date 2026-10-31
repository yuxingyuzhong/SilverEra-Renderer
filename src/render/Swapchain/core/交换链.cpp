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

		//创建交换链与图像视图、图像可用信号量、深度图像（首建无旧链，故 oldSwapchain 传空）
		if (!swapchain_create(width, height, VK_NULL_HANDLE))
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
	bool Swapchain_Context::rebuild(uint32_t width, uint32_t height, Swapchain_Old_Resources& old_out)
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

		/*
		保存派生状态（重建失败回滚用）
		这些状态依赖旧资源，抽出 / 销毁流程会将其复位，故先留存，供失败时原样恢复。
		*/
		VkFormat saved_format = format;
		VkFormat saved_depth_format = depth_format;
		VkExtent2D saved_extent = extent;
		bool saved_depth_initialized = depth_initialized;
		bool saved_color_transfer_src = color_transfer_src;

		/*
		析出旧资源（不销毁、不等设备空闲）
		旧交换链作 newSwapchain 的 oldSwapchain 复用；其附属资源（图像视图 / 信号量 / 深度图像）
		可能仍被在途帧引用，故整体移交给调用方，由帧模块延迟销毁（§6.4）。
		*/
		Swapchain_Old_Resources old{};
		swapchain_resources_extract(old);

		//以旧交换链作 oldSwapchain 建新链
		if (!swapchain_create(width, height, old.swapchain))
		{
			logger.error("交换链:: 重建失败，清理半成品并回滚旧资源");
			//清理可能已创建的半成品新资源
			swapchain_resources_destroy();
			//恢复旧资源句柄（交换链仍可用）
			swapchain_resources_restore(old);
			//恢复派生状态
			format = saved_format;
			depth_format = saved_depth_format;
			extent = saved_extent;
			depth_initialized = saved_depth_initialized;
			color_transfer_src = saved_color_transfer_src;
			return false;
		}

		//新链已建立，旧资源移交调用方延迟销毁
		old_out = std::move(old);
		logger.info("交换链:: 交换链已重建（{} 张图像，{}×{}）",
			static_cast<uint32_t>(images.size()), extent.width, extent.height);
		return true;
	}

	//析出全部交换链侧资源到旧资源集合（不销毁）
	void Swapchain_Context::swapchain_resources_extract(Swapchain_Old_Resources& old_out)
	{
		//移出交换链句柄
		old_out.swapchain = swapchain;
		swapchain = VK_NULL_HANDLE;
		//移出交换链图像句柄记录
		old_out.images = std::move(images);
		images.clear();
		//移出图像视图
		old_out.image_views = std::move(image_views);
		image_views.clear();
		//移出图像可用信号量
		old_out.image_available_semaphores = std::move(image_available_semaphores);
		image_available_semaphores.clear();
		//移出呈现完成信号量
		old_out.render_finished_semaphores = std::move(render_finished_semaphores);
		render_finished_semaphores.clear();
		//移出深度图像 / 内存 / 视图
		old_out.depth_images = std::move(depth_images);
		old_out.depth_allocations = std::move(depth_allocations);
		old_out.depth_views = std::move(depth_views);
		depth_images.clear();
		depth_allocations.clear();
		depth_views.clear();
		//移出 MSAA 颜色 / 深度附件（第九十六轮）
		old_out.msaa_color_images = std::move(msaa_color_images);
		old_out.msaa_color_allocations = std::move(msaa_color_allocations);
		old_out.msaa_color_views = std::move(msaa_color_views);
		old_out.msaa_depth_images = std::move(msaa_depth_images);
		old_out.msaa_depth_allocations = std::move(msaa_depth_allocations);
		old_out.msaa_depth_views = std::move(msaa_depth_views);
		msaa_color_images.clear();
		msaa_color_allocations.clear();
		msaa_color_views.clear();
		msaa_depth_images.clear();
		msaa_depth_allocations.clear();
		msaa_depth_views.clear();
		//复位派生状态（新链建立时重新确定；深度首次转换标志须复位以令新深度图像重新转换）
		format = VK_FORMAT_UNDEFINED;
		extent = { 0, 0 };
		color_transfer_src = false;
		depth_initialized = false;
	}

	//把旧资源集合原样恢复回成员（重建失败回滚用）
	void Swapchain_Context::swapchain_resources_restore(Swapchain_Old_Resources& old_in)
	{
		//恢复交换链句柄
		swapchain = old_in.swapchain;
		//恢复交换链图像句柄记录
		images = std::move(old_in.images);
		//恢复图像视图
		image_views = std::move(old_in.image_views);
		//恢复图像可用信号量
		image_available_semaphores = std::move(old_in.image_available_semaphores);
		//恢复呈现完成信号量
		render_finished_semaphores = std::move(old_in.render_finished_semaphores);
		//恢复深度图像 / 内存 / 视图
		depth_images = std::move(old_in.depth_images);
		depth_allocations = std::move(old_in.depth_allocations);
		depth_views = std::move(old_in.depth_views);
		//恢复 MSAA 颜色 / 深度附件（第九十六轮）
		msaa_color_images = std::move(old_in.msaa_color_images);
		msaa_color_allocations = std::move(old_in.msaa_color_allocations);
		msaa_color_views = std::move(old_in.msaa_color_views);
		msaa_depth_images = std::move(old_in.msaa_depth_images);
		msaa_depth_allocations = std::move(old_in.msaa_depth_allocations);
		msaa_depth_views = std::move(old_in.msaa_depth_views);
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
		//销毁 MSAA 颜色 / 深度附件（第九十六轮）
		msaa_resources_destroy();
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

	//交换链图像是否具备传输源用法
	bool Swapchain_Context::color_transfer_src_get(void) const
	{
		return color_transfer_src;
	}

	//设置 MSAA 采样数（钳制到 1 / 2 / 4 / 8）
	void Swapchain_Context::sample_count_set(uint32_t count)
	{
		//仅接受 2 的幂且不超过 8 的采样数，其余按 1 处理
		if (count != 2 && count != 4 && count != 8)
			count = 1;
		sample_count = count;
	}

	//当前 MSAA 采样数
	uint32_t Swapchain_Context::sample_count_get(void) const
	{
		return sample_count;
	}

	//指定索引的 MSAA 颜色图像
	VkImage Swapchain_Context::msaa_color_image_get(uint32_t index) const
	{
		//越界或未启用则返回空句柄
		if (index >= msaa_color_images.size())
			return VK_NULL_HANDLE;
		return msaa_color_images[index];
	}

	//指定索引的 MSAA 颜色视图
	VkImageView Swapchain_Context::msaa_color_view_get(uint32_t index) const
	{
		//越界或未启用则返回空句柄
		if (index >= msaa_color_views.size())
			return VK_NULL_HANDLE;
		return msaa_color_views[index];
	}

	//指定索引的 MSAA 深度视图
	VkImageView Swapchain_Context::msaa_depth_view_get(uint32_t index) const
	{
		//越界或未启用则返回空句柄
		if (index >= msaa_depth_views.size())
			return VK_NULL_HANDLE;
		return msaa_depth_views[index];
	}

	//指定索引的 MSAA 深度图像（第二百六十轮）
	VkImage Swapchain_Context::msaa_depth_image_get(uint32_t index) const
	{
		//越界或未启用则返回空句柄
		if (index >= msaa_depth_images.size())
			return VK_NULL_HANDLE;
		return msaa_depth_images[index];
	}
}
