#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//内存上下文完整定义（图像 / 视图创建与销毁）
#include "src/render/Memory/内存上下文.h"

namespace engine
{
	//析构

	void Render_Target_Context::target_destroy(uint64_t handle)
	{
		//句柄无效则忽略
		if (handle == 0 || handle > records.size())
			return;
		//取目标记录
		Render_Target_Record& record = records[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//归还图像与视图
		record_resources_destroy(record);
		//整条记录复位（标记槽为空）
		record = Render_Target_Record{};
		logger.info("渲染目标:: 已销毁（句柄 {}）", handle);
	}

	bool Render_Target_Context::target_detach(uint64_t handle, Render_Target_Record& record_out)
	{
		//默认无资源摘出
		record_out = Render_Target_Record{};
		//句柄为零或越界即视为无需分离
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出被摘出的记录（阻塞颜色 / 深度图像与视图）
		record_out = record;
		//整条记录复位（标记槽为空；归还责任已移交调用方）
		record = Render_Target_Record{};
		return true;
	}

	bool Render_Target_Context::target_get(uint64_t handle, VkImage& color_image_out, VkImageView& color_view_out,
		VkImageView& depth_view_out, VkExtent2D& extent_out) const
	{
		//出参先复位
		color_image_out = VK_NULL_HANDLE;
		color_view_out = VK_NULL_HANDLE;
		depth_view_out = VK_NULL_HANDLE;
		extent_out = { 0, 0 };
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出各附件与范围
		color_image_out = record.color_image;
		color_view_out = record.color_view;
		depth_view_out = record.depth_view;
		extent_out = record.extent;
		return true;
	}

	bool Render_Target_Context::target_depth_get(uint64_t handle, VkImage& depth_image_out, VkImageView& depth_view_out) const
	{
		//出参先复位
		depth_image_out = VK_NULL_HANDLE;
		depth_view_out = VK_NULL_HANDLE;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出深度图像与视图
		depth_image_out = record.depth_image;
		depth_view_out = record.depth_view;
		return true;
	}

	bool Render_Target_Context::target_extent_get(uint64_t handle, VkExtent2D& extent_out) const
	{
		//出参先复位
		extent_out = { 0, 0 };
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出范围
		extent_out = record.extent;
		return true;
	}

	bool Render_Target_Context::target_valid(uint64_t handle) const
	{
		//越界或空槽均视为无效
		if (handle == 0 || handle > records.size())
			return false;
		return records[handle - 1].valid;
	}

	//取目标颜色 / 深度格式
	bool Render_Target_Context::target_format_get(uint64_t handle, VkFormat& color_format_out, VkFormat& depth_format_out) const
	{
		//出参先复位
		color_format_out = VK_FORMAT_UNDEFINED;
		depth_format_out = VK_FORMAT_UNDEFINED;
		//句柄无效则返回假
		if (handle == 0 || handle > records.size())
			return false;
		//取目标记录
		const Render_Target_Record& record = records[handle - 1];
		//已空则返回假
		if (!record.valid)
			return false;
		//写出格式
		color_format_out = record.color_format;
		depth_format_out = record.depth_format;
		return true;
	}

	void Render_Target_Context::destroy(void)
	{
		//逐条归还渲染目标
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			//跳过空槽
			if (!records[index].valid)
				continue;
			record_resources_destroy(records[index]);
			records[index] = Render_Target_Record{};
		}
		records.clear();

		//归还一次性资源
		if (device != VK_NULL_HANDLE)
		{
			//先栅栏后命令池
			if (transient_fence != VK_NULL_HANDLE)
				vkDestroyFence(device, transient_fence, nullptr);
			if (transient_pool != VK_NULL_HANDLE)
				vkDestroyCommandPool(device, transient_pool, nullptr);
		}
		transient_fence = VK_NULL_HANDLE;
		transient_pool = VK_NULL_HANDLE;
		memory = nullptr;
		graphics_queue = VK_NULL_HANDLE;
		device = VK_NULL_HANDLE;
		graphics_family = 0;
	}
}
