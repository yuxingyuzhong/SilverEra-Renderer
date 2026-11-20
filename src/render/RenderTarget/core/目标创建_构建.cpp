#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像 / 视图创建与销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构

	Render_Target_Context::~Render_Target_Context(void)
	{
		//释放全部渲染目标与一次性资源
		destroy();
	}

	bool Render_Target_Context::build(VkDevice device, uint32_t graphics_family, VkQueue graphics_queue, Memory_Context& memory)
	{
		//前置：设备 / 队列 / 内存模块必须齐备
		if (device == VK_NULL_HANDLE || graphics_queue == VK_NULL_HANDLE || !memory.valid())
		{
			logger.error("渲染目标:: 构建前置不完整（设备 / 队列 / 内存模块）");
			return false;
		}
		//记录句柄与内存模块
		this->device = device;
		this->graphics_family = graphics_family;
		this->graphics_queue = graphics_queue;
		this->memory = &memory;
		logger.info("渲染目标:: 上下文构建完成");
		return true;
	}

	bool Render_Target_Context::valid(void) const
	{
		return device != VK_NULL_HANDLE && memory != nullptr;
	}

	uint32_t Render_Target_Context::target_count(void) const
	{
		//逐条统计有效记录
		uint32_t count = 0;
		for (const Render_Target_Record& record : records)
			if (record.valid)
				++count;
		return count;
	}

	bool Render_Target_Context::transient_resources_ensure(void)
	{
		//均已就绪则直接复用
		if (transient_pool != VK_NULL_HANDLE && transient_fence != VK_NULL_HANDLE)
			return true;

		//创建一次性命令池（TRANSIENT，跨多次转换经 vkResetCommandPool 复用）
		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_info.queueFamilyIndex = graphics_family;
		//若命令池创建失败
		if (!vk_result_check(vkCreateCommandPool(device, &pool_info, nullptr, &transient_pool), "渲染目标:: 创建一次性命令池"))
		{
			transient_pool = VK_NULL_HANDLE;
			return false;
		}

		//创建完成栅栏（未触发态，供等待本次提交用）
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		//若栅栏创建失败
		if (!vk_result_check(vkCreateFence(device, &fence_info, nullptr, &transient_fence), "渲染目标:: 创建一次性栅栏"))
		{
			vkDestroyCommandPool(device, transient_pool, nullptr);
			transient_pool = VK_NULL_HANDLE;
			transient_fence = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	bool Render_Target_Context::target_create(uint64_t& handle_out, uint32_t width, uint32_t height,
		VkFormat color_format, VkFormat depth_format, uint32_t sample_count, VkFormat tone_id_format)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：上下文与参数校验
		if (!valid() || width == 0 || height == 0
			|| color_format == VK_FORMAT_UNDEFINED || depth_format == VK_FORMAT_UNDEFINED)
		{
			logger.error("渲染目标:: 创建参数不完整（{}×{}，颜色 {}，深度 {}）", width, height,
				static_cast<int32_t>(color_format), static_cast<int32_t>(depth_format));
			return false;
		}
		//确保一次性转换资源就绪
		if (!transient_resources_ensure())
			return false;

		/*
		采样数解析（第九十六轮）
		0 = 跟随上下文默认采样数（follow = true）；非 0 = 固定值（follow = false，如阴影目标）。
		*/
		Render_Target_Record record{};
		record.extent = { width, height };
		record.color_format = color_format;
		record.depth_format = depth_format;
		record.follow_sample_count = (sample_count == 0);
		record.sample_count = sample_count == 0 ? this->sample_count : sample_count;
		//固定采样数兜底（仅接受 2 / 4 / 8，其余按 1）
		if (record.sample_count != 2 && record.sample_count != 4 && record.sample_count != 8)
			record.sample_count = 1;
		//创建颜色图像（颜色附件 ＋ 采样 ＋ 传输源）
		if (!memory->image_create(record.color_image, record.color_alloc, width, height, color_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
		{
			logger.error("渲染目标:: 颜色图像创建失败");
			return false;
		}
		//创建颜色图像视图
		if (!memory->image_view_create(record.color_view, record.color_image, color_format, VK_IMAGE_ASPECT_COLOR_BIT))
		{
			logger.error("渲染目标:: 颜色图像视图创建失败");
			record_resources_destroy(record);
			return false;
		}
		//创建深度图像（深度附件 ＋ 传输源；第二十八轮增加传输源以支持深度读回；第一百二十三轮增加采样以支持景深）
		if (!memory->image_create(record.depth_image, record.depth_alloc, width, height, depth_format,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
		{
			logger.error("渲染目标:: 深度图像创建失败");
			record_resources_destroy(record);
			return false;
		}
		//创建深度图像视图
		if (!memory->image_view_create(record.depth_view, record.depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
		{
			logger.error("渲染目标:: 深度图像视图创建失败");
			record_resources_destroy(record);
			return false;
		}
		//创建 MSAA 附件（第九十六轮；采样数为 1 时为空操作）
		if (!record_msaa_create(record))
		{
			logger.error("渲染目标:: MSAA 附件创建失败");
			record_resources_destroy(record);
			return false;
		}
		/*
		逐材质色调编号附件（第 801 轮 · MRT 段 1；第 821 轮支持多重采样）
		单采样图始终创建（作解析目标 / 读回源）；采样数 > 1 时再建同格式的多重采样图，
		编号先写入多重采样图、pass 结束时按「样本 0」解析到单采样图。
		*/
		if (tone_id_format != VK_FORMAT_UNDEFINED)
		{
			if (!memory->image_create(record.tone_id_image, record.tone_id_alloc, width, height,
				tone_id_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
					| VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
			{
				logger.error("渲染目标:: 色调编号附件创建失败");
				record_resources_destroy(record);
				return false;
			}
			if (!memory->image_view_create(record.tone_id_view, record.tone_id_image, tone_id_format,
				VK_IMAGE_ASPECT_COLOR_BIT))
			{
				logger.error("渲染目标:: 色调编号附件视图创建失败");
				record_resources_destroy(record);
				return false;
			}
			//多重采样编号图（采样数 > 1）
			if (record.sample_count > 1)
			{
				if (!memory->image_create(record.msaa_tone_id_image, record.msaa_tone_id_alloc, width, height,
					tone_id_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1,
					static_cast<VkSampleCountFlagBits>(record.sample_count)))
				{
					logger.error("渲染目标:: 多重采样色调编号附件创建失败");
					record_resources_destroy(record);
					return false;
				}
				if (!memory->image_view_create(record.msaa_tone_id_view, record.msaa_tone_id_image, tone_id_format,
					VK_IMAGE_ASPECT_COLOR_BIT))
				{
					logger.error("渲染目标:: 多重采样色调编号附件视图创建失败");
					record_resources_destroy(record);
					return false;
				}
			}
			record.tone_id_format = tone_id_format;
		}
		//一次性布局转换（颜色 → 着色器可读；深度 → 深度附件；MSAA 附件 → 对应附件布局）
		if (!target_transition(record))
		{
			logger.error("渲染目标:: 初始布局转换失败");
			record_resources_destroy(record);
			return false;
		}
		//标记记录有效
		record.valid = true;

		//分配句柄：优先复用第一个空槽，否则追加
		uint64_t handle = 0;
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			//若该槽已空
			if (!records[index].valid)
			{
				records[index] = record;
				handle = static_cast<uint64_t>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			records.push_back(record);
			handle = static_cast<uint64_t>(records.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染目标:: 已创建（句柄 {}，{}×{}，采样数 {}）", handle, width, height, record.sample_count);
		return true;
	}

	bool Render_Target_Context::record_msaa_create(Render_Target_Record& record)
	{
		//关闭 MSAA 时无附件可建
		if (record.sample_count <= 1)
			return true;
		//前置：内存模块必须就绪
		if (memory == nullptr || !memory->valid())
		{
			logger.error("渲染目标:: 内存上下文不可用，无法创建 MSAA 附件");
			return false;
		}
		//采样标志
		VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(record.sample_count);
		//MSAA 颜色附件（仅颜色附件用途；解析到单采样颜色图像）
		if (!memory->image_create(record.msaa_color_image, record.msaa_color_alloc,
			record.extent.width, record.extent.height, record.color_format,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, samples))
			return false;
		//MSAA 颜色视图
		if (!memory->image_view_create(record.msaa_color_view, record.msaa_color_image,
			record.color_format, VK_IMAGE_ASPECT_COLOR_BIT))
			return false;
		//MSAA 深度附件
		if (!memory->image_create(record.msaa_depth_image, record.msaa_depth_alloc,
			record.extent.width, record.extent.height, record.depth_format,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1, samples))
			return false;
		//MSAA 深度视图
		if (!memory->image_view_create(record.msaa_depth_view, record.msaa_depth_image,
			record.depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
			return false;
		/*
		多重采样编号图（第 822 轮；采样数切换时随 MSAA 附件一起重建）
		编号格式启用且尚未创建时补建，避免采样数变化后指向已销毁图像。
		*/
		if (record.tone_id_format != VK_FORMAT_UNDEFINED && record.msaa_tone_id_image == VK_NULL_HANDLE)
		{
			if (!memory->image_create(record.msaa_tone_id_image, record.msaa_tone_id_alloc,
				record.extent.width, record.extent.height, record.tone_id_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, samples))
				return false;
			if (!memory->image_view_create(record.msaa_tone_id_view, record.msaa_tone_id_image,
				record.tone_id_format, VK_IMAGE_ASPECT_COLOR_BIT))
				return false;
		}
		return true;
	}

	void Render_Target_Context::record_msaa_destroy(Render_Target_Record& record)
	{
		//内存模块就绪时归还图像与视图
		if (memory != nullptr)
		{
			//多重采样编号附件（第 822 轮）
			if (record.msaa_tone_id_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.msaa_tone_id_view);
			if (record.msaa_tone_id_image != VK_NULL_HANDLE)
				memory->image_destroy(record.msaa_tone_id_image, record.msaa_tone_id_alloc);
			if (record.msaa_depth_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.msaa_depth_view);
			if (record.msaa_depth_image != VK_NULL_HANDLE)
				memory->image_destroy(record.msaa_depth_image, record.msaa_depth_alloc);
			if (record.msaa_color_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.msaa_color_view);
			if (record.msaa_color_image != VK_NULL_HANDLE)
				memory->image_destroy(record.msaa_color_image, record.msaa_color_alloc);
		}
		record.msaa_color_image = VK_NULL_HANDLE;
		record.msaa_color_alloc = VK_NULL_HANDLE;
		record.msaa_color_view = VK_NULL_HANDLE;
		record.msaa_depth_image = VK_NULL_HANDLE;
		record.msaa_depth_alloc = VK_NULL_HANDLE;
		record.msaa_depth_view = VK_NULL_HANDLE;
		record.msaa_tone_id_image = VK_NULL_HANDLE;
		record.msaa_tone_id_alloc = VK_NULL_HANDLE;
		record.msaa_tone_id_view = VK_NULL_HANDLE;
	}

	bool Render_Target_Context::sample_count_set(uint32_t count)
	{
		//钳制采样数（仅接受 2 / 4 / 8，其余按 1）
		if (count != 2 && count != 4 && count != 8)
			count = 1;
		//未变化则空操作
		if (count == sample_count)
			return true;
		sample_count = count;
		//逐条重建跟随采样数的目标
		for (Render_Target_Record& record : records)
		{
			//跳过空槽与固定采样数目标
			if (!record.valid || !record.follow_sample_count)
				continue;
			//保存重建参数
			uint32_t width = record.extent.width;
			uint32_t height = record.extent.height;
			VkFormat color_format = record.color_format;
			VkFormat depth_format = record.depth_format;
			//销毁旧资源（含 MSAA）
			record_resources_destroy(record);
			//复位资源句柄与采样数（保留 extent / 格式 / 跟随标记）
			record.sample_count = count;
			//重建颜色图像
			if (!memory->image_create(record.color_image, record.color_alloc, width, height, color_format,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
			{
				logger.error("渲染目标:: 采样数切换时颜色图像重建失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
			//重建颜色视图
			if (!memory->image_view_create(record.color_view, record.color_image, color_format, VK_IMAGE_ASPECT_COLOR_BIT))
			{
				logger.error("渲染目标:: 采样数切换时颜色视图重建失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
			//重建深度图像（第一百二十三轮起同时保留采样用途）
			if (!memory->image_create(record.depth_image, record.depth_alloc, width, height, depth_format,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
			{
				logger.error("渲染目标:: 采样数切换时深度图像重建失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
			//重建深度视图
			if (!memory->image_view_create(record.depth_view, record.depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT))
			{
				logger.error("渲染目标:: 采样数切换时深度视图重建失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
			//重建 MSAA 附件
			if (!record_msaa_create(record))
			{
				logger.error("渲染目标:: 采样数切换时 MSAA 附件重建失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
			//重建后一次性布局转换
			if (!target_transition(record))
			{
				logger.error("渲染目标:: 采样数切换时布局转换失败");
				record.valid = false;
				record_resources_destroy(record);
				return false;
			}
		}
		return true;
	}

	uint32_t Render_Target_Context::sample_count_get(void) const
	{
		return sample_count;
	}

	bool Render_Target_Context::target_msaa_get(uint64_t handle, VkImage& msaa_color_image_out,
		VkImageView& msaa_color_view_out, VkImageView& msaa_depth_view_out, uint32_t& sample_count_out) const
	{
		//出参先复位
		msaa_color_image_out = VK_NULL_HANDLE;
		msaa_color_view_out = VK_NULL_HANDLE;
		msaa_depth_view_out = VK_NULL_HANDLE;
		sample_count_out = 1;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出 MSAA 图像 / 视图与采样数
		msaa_color_image_out = record.msaa_color_image;
		msaa_color_view_out = record.msaa_color_view;
		msaa_depth_view_out = record.msaa_depth_view;
		sample_count_out = record.sample_count;
		return true;
	}

	bool Render_Target_Context::target_msaa_depth_image_get(uint64_t handle, VkImage& msaa_depth_image_out) const
	{
		//出参先复位
		msaa_depth_image_out = VK_NULL_HANDLE;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出 MSAA 深度图像（关闭 MSAA 时为空）
		msaa_depth_image_out = record.msaa_depth_image;
		return true;
	}

	void Render_Target_Context::record_resources_destroy(Render_Target_Record& record)
	{
		//先归还 MSAA 附件（第九十六轮）
		record_msaa_destroy(record);
		//内存模块就绪时逐个归还
		if (memory != nullptr)
		{
			//多重采样编号附件视图 / 图像（第 821 轮）
			if (record.msaa_tone_id_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.msaa_tone_id_view);
			if (record.msaa_tone_id_image != VK_NULL_HANDLE)
				memory->image_destroy(record.msaa_tone_id_image, record.msaa_tone_id_alloc);
			//色调编号附件视图 / 图像（第 801 轮）
			if (record.tone_id_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.tone_id_view);
			if (record.tone_id_image != VK_NULL_HANDLE)
				memory->image_destroy(record.tone_id_image, record.tone_id_alloc);
			//深度视图 / 深度图像
			if (record.depth_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.depth_view);
			if (record.depth_image != VK_NULL_HANDLE)
				memory->image_destroy(record.depth_image, record.depth_alloc);
			//颜色视图 / 颜色图像
			if (record.color_view != VK_NULL_HANDLE)
				memory->image_view_destroy(record.color_view);
			if (record.color_image != VK_NULL_HANDLE)
				memory->image_destroy(record.color_image, record.color_alloc);
		}
		//复位句柄（保留 extent 由调用方决定是否清空）
		record.tone_id_image = VK_NULL_HANDLE;
		record.tone_id_alloc = VK_NULL_HANDLE;
		record.tone_id_view = VK_NULL_HANDLE;
		record.tone_id_format = VK_FORMAT_UNDEFINED;
		record.color_image = VK_NULL_HANDLE;
		record.color_alloc = VK_NULL_HANDLE;
		record.color_view = VK_NULL_HANDLE;
		record.depth_image = VK_NULL_HANDLE;
		record.depth_alloc = VK_NULL_HANDLE;
		record.depth_view = VK_NULL_HANDLE;
	}
}
