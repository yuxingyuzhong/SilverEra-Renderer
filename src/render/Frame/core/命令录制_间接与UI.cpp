#include "命令录制_内部.h"

namespace engine
{
	void Frame_Context::indirect_context_set(void* mapped, VkBuffer buffer, uint32_t capacity, uint32_t instance_stride)
	{
		//关闭条件
		if (mapped == nullptr || buffer == VK_NULL_HANDLE || capacity <= 1 || instance_stride == 0)
		{
			indirect_mapped = nullptr;
			indirect_buffer = VK_NULL_HANDLE;
			indirect_capacity = 0;
			return;
		}
		indirect_mapped = mapped;
		indirect_buffer = buffer;
		indirect_capacity = capacity;
		indirect_instance_stride = instance_stride;
	}

	uint32_t Frame_Context::indirect_command_count_get(void) const
	{
		return frame_indirect_commands;
	}

	uint64_t Frame_Context::indirect_draw_count_get(void) const
	{
		return total_indirect_draws;
	}

	bool Frame_Context::slot_indirect_buffer_set(uint32_t slot_index, VkBuffer buffer, void* mapped, uint32_t capacity)
	{
		//槽序号越界
		if (slot_index >= slots.size())
			return false;
		slots[slot_index].indirect_buffer = buffer;
		slots[slot_index].indirect_mapped = mapped;
		slots[slot_index].indirect_capacity = buffer == VK_NULL_HANDLE ? 0 : capacity;
		return true;
	}

	void Frame_Context::ui_pass_record(VkCommandBuffer command_buffer, VkImage color_image, VkImageView color_view,
		VkExtent2D extent, VkViewport viewport, VkRect2D scissor, const Frame_Ui_Callback* ui_callback)
	{
		//解析写入 → 下一通道读取 / 写入的依赖屏障（同布局）
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = color_image;
		barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		vkCmdPipelineBarrier(command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		//UI 颜色附件（载入解析结果）
		VkRenderingAttachmentInfo ui_color{};
		ui_color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		ui_color.imageView = color_view;
		ui_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		ui_color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		ui_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		//UI 渲染信息（无深度附件）
		VkRenderingInfo ui_rendering{};
		ui_rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		ui_rendering.renderArea.offset = { 0, 0 };
		ui_rendering.renderArea.extent = extent;
		ui_rendering.layerCount = 1;
		ui_rendering.colorAttachmentCount = 1;
		ui_rendering.pColorAttachments = &ui_color;

		//进入 UI 通道并恢复视口 / 裁剪（上层 UI 后端按当前动态状态绘制）
		vkCmdBeginRendering(command_buffer, &ui_rendering);
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);
		ui_callback->fn(command_buffer, ui_callback->user_data);
		vkCmdEndRendering(command_buffer);
	}
}
