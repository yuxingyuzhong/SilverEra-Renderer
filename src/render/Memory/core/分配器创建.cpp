#include "../局部命名空间使用.h"
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构（兜底销毁）
	Memory_Context::~Memory_Context(void)
	{
		destroy();
	}

	/*
	创建 VMA 分配器
	第一版不启用任何 VMA 特性开关：不做内存预算、不做环形上传缓冲、不做延迟分配。
	VMA 默认按 VMA_STATIC_VULKAN_FUNCTIONS=1 直接使用已静态链接的 vulkan-1 入口，
	故不需要（也不应）额外传入函数表。
	*/
	bool Memory_Context::build_allocator(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device)
	{
		//填充分配器创建信息（按 Vulkan 1.0 口径，不启用 1.1+ 特性）
		VmaAllocatorCreateInfo create_info{};
		create_info.vulkanApiVersion = VK_API_VERSION_1_0;
		create_info.instance = instance;
		create_info.physicalDevice = physical_device;
		create_info.device = device;

		//创建分配器
		VkResult result = vmaCreateAllocator(&create_info, &allocator);
		//若创建失败
		if (!vk_result_check(result, "分配器创建:: 创建 VMA 分配器"))
		{
			allocator = VK_NULL_HANDLE;
			return false;
		}

		logger.info("分配器创建:: VMA 分配器已建立");
		return true;
	}

	/*
	构建
	先记录设备侧句柄，再创建 VMA 分配器；失败即回退已记录的状态，不留半初始化。
	*/
	bool Memory_Context::build(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
		uint32_t graphics_family, VkQueue graphics_queue)
	{
		//记录设备侧句柄（分配器创建与一次性上传都要用）
		this->instance = instance;
		this->physical_device = physical_device;
		this->device = device;
		this->graphics_family = graphics_family;
		this->graphics_queue = graphics_queue;

		//创建 VMA 分配器
		if (!build_allocator(instance, physical_device, device))
		{
			//回退已记录的句柄
			destroy();
			return false;
		}

		logger.info("内存上下文:: 内存基础设施构建完成");
		return true;
	}

	//销毁（幂等）
	void Memory_Context::destroy(void)
	{
		//若分配器已创建
		if (allocator != VK_NULL_HANDLE)
		{
			/*
			销毁分配器
			分配器不负责销毁其下缓冲 / 图像：那些必须由调用方先行销毁，
			否则会泄漏（VMA 会在销毁分配器时报告仍有存活分配）。
			*/
			vmaDestroyAllocator(allocator);
			allocator = VK_NULL_HANDLE;
		}

		//清空设备侧句柄
		graphics_queue = VK_NULL_HANDLE;
		graphics_family = 0;
		device = VK_NULL_HANDLE;
		physical_device = VK_NULL_HANDLE;
		instance = VK_NULL_HANDLE;
	}

	//是否已构建完成
	bool Memory_Context::valid(void) const
	{
		return allocator != VK_NULL_HANDLE && device != VK_NULL_HANDLE;
	}

	//获取 VMA 分配器
	VmaAllocator Memory_Context::allocator_get(void) const
	{
		return allocator;
	}

	//获取逻辑设备句柄
	VkDevice Memory_Context::device_get(void) const
	{
		return device;
	}
}
