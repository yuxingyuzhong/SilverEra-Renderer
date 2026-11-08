#include "../局部命名空间使用.h"
//UI 渲染器声明
#include "Engine/EngineRenderer/src/render/UI/UI渲染器.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//析构（归还每帧槽缓冲）
	Ui_Renderer::~Ui_Renderer(void)
	{
		shutdown();
	}

	//绑定依赖模块与 UI 管线句柄
	bool Ui_Renderer::build(VkDevice device, Memory_Context& memory, Pipeline_Context& pipeline,
		Texture_Context& texture, Font_Context& font,
		uint64_t solid_pipeline, uint64_t text_pipeline, uint32_t frame_overlap)
	{
		//前置：设备、内存与管线模块必须齐备
		if (device == VK_NULL_HANDLE || !memory.valid() || !pipeline.valid())
		{
			logger.error("UI 渲染器:: 构建前置不完整");
			return false;
		}
		//前置：在途帧数为正
		if (frame_overlap == 0)
		{
			logger.error("UI 渲染器:: 在途帧数必须为正");
			return false;
		}

		//记录依赖与管线句柄
		this->device = device;
		this->memory = &memory;
		this->pipeline = &pipeline;
		this->texture = &texture;
		this->font = &font;
		this->solid_pipeline = solid_pipeline;
		this->text_pipeline = text_pipeline;
		//按帧槽预留缓冲记录
		slots.assign(frame_overlap, Slot_Buffers{});
		return true;
	}

	//销毁（幂等）
	void Ui_Renderer::shutdown(void)
	{
		//逐帧槽归还缓冲
		for (Slot_Buffers& slot : slots)
		{
			if (memory != nullptr)
			{
				if (slot.vertex_buffer != VK_NULL_HANDLE)
					memory->buffer_destroy(slot.vertex_buffer, slot.vertex_allocation);
				if (slot.index_buffer != VK_NULL_HANDLE)
					memory->buffer_destroy(slot.index_buffer, slot.index_allocation);
			}
			slot = Slot_Buffers{};
		}
		//清空记录与依赖
		slots.clear();
		cpu_vertices.clear();
		cpu_indices.clear();
		draws.clear();
		text_scratch = Mesh_Data{};
		text_atlas = 0;
		device = VK_NULL_HANDLE;
		memory = nullptr;
		pipeline = nullptr;
		texture = nullptr;
		font = nullptr;
		solid_pipeline = 0;
		text_pipeline = 0;
	}

	//是否已构建完成
	bool Ui_Renderer::valid(void) const
	{
		return device != VK_NULL_HANDLE && memory != nullptr && pipeline != nullptr && !slots.empty();
	}

	//确保帧槽缓冲容量足够
	bool Ui_Renderer::slot_ensure(Slot_Buffers& slot, uint32_t vertex_count, uint32_t index_count)
	{
		//容量已足够则空操作
		if (slot.vertex_capacity >= vertex_count && slot.index_capacity >= index_count)
			return true;

		//目标容量（预留一倍余量，减少逐帧重建）
		const uint32_t target_vertices = vertex_count * 2 + 256;
		const uint32_t target_indices = index_count * 2 + 512;

		//重建前先归还旧缓冲
		if (slot.vertex_buffer != VK_NULL_HANDLE)
			memory->buffer_destroy(slot.vertex_buffer, slot.vertex_allocation);
		if (slot.index_buffer != VK_NULL_HANDLE)
			memory->buffer_destroy(slot.index_buffer, slot.index_allocation);
		slot = Slot_Buffers{};

		//创建顶点缓冲（主机可见且一致，供逐帧直接写入）
		const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(target_vertices) * sizeof(Vertex);
		if (!memory->buffer_create(slot.vertex_buffer, slot.vertex_allocation, vertex_bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, false,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			logger.error("UI 渲染器:: 顶点缓冲创建失败");
			return false;
		}
		//持久映射顶点缓冲
		if (!memory->buffer_map(slot.vertex_buffer, slot.vertex_allocation, slot.vertex_mapped))
		{
			logger.error("UI 渲染器:: 顶点缓冲映射失败");
			memory->buffer_destroy(slot.vertex_buffer, slot.vertex_allocation);
			slot = Slot_Buffers{};
			return false;
		}

		//创建索引缓冲
		const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(target_indices) * sizeof(uint32_t);
		if (!memory->buffer_create(slot.index_buffer, slot.index_allocation, index_bytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true, false,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			logger.error("UI 渲染器:: 索引缓冲创建失败");
			memory->buffer_destroy(slot.vertex_buffer, slot.vertex_allocation);
			slot = Slot_Buffers{};
			return false;
		}
		//持久映射索引缓冲
		if (!memory->buffer_map(slot.index_buffer, slot.index_allocation, slot.index_mapped))
		{
			logger.error("UI 渲染器:: 索引缓冲映射失败");
			memory->buffer_destroy(slot.vertex_buffer, slot.vertex_allocation);
			memory->buffer_destroy(slot.index_buffer, slot.index_allocation);
			slot = Slot_Buffers{};
			return false;
		}

		//记录容量
		slot.vertex_capacity = target_vertices;
		slot.index_capacity = target_indices;
		return true;
	}

	//追加一个矩形并登记纯色绘制
	void Ui_Renderer::quad_append(const Ui_Rect& rect, const Ui_Color& color)
	{
		//零面积矩形不产生几何
		if (rect.w <= 0.0f || rect.h <= 0.0f)
			return;
		//顶点基址与索引基址（两者可不同，first_index 必须取索引基址）
		const uint32_t base = static_cast<uint32_t>(cpu_vertices.size());
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());

		//四个角（像素坐标，z = 0；uv 对纯色无意义）
		const float x0 = rect.x;
		const float y0 = rect.y;
		const float x1 = rect.x + rect.w;
		const float y1 = rect.y + rect.h;

		Vertex vertex{};
		//左上
		vertex.position[0] = x0; vertex.position[1] = y0; vertex.position[2] = 0.0f;
		cpu_vertices.push_back(vertex);
		//右上
		vertex.position[0] = x1; vertex.position[1] = y0;
		cpu_vertices.push_back(vertex);
		//右下
		vertex.position[0] = x1; vertex.position[1] = y1;
		cpu_vertices.push_back(vertex);
		//左下
		vertex.position[0] = x0; vertex.position[1] = y1;
		cpu_vertices.push_back(vertex);

		//两个三角形
		cpu_indices.push_back(base + 0);
		cpu_indices.push_back(base + 1);
		cpu_indices.push_back(base + 2);
		cpu_indices.push_back(base + 0);
		cpu_indices.push_back(base + 2);
		cpu_indices.push_back(base + 3);

		//登记绘制记录
		Draw_Record record{};
		record.first_index = index_base;
		record.index_count = 6;
		record.color = color;
		record.text = false;
		record.clip = current_clip;
		draws.push_back(record);
	}

	//追加文本
	bool Ui_Renderer::text_append(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
		float center_area_width, const Ui_Color& color)
	{
		//字体未设置或字体模块缺失
		if (font_handle == 0 || font == nullptr)
			return false;
		//空文本不绘制
		if (text.empty())
			return false;

		//取可写字体记录
		Font_Record* record = font->font_get_mutable(font_handle);
		if (record == nullptr)
			return false;

		//惰性补烘所需字形（含中文）
		font->glyphs_ensure(*record, text);

		//生成文字几何（像素坐标，原点在文本左上角）
		text_scratch = Mesh_Data{};
		if (!Font_Context::text_geometry_build(text_scratch, *record, text))
			return false;

		//度量单行宽度用于居中
		uint32_t width = 0;
		uint32_t height = 0;
		Font_Context::text_measure(*record, text, width, height);

		//居中时按目标区宽度平移
		float offset_x = pixel_x;
		if (center_area_width > 0.0f)
			offset_x += (center_area_width - static_cast<float>(width)) * 0.5f;

		//写入顶点与索引（索引按当前顶点基址偏移，绘制记录取索引基址）
		const uint32_t base = static_cast<uint32_t>(cpu_vertices.size());
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());
		for (const Vertex& source : text_scratch.vertices)
		{
			Vertex vertex = source;
			vertex.position[0] += offset_x;
			vertex.position[1] += pixel_y;
			vertex.position[2] = 0.0f;
			cpu_vertices.push_back(vertex);
		}
		for (uint32_t index : text_scratch.indices)
			cpu_indices.push_back(base + index);

		//登记文字绘制记录
		Draw_Record draw_record{};
		draw_record.first_index = index_base;
		draw_record.index_count = static_cast<uint32_t>(text_scratch.indices.size());
		draw_record.color = color;
		draw_record.text = true;
		draw_record.clip = current_clip;
		draws.push_back(draw_record);

		//登记本帧使用的字形图集
		text_atlas = record->atlas_texture;
		return true;
	}

	//把控件树绘制到给定命令缓冲
	void Ui_Renderer::draw(const Ui_Context& ui, VkCommandBuffer command_buffer, VkExtent2D extent, uint32_t slot)
	{
		//前置：未构建 / 命令缓冲为空 / 帧槽越界
		if (!valid() || command_buffer == VK_NULL_HANDLE || slot >= slots.size())
			return;
		//目标尺寸非法
		if (extent.width == 0 || extent.height == 0)
			return;

		//复位本帧 CPU 数据
		cpu_vertices.clear();
		cpu_indices.clear();
		draws.clear();
		text_atlas = 0;

		//UI 字体与字体高度
		const uint64_t font_handle = ui.font_get();
		const Font_Record* font_record = (font != nullptr && font_handle != 0) ? font->font_get(font_handle) : nullptr;
		const float text_height = font_record != nullptr ? static_cast<float>(font_record->pixel_height) : 0.0f;

		//焦点环（4 条 2 像素边框；颜色固定为强调蓝）
		const Ui_Color focus_color{ 0.35f, 0.65f, 1.0f, 1.0f };
		auto focus_ring = [this, &focus_color](const Ui_Rect& area) -> void
			{
				//边框厚度
				const float thickness = 2.0f;
				quad_append(Ui_Rect{ area.x, area.y, area.w, thickness }, focus_color);
				quad_append(Ui_Rect{ area.x, area.y + area.h - thickness, area.w, thickness }, focus_color);
				quad_append(Ui_Rect{ area.x, area.y, thickness, area.h }, focus_color);
				quad_append(Ui_Rect{ area.x + area.w - thickness, area.y, thickness, area.h }, focus_color);
			};
		//系统焦点环（FocusChanged 映射到控件；橙色以区别于 UI 本地焦点）
		const Ui_Color system_focus_color{ 0.95f, 0.62f, 0.18f, 1.0f };
		auto system_focus_ring = [this, &system_focus_color](const Ui_Rect& area) -> void
			{
				//边框厚度
				const float thickness = 2.0f;
				quad_append(Ui_Rect{ area.x, area.y, area.w, thickness }, system_focus_color);
				quad_append(Ui_Rect{ area.x, area.y + area.h - thickness, area.w, thickness }, system_focus_color);
				quad_append(Ui_Rect{ area.x, area.y, thickness, area.h }, system_focus_color);
				quad_append(Ui_Rect{ area.x + area.w - thickness, area.y, thickness, area.h }, system_focus_color);
			};

		//按创建顺序遍历控件（父先于子；逆序命中测试已在交互侧完成）
		const std::size_t count = ui.node_count();
		for (std::size_t index = 0; index < count; ++index)
		{
			const Ui_Node* node = ui.node_at(index);
			//跳过已销毁 / 不可见节点
			if (node == nullptr || !node->visible)
				continue;
			//根节点不绘制
			if (node->type == Ui_Node_Type::Root)
				continue;

			//控件裁剪矩形（与渲染范围求交，避免 scissor 越界）
			Ui_Rect clip{};
			clip.x = std::max(node->rect.x, 0.0f);
			clip.y = std::max(node->rect.y, 0.0f);
			const float right = std::min(node->rect.x + node->rect.w, static_cast<float>(extent.width));
			const float bottom = std::min(node->rect.y + node->rect.h, static_cast<float>(extent.height));
			clip.w = std::max(right - clip.x, 0.0f);
			clip.h = std::max(bottom - clip.y, 0.0f);
			//裁剪区域为空则不绘制（含文字溢出被完全裁掉的情形）
			if (clip.w <= 0.0f || clip.h <= 0.0f)
				continue;
			//本控件后续几何按此裁剪
			current_clip = clip;

			switch (node->type)
			{
			//面板：矩形背景
			case Ui_Node_Type::Panel:
				quad_append(node->rect, node->color);
				break;
			//纯文本标签：居左、垂直居中
			case Ui_Node_Type::Label:
				text_append(font_handle, node->text, node->rect.x,
					node->rect.y + (node->rect.h - text_height) * 0.5f, 0.0f, node->text_color);
				break;
			//按钮：背景 + 居中标签
			case Ui_Node_Type::Button:
			{
				//按悬停 / 按下调整背景亮度
				Ui_Color color = node->color;
				if (node->pressed)
				{
					color.r *= 0.72f;
					color.g *= 0.72f;
					color.b *= 0.72f;
				}
				else if (node->hovered)
				{
					color.r = std::min(1.0f, color.r + 0.12f);
					color.g = std::min(1.0f, color.g + 0.12f);
					color.b = std::min(1.0f, color.b + 0.12f);
				}
				quad_append(node->rect, color);
				//标签垂直居中
				if (!node->label.empty())
					text_append(font_handle, node->label, node->rect.x,
						node->rect.y + (node->rect.h - text_height) * 0.5f, node->rect.w, node->text_color);
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//文本框：背景 + 左对齐文本 + 光标
			case Ui_Node_Type::Text_Box:
			{
				quad_append(node->rect, node->color);
				const float padding = 6.0f;
				const float text_x = node->rect.x + padding;
				const float text_y = node->rect.y + (node->rect.h - text_height) * 0.5f;
				text_append(font_handle, node->text, text_x, text_y, 0.0f, node->text_color);
				//焦点态绘制光标
				if (node->focused)
				{
					uint32_t caret_width = 0;
					uint32_t caret_height = 0;
					//度量光标之前的文本宽度（空串按 0）
					if (font_record != nullptr && node->caret > 0 && node->caret <= node->text.size())
						Font_Context::text_measure(*font_record, node->text.substr(0, node->caret),
							caret_width, caret_height);
					Ui_Rect caret_rect{};
					caret_rect.x = text_x + static_cast<float>(caret_width);
					caret_rect.y = node->rect.y + 4.0f;
					caret_rect.w = 2.0f;
					caret_rect.h = node->rect.h - 8.0f;
					Ui_Color caret_color{ 1.0f, 1.0f, 1.0f, 1.0f };
					quad_append(caret_rect, caret_color);
					focus_ring(node->rect);
				}
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//复选框：方框 + 勾选 + 右侧标签
			case Ui_Node_Type::Checkbox:
			{
				//方框边长取控件高度上限 20 像素，垂直居中
				const float box_size = std::min(node->rect.h, 20.0f);
				Ui_Rect box_rect{ node->rect.x, node->rect.y + (node->rect.h - box_size) * 0.5f,
					box_size, box_size };
				//悬停 / 按下时方框略亮
				Ui_Color box_color = node->color;
				if (node->pressed || node->hovered)
				{
					box_color.r = std::min(1.0f, box_color.r + 0.20f);
					box_color.g = std::min(1.0f, box_color.g + 0.20f);
					box_color.b = std::min(1.0f, box_color.b + 0.20f);
				}
				quad_append(box_rect, box_color);
				//选中态绘制内块
				if (node->checked)
				{
					const float inset = 4.0f;
					Ui_Rect inner_rect{ box_rect.x + inset, box_rect.y + inset,
						box_rect.w - inset * 2.0f, box_rect.h - inset * 2.0f };
					quad_append(inner_rect, focus_color);
				}
				//右侧标签
				if (!node->label.empty())
					text_append(font_handle, node->label, box_rect.x + box_rect.w + 6.0f,
						node->rect.y + (node->rect.h - text_height) * 0.5f, 0.0f, node->text_color);
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//滑动条：轨道 + 进度填充 + 滑块
			case Ui_Node_Type::Slider:
			{
				//轨道（高 4 像素，垂直居中）
				const float track_y = node->rect.y + node->rect.h * 0.5f - 2.0f;
				const Ui_Rect track_rect{ node->rect.x, track_y, node->rect.w, 4.0f };
				quad_append(track_rect, node->color);
				//已选进度
				const float fill_width = node->rect.w * node->value;
				if (fill_width > 0.0f)
					quad_append(Ui_Rect{ node->rect.x, track_y, fill_width, 4.0f }, focus_color);
				//滑块（10 × 18，居中于当前值）
				const float knob_x = node->rect.x + node->rect.w * node->value - 5.0f;
				Ui_Rect knob_rect{ knob_x, node->rect.y + node->rect.h * 0.5f - 9.0f, 10.0f, 18.0f };
				Ui_Color knob_color{ 0.85f, 0.88f, 0.95f, 1.0f };
				if (node->pressed || node->hovered)
				{
					knob_color.r = 1.0f;
					knob_color.g = 1.0f;
					knob_color.b = 1.0f;
				}
				quad_append(knob_rect, knob_color);
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			default:
				break;
			}
		}

		//无任何绘制内容
		if (cpu_indices.empty())
			return;

		//确保帧槽缓冲并上传
		Slot_Buffers& buffers = slots[slot];
		if (!slot_ensure(buffers, static_cast<uint32_t>(cpu_vertices.size()), static_cast<uint32_t>(cpu_indices.size())))
			return;
		std::memcpy(buffers.vertex_mapped, cpu_vertices.data(), cpu_vertices.size() * sizeof(Vertex));
		std::memcpy(buffers.index_mapped, cpu_indices.data(), cpu_indices.size() * sizeof(uint32_t));

		//取管线与布局
		VkPipeline solid_vk = pipeline->pipeline_by_handle_get(solid_pipeline);
		VkPipeline text_vk = pipeline->pipeline_by_handle_get(text_pipeline);
		VkPipelineLayout layout = pipeline->layout_get();
		if (solid_vk == VK_NULL_HANDLE && text_vk == VK_NULL_HANDLE)
			return;

		//绑定顶点 / 索引缓冲
		const VkDeviceSize vertex_offset = 0;
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffers.vertex_buffer, &vertex_offset);
		vkCmdBindIndexBuffer(command_buffer, buffers.index_buffer, 0, VK_INDEX_TYPE_UINT32);

		//推送常量（像素坐标 → NDC 矩阵 + 颜色）
		struct Push_Constant
		{
			float transform[16];
			float color[4];
		};
		/*
		像素坐标 → NDC 的仿射矩阵（列主序）
		Vulkan 视口把 NDC y 映射为「向下递增」的帧缓冲行号，故**不做 Y 翻转**：
		x' = 2 / 宽 × px - 1；y' = 2 / 高 × py - 1。
		*/
		Push_Constant push{};
		const float width = static_cast<float>(extent.width);
		const float height = static_cast<float>(extent.height);
		push.transform[0] = 2.0f / width;
		push.transform[5] = 2.0f / height;
		push.transform[10] = 1.0f;
		push.transform[12] = -1.0f;
		push.transform[13] = -1.0f;
		push.transform[15] = 1.0f;

		//当前已绑定管线（0 = 未绑定；1 = 纯色；2 = 文字）
		int active_pipeline = 0;
		//逐条绘制
		for (const Draw_Record& record : draws)
		{
			//按控件裁剪矩形设置 scissor（向外取整，保证文字边缘不被误裁）
			VkRect2D scissor{};
			scissor.offset = { static_cast<int32_t>(std::floor(record.clip.x)),
				static_cast<int32_t>(std::floor(record.clip.y)) };
			scissor.extent = { static_cast<uint32_t>(std::ceil(record.clip.w)),
				static_cast<uint32_t>(std::ceil(record.clip.h)) };
			//零面积裁剪跳过该绘制
			if (scissor.extent.width == 0 || scissor.extent.height == 0)
				continue;
			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			//文字绘制
			if (record.text)
			{
				//无文字管线或字形图集则跳过
				if (text_vk == VK_NULL_HANDLE || text_atlas == 0 || texture == nullptr)
					continue;
				//字形图集变化时重写 UI 专用描述符集
				if (buffers.bound_atlas != text_atlas)
				{
					VkImageView view = VK_NULL_HANDLE;
					VkSampler sampler = VK_NULL_HANDLE;
					if (texture->texture_bind_get(text_atlas, view, sampler))
					{
						pipeline->ui_texture_bind(slot, sampler, view);
						buffers.bound_atlas = text_atlas;
					}
				}
				//取 UI 专用采样集
				const VkDescriptorSet ui_set = pipeline->ui_set_get(slot);
				if (ui_set == VK_NULL_HANDLE)
					continue;
				//切换管线与采样集
				if (active_pipeline != 2)
				{
					vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, text_vk);
					vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
						1, 1, &ui_set, 0, nullptr);
					active_pipeline = 2;
				}
			}
			//纯色绘制
			else
			{
				//无纯色管线则跳过
				if (solid_vk == VK_NULL_HANDLE)
					continue;
				//切换纯色管线
				if (active_pipeline != 1)
				{
					vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_vk);
					active_pipeline = 1;
				}
			}

			//写入颜色并绘制
			push.color[0] = record.color.r;
			push.color[1] = record.color.g;
			push.color[2] = record.color.b;
			push.color[3] = record.color.a;
			vkCmdPushConstants(command_buffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(Push_Constant), &push);
			vkCmdDrawIndexed(command_buffer, record.index_count, 1, record.first_index, 0, 0);
		}

		//恢复全屏裁剪（外部 UI 回调按自身设置继续绘制）
		VkRect2D full_scissor{};
		full_scissor.offset = { 0, 0 };
		full_scissor.extent = extent;
		vkCmdSetScissor(command_buffer, 0, 1, &full_scissor);
	}
}
