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

	//当前有效纹理数量（第一百一十三轮）
	uint32_t Texture_Context::texture_count(void) const
	{
		//逐条统计有效记录
		uint32_t count = 0;
		for (const Texture_Record& record : records)
			if (record.valid)
				++count;
		return count;
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

		/*
		mip 级数 = floor(log2(max(宽, 高))) + 1
		生成完整 mip 链，使缩小绘制时能采到低级别（是否真正启用由采样器状态的 mip_filter 决定）。
		*/
		uint32_t mip_levels = 1;
		uint32_t largest_side = width > height ? width : height;
		while (largest_side > 1)
		{
			largest_side >>= 1;
			++mip_levels;
		}

		//创建图像（sRGB RGBA8；采样 + 传输目标 + 传输源（mip 生成需作为源），级数 = mip 级数）
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (!memory->image_create(image, allocation, width, height, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			mip_levels))
			return false;

		//上传像素（RGBA8 紧排；row_pixels 传 0 表按 width 紧排）
		VkDeviceSize data_size = static_cast<VkDeviceSize>(width) * height * 4;
		if (!memory->upload_image(image, pixels, data_size, width, height, 0))
		{
			memory->image_destroy(image, allocation);
			return false;
		}

		//生成 mip 链（第 0 级已写入且处于着色器可读布局）
		if (!memory->image_generate_mips(image, width, height, mip_levels))
		{
			logger.error("纹理创建:: mip 链生成失败，回退");
			memory->image_destroy(image, allocation);
			return false;
		}

		//创建图像视图（须覆盖全部 mip 级，否则采样器无法选取低级别）
		VkImageView view = VK_NULL_HANDLE;
		if (!memory->image_view_create(view, image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mip_levels))
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
		record.mip_levels = mip_levels;
		record.width = width;
		record.height = height;
		record.valid = true;
		records.push_back(record);

		//句柄自 1 起
		handle_out = static_cast<uint64_t>(records.size());
		logger.info("纹理创建:: 纹理已建立（句柄 {}，{}x{}，mip 级数 {}）", handle_out, width, height, mip_levels);
		return true;
	}

	//从内存像素创建立方体贴图（第一百二十五轮）
	bool Texture_Context::texture_create_cube(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：内存模块、逻辑设备、像素与尺寸必须有效
		if (memory == nullptr || !memory->valid() || device == VK_NULL_HANDLE)
		{
			logger.error("纹理创建:: 纹理上下文尚未构建，无法创建立方体贴图");
			return false;
		}
		if (six_faces == nullptr || width == 0 || height == 0)
		{
			logger.error("纹理创建:: 立方体贴图的像素指针为空或尺寸为零");
			return false;
		}

		/*
		创建六层立方体图像
		单 mip；usage 需含采样与传输目标；flags 必须置 CUBE_COMPATIBLE 才允许立方体视图。
		*/
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (!memory->image_create(image, allocation, width, height, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1,
			VK_SAMPLE_COUNT_1_BIT, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))
			return false;

		//逐面上传（每面 RGBA8 紧排）
		VkDeviceSize face_size = static_cast<VkDeviceSize>(width) * height * 4;
		for (uint32_t face = 0; face < 6; ++face)
		{
			if (!memory->upload_image(image, six_faces + face_size * face, face_size, width, height, 0, face))
			{
				logger.error("纹理创建:: 立方体贴图第 {} 面上传失败，回退", face);
				memory->image_destroy(image, allocation);
				return false;
			}
		}

		//创建立方体视图（覆盖六层）
		VkImageView view = VK_NULL_HANDLE;
		if (!memory->image_view_create(view, image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT,
			1, VK_IMAGE_VIEW_TYPE_CUBE, 6))
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

		//登记纹理记录（mip 级数恒 1；尺寸按单面计）
		Texture_Record record;
		record.image = image;
		record.allocation = allocation;
		record.view = view;
		record.sampler = shared_sampler;
		record.mip_levels = 1;
		record.width = width;
		record.height = height;
		record.valid = true;
		records.push_back(record);

		//句柄自 1 起
		handle_out = static_cast<uint64_t>(records.size());
		logger.info("纹理创建:: 立方体贴图已建立（句柄 {}，{}x{}，6 面）", handle_out, width, height);
		return true;
	}

	//程序生成棋盘格纹理
	//取纹理尺寸（只读；句柄无效返回假且出参归零）
	bool Texture_Context::texture_size_get(uint64_t handle, uint32_t& width_out, uint32_t& height_out) const
	{
		//出参先复位
		width_out = 0;
		height_out = 0;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取纹理记录
		const Texture_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出尺寸
		width_out = record.width;
		height_out = record.height;
		return true;
	}

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

	//分离纹理资源（摘出记录不销毁，归还责任移交调用方）
	bool Texture_Context::texture_detach(uint64_t handle, Texture_Record& record_out)
	{
		//默认无资源摘出
		record_out = Texture_Record{};
		//句柄为零或越界即视为无需分离
		if (handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		Texture_Record& record = records[handle - 1];
		//若记录已无效
		if (!record.valid)
			return false;
		//写出被摘出的记录（阻塞图像、内存与视图）
		record_out = record;
		//复位原记录（归还责任已移交调用方）
		record = Texture_Record{};
		return true;
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

	//读回纹理第 0 级像素
	bool Texture_Context::texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若内存模块未就绪 / 出参为空 / 句柄越界
		if (memory == nullptr || pixels == nullptr || handle == 0 || handle > records.size())
			return false;
		//获取目标记录
		const Texture_Record& record = records[handle - 1];
		//若记录无效或尺寸缺失
		if (!record.valid || record.width == 0 || record.height == 0)
			return false;
		//本幅字节数（RGBA8 紧排）
		VkDeviceSize byte_size = static_cast<VkDeviceSize>(record.width) * record.height * 4;
		//若容量不足
		if (byte_size > capacity)
		{
			logger.error("纹理创建:: 读回容量不足（需要 {} 字节，给出 {}）", byte_size, capacity);
			return false;
		}
		//转交内存模块同步读回（布局往返由内存模块完成）
		if (!memory->image_readback(record.image, record.width, record.height, pixels, byte_size))
			return false;
		//写出尺寸
		if (width_out != nullptr)
			*width_out = record.width;
		if (height_out != nullptr)
			*height_out = record.height;
		return true;
	}

	//读回纹理第 0 级的指定子区域
	bool Texture_Context::texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若内存模块未就绪 / 出参为空 / 句柄越界 / 区域为空
		if (memory == nullptr || pixels == nullptr || handle == 0 || handle > records.size()
			|| region_width == 0 || region_height == 0)
			return false;
		//获取目标记录
		const Texture_Record& record = records[handle - 1];
		//若记录无效 / 尺寸缺失 / 区域越界
		if (!record.valid || record.width == 0 || record.height == 0
			|| x >= record.width || y >= record.height
			|| region_width > record.width - x || region_height > record.height - y)
			return false;
		//区域字节数（RGBA8 紧排）
		VkDeviceSize byte_size = static_cast<VkDeviceSize>(region_width) * region_height * 4;
		//若容量不足
		if (byte_size > capacity)
		{
			logger.error("纹理创建:: 区域读回容量不足（需要 {} 字节，给出 {}）", byte_size, capacity);
			return false;
		}
		//转交内存模块（子区域同步读回）
		if (!memory->image_readback_region(record.image, x, y, region_width, region_height, pixels, byte_size))
			return false;
		//写出区域尺寸
		if (width_out != nullptr)
			*width_out = region_width;
		if (height_out != nullptr)
			*height_out = region_height;
		return true;
	}

	//更新纹理第 0 级的子区域（第一百五十一轮；第一百五十八轮委托分级入口）
	bool Texture_Context::texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//第 0 级即分级入口的 level = 0
		return texture_update_region_level(handle, 0, x, y, width, height, pixels);
	}

	//更新纹理指定级的子区域（第一百五十八轮）
	bool Texture_Context::texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//句柄 0 无效
		if (handle == 0 || handle > records.size())
			return false;
		Texture_Record& record = records[handle - 1];
		//若记录无效 / 尺寸缺失 / 级号越界 / 源数据为空
		if (!record.valid || record.width == 0 || record.height == 0 || pixels == nullptr
			|| level >= record.mip_levels)
			return false;
		//该级尺寸（每降一级取半，最小 1）
		const uint32_t level_width = (record.width >> level) > 0 ? record.width >> level : 1;
		const uint32_t level_height = (record.height >> level) > 0 ? record.height >> level : 1;
		//若区域不完整落在该级内则拒绝
		if (x >= level_width || y >= level_height || width == 0 || height == 0
			|| width > level_width - x || height > level_height - y)
			return false;
		//转交内存模块（子区域同步上传；前后保持着色器可读布局）
		if (!memory->upload_image_region(record.image, pixels, x, y, width, height, level))
			return false;
		//重建该级的下游降采样级，避免低级别仍是旧内容
		if (level + 1 < record.mip_levels)
			return memory->image_generate_mips_from(record.image, level, level_width, level_height,
				record.mip_levels);
		return true;
	}

	//查询指定级尺寸（第一百六十一轮）
	bool Texture_Context::texture_size_level(uint64_t handle, uint32_t level,
		uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 0 / 越界 / 记录无效 / 级号越界均拒绝
		if (handle == 0 || handle > records.size())
			return false;
		const Texture_Record& record = records[handle - 1];
		if (!record.valid || record.width == 0 || record.height == 0 || level >= record.mip_levels)
			return false;
		//该级尺寸（每降一级取半，最小 1）
		const uint32_t level_width = (record.width >> level) > 0 ? record.width >> level : 1;
		const uint32_t level_height = (record.height >> level) > 0 ? record.height >> level : 1;
		//写出尺寸
		if (width_out != nullptr)
			*width_out = level_width;
		if (height_out != nullptr)
			*height_out = level_height;
		return true;
	}

	//读回指定级整幅像素（第一百五十八轮）
	bool Texture_Context::texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels,
		uint32_t capacity, uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//句柄 0 无效
		if (handle == 0 || handle > records.size())
			return false;
		//记录无效 / 级号越界则拒绝
		const Texture_Record& record = records[handle - 1];
		if (!record.valid || record.width == 0 || record.height == 0 || level >= record.mip_levels)
			return false;
		//该级尺寸（每降一级取半，最小 1）
		const uint32_t level_width = (record.width >> level) > 0 ? record.width >> level : 1;
		const uint32_t level_height = (record.height >> level) > 0 ? record.height >> level : 1;
		//区域字节数（RGBA8 紧排）
		const VkDeviceSize byte_size = static_cast<VkDeviceSize>(level_width) * level_height * 4;
		//若容量不足或输出缓冲为空
		if (byte_size > capacity || pixels == nullptr)
			return false;
		//转交内存模块（该级整幅同步读回）
		if (!memory->image_readback_region(record.image, 0, 0, level_width, level_height, pixels,
				byte_size, level))
			return false;
		//写出该级尺寸
		if (width_out != nullptr)
			*width_out = level_width;
		if (height_out != nullptr)
			*height_out = level_height;
		return true;
	}
}
