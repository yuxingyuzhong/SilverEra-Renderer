#include "src/render/Texture/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像创建 / 上传 / 销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构
	Texture_Context::~Texture_Context(void)
	{
		//释放全部纹理
		destroy();
	}

	//构建
	bool Texture_Context::build(Memory_Context& memory, VkDevice device)
	{
		//前置：内存上下文必须已构建
		if (!memory.valid())
		{
			logger.error("纹理上下文:: 内存上下文尚未构建，无法建立纹理上下文");
			return false;
		}
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("纹理上下文:: 构建时逻辑设备为空");
			return false;
		}

		//记录内存模块与逻辑设备
		this->memory = &memory;
		this->device = device;
		return true;
	}

	//销毁
	void Texture_Context::destroy(void)
	{
		//逐条销毁全部纹理（记录彼此独立，销毁顺序不影响正确性）
		for (auto& record : records)
		{
			//若内存模块已记录则归还图像视图与图像
			if (memory != nullptr)
			{
				//归还图像视图
				memory->image_view_destroy(record.view);
				//归还图像与它的内存
				memory->image_destroy(record.image, record.allocation);
			}
			//复位记录
			record = Texture_Record{};
		}
		//归还共享采样器
		if (device != VK_NULL_HANDLE && shared_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, shared_sampler, nullptr);
			shared_sampler = VK_NULL_HANDLE;
		}

		//清空记录并复位
		records.clear();
		memory = nullptr;
		device = VK_NULL_HANDLE;
	}

	//是否已构建完成
	bool Texture_Context::valid(void) const
	{
		return memory != nullptr && device != VK_NULL_HANDLE;
	}

	//从内存像素创建纹理
	bool Texture_Context::texture_create_from_memory(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：内存模块与逻辑设备必须就绪
		if (memory == nullptr || !memory->valid() || device == VK_NULL_HANDLE)
		{
			logger.error("纹理创建:: 纹理上下文尚未构建，无法创建纹理");
			return false;
		}
		//前置：像素与尺寸必须有效
		if (pixels == nullptr || width == 0 || height == 0)
		{
			logger.error("纹理创建:: 像素指针为空或尺寸为零，无法创建纹理");
			return false;
		}

		//创建图像（sRGB RGBA8；采样 + 传输目标）
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (!memory->image_create(image, allocation, width, height, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			return false;

		//上传像素（RGBA8 紧排；row_pixels 传 0 表按 width 紧排）
		VkDeviceSize data_size = static_cast<VkDeviceSize>(width) * height * 4;
		if (!memory->upload_image(image, pixels, data_size, width, height, 0))
		{
			memory->image_destroy(image, allocation);
			return false;
		}

		//创建图像视图
		VkImageView view = VK_NULL_HANDLE;
		if (!memory->image_view_create(view, image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT))
		{
			memory->image_destroy(image, allocation);
			return false;
		}

		//取共享采样器（首次调用创建，之后复用）
		if (!sampler_create(shared_sampler))
		{
			memory->image_view_destroy(view);
			memory->image_destroy(image, allocation);
			return false;
		}

		//登记纹理记录
		Texture_Record record;
		record.image = image;
		record.allocation = allocation;
		record.view = view;
		record.sampler = shared_sampler;
		record.valid = true;
		records.push_back(record);

		//句柄自 1 起
		handle_out = static_cast<uint64_t>(records.size());
		logger.info("纹理创建:: 纹理已建立（句柄 {}，{}x{}）", handle_out, width, height);
		return true;
	}

	//程序生成棋盘格纹理
	bool Texture_Context::texture_create_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：尺寸与格子边长必须有效
		if (width == 0 || height == 0 || cell_size == 0)
		{
			logger.error("纹理创建:: 棋盘格尺寸或格子边长为零，无法生成");
			return false;
		}

		//像素缓冲（RGBA8 紧排）
		vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
		//逐像素填充两种灰度
		for (uint32_t y = 0; y < height; ++y)
			for (uint32_t x = 0; x < width; ++x)
			{
				//棋盘格判定（横纵格子索引之和为奇则取深色）
				bool odd = ((x / cell_size) + (y / cell_size)) % 2 == 1;
				//两种灰度（深 0x40 / 浅 0xC0）
				uint8_t value = odd ? 0x40 : 0xC0;
				//目标像素偏移
				size_t index = (static_cast<size_t>(y) * width + x) * 4;
				pixels[index + 0] = value;
				pixels[index + 1] = value;
				pixels[index + 2] = value;
				pixels[index + 3] = 0xFF;
			}

		//转交内存像素入口创建
		return texture_create_from_memory(handle_out, pixels.data(), width, height);
	}

	//销毁纹理
	void Texture_Context::texture_destroy(uint64_t handle)
	{
		//句柄为零或越界即视为无需销毁（销毁幂等）
		if (handle == 0 || handle > records.size())
			return;
		//获取目标记录
		Texture_Record& record = records[handle - 1];
		//若记录已无效
		if (!record.valid)
			return;
		//内存模块就绪时归还图像视图与图像（采样器为模块共享，不随单条纹理销毁）
		if (memory != nullptr)
		{
			//归还图像视图
			memory->image_view_destroy(record.view);
			//归还图像与它的内存
			memory->image_destroy(record.image, record.allocation);
		}
		//复位记录
		record = Texture_Record{};
	}

	//取图像视图与采样器
	bool Texture_Context::texture_bind_get(uint64_t handle, VkImageView& view_out, VkSampler& sampler_out) const
	{
		//默认输出为空
		view_out = VK_NULL_HANDLE;
		sampler_out = VK_NULL_HANDLE;
		//若句柄为零或越界
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		const Texture_Record& record = records[handle - 1];
		//若记录无效
		if (!record.valid)
			return false;
		//写出视图与采样器
		view_out = record.view;
		sampler_out = record.sampler;
		return true;
	}
}
