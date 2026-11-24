#include "../局部命名空间使用.h"
//UI 渲染器声明
#include "Engine/EngineRenderer/src/render/UI/UI渲染器.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	namespace
	{
		//按节点不透明度调整颜色 alpha
		Ui_Color with_opacity(const Ui_Color& color, float opacity)
		{
			Ui_Color result = color;
			const float clamped = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
			result.a *= clamped;
			return result;
		}

		//求两个矩形的并集（包裹区域；第九百六十九轮合批用）
		Ui_Rect rect_union(const Ui_Rect& left, const Ui_Rect& right)
		{
			const float x0 = std::min(left.x, right.x);
			const float y0 = std::min(left.y, right.y);
			const float x1 = std::max(left.x + left.w, right.x + right.w);
			const float y1 = std::max(left.y + left.h, right.y + right.h);
			return Ui_Rect{ x0, y0, x1 - x0, y1 - y0 };
		}

		//求两个矩形的交集（无重叠时返回零矩形）
		Ui_Rect rect_intersect(const Ui_Rect& left, const Ui_Rect& right)
		{
			const float x0 = std::max(left.x, right.x);
			const float y0 = std::max(left.y, right.y);
			const float x1 = std::min(left.x + left.w, right.x + right.w);
			const float y1 = std::min(left.y + left.h, right.y + right.h);
			//无重叠
			if (x1 <= x0 || y1 <= y0)
				return Ui_Rect{};
			return Ui_Rect{ x0, y0, x1 - x0, y1 - y0 };
		}

		/*
		矩形是否完全落在圆角矩形的四角切除区（第九百七十二轮）
		圆角容器裁剪子项时以矩形近似：与任何角圆盘都不相交且位于该角方块内，
		即判定为可整体跳过；部分相交仍保留（矩形裁剪无法表达弧形边界）。
		*/
		bool rect_in_rounded_corner(const Ui_Rect& rect, const Ui_Rect& round_rect, float radius)
		{
			//半径非正不构成圆角
			if (radius <= 0.0f)
				return false;
			const float x0 = round_rect.x;
			const float y0 = round_rect.y;
			const float x1 = round_rect.x + round_rect.w;
			const float y1 = round_rect.y + round_rect.h;
			//四角方块与圆心（左上 / 右上 / 左下 / 右下）
			const float corners[4][6] = {
				{ x0, y0, x0 + radius, y0 + radius, x0 + radius, y0 + radius },
				{ x1 - radius, y0, x1, y0 + radius, x1 - radius, y0 + radius },
				{ x0, y1 - radius, x0 + radius, y1, x0 + radius, y1 - radius },
				{ x1 - radius, y1 - radius, x1, y1, x1 - radius, y1 - radius } };
			for (int corner = 0; corner < 4; ++corner)
			{
				//矩形须完全位于该角方块内
				if (rect.x < corners[corner][0] || rect.y < corners[corner][1]
					|| rect.x + rect.w > corners[corner][2] || rect.y + rect.h > corners[corner][3])
					continue;
				//圆心到矩形最近点的距离
				const float nearest_x = std::min(std::max(corners[corner][4], rect.x), rect.x + rect.w);
				const float nearest_y = std::min(std::max(corners[corner][5], rect.y), rect.y + rect.h);
				const float delta_x = corners[corner][4] - nearest_x;
				const float delta_y = corners[corner][5] - nearest_y;
				//与圆盘不相交：整体落在切除区
				if (delta_x * delta_x + delta_y * delta_y > radius * radius)
					return true;
			}
			return false;
		}

		//把一段字节混入 FNV-1a 指纹
		void hash_bytes(uint64_t& hash, const void* data, std::size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (std::size_t index = 0; index < size; ++index)
			{
				hash ^= static_cast<uint64_t>(bytes[index]);
				hash *= 1099511628211ull;
			}
		}

		//把字符串混入指纹
		void hash_string(uint64_t& hash, const std::string& text)
		{
			const std::size_t size = text.size();
			hash_bytes(hash, &size, sizeof(size));
			hash_bytes(hash, text.data(), size);
		}

		//按字间距度量单行宽度（基线行宽 + 字符间隔数 × 间距）
		float text_width_spaced(Font_Record& record, const std::string& text, float spacing)
		{
			uint32_t width = 0;
			uint32_t height = 0;
			Font_Context::text_measure(record, text, width, height);
			//无间距时沿用整体度量
			if (spacing == 0.0f)
				return static_cast<float>(width);
			//统计码点数（间隔数 = 码点数 - 1）
			std::size_t count = 0;
			for (std::size_t index = 0; index < text.size();)
			{
				//按首字节推断 UTF-8 码点长度
				const unsigned char lead = static_cast<unsigned char>(text[index]);
				std::size_t length = 1;
				if ((lead & 0xE0) == 0xC0)
					length = 2;
				else if ((lead & 0xF0) == 0xE0)
					length = 3;
				else if ((lead & 0xF8) == 0xF0)
					length = 4;
				//截断时回退到剩余长度
				if (index + length > text.size())
					length = text.size() - index;
				index += length;
				++count;
			}
			//单字符无间隔
			if (count <= 1)
				return static_cast<float>(width);
			return static_cast<float>(width) + spacing * static_cast<float>(count - 1);
		}

		//解码单个 UTF-8 码点并返回其字节长度（非法首字节按 1 字节跳过）
		std::size_t utf8_decode_one(const std::string& text, std::size_t index, uint32_t& codepoint)
		{
			const unsigned char lead = static_cast<unsigned char>(text[index]);
			std::size_t length = 1;
			uint32_t value = lead;
			if ((lead & 0xE0) == 0xC0)
			{
				length = 2;
				value = static_cast<uint32_t>(lead & 0x1F);
			}
			else if ((lead & 0xF0) == 0xE0)
			{
				length = 3;
				value = static_cast<uint32_t>(lead & 0x0F);
			}
			else if ((lead & 0xF8) == 0xF0)
			{
				length = 4;
				value = static_cast<uint32_t>(lead & 0x07);
			}
			//截断时回退到剩余长度
			if (index + length > text.size())
				length = text.size() - index;
			//拼接续字节
			for (std::size_t offset = 1; offset < length; ++offset)
				value = (value << 6) | (static_cast<unsigned char>(text[index + offset]) & 0x3F);
			codepoint = value;
			return length;
		}

		//把字节下标后移到 UTF-8 字符头（越界返回串尾）
		std::size_t utf8_align_forward(const std::string& text, std::size_t index)
		{
			if (index >= text.size())
				return text.size();
			//落在续字节则继续后移
			while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
				++index;
			return index;
		}
	}

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
		inherited_clips.clear();
		text_cache.clear();
		text_cache_stamp = 0;
		block_cache.clear();
		block_cache_stamp = 0;
		round_points_x.clear();
		round_points_y.clear();
		arc_cos.clear();
		arc_sin.clear();
		arc_table_ready = false;
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

	//最近一帧绘制统计
	const Ui_Render_Stats& Ui_Renderer::stats_get(void) const
	{
		return stats;
	}

	//设置文本几何 / 文本块布局缓存容量
	void Ui_Renderer::cache_limits_set(uint32_t text_capacity, uint32_t block_capacity)
	{
		//钳制到 [1, 1024]
		text_cache_limit = std::clamp<std::size_t>(text_capacity, 1, 1024);
		block_cache_limit = std::clamp<std::size_t>(block_capacity, 1, 1024);
		//清空既有条目，避免超限存量
		text_cache.clear();
		block_cache.clear();
	}

	//计算控件树绘制相关字段的指纹
	uint64_t Ui_Renderer::fingerprint_build(const Ui_Context& ui) const
	{
		//FNV-1a 初值
		uint64_t hash = 1469598103934665603ull;
		const std::size_t count = ui.node_count();
		hash_bytes(hash, &count, sizeof(count));
		for (std::size_t index = 0; index < count; ++index)
		{
			const Ui_Node* node = ui.node_at(index);
			//已销毁槽位不参与绘制
			if (node == nullptr || !node->alive)
				continue;
			hash_bytes(hash, &node->handle, sizeof(node->handle));
			hash_bytes(hash, &node->parent, sizeof(node->parent));
			hash_bytes(hash, &node->type, sizeof(node->type));
			hash_bytes(hash, &node->rect, sizeof(node->rect));
			hash_bytes(hash, &node->color, sizeof(node->color));
			hash_bytes(hash, &node->text_color, sizeof(node->text_color));
			hash_bytes(hash, &node->border_color, sizeof(node->border_color));
			hash_bytes(hash, &node->opacity, sizeof(node->opacity));
			hash_bytes(hash, &node->corner_radius, sizeof(node->corner_radius));
			hash_bytes(hash, &node->border_width, sizeof(node->border_width));
			hash_bytes(hash, &node->role, sizeof(node->role));
			hash_bytes(hash, &node->texture, sizeof(node->texture));
			hash_bytes(hash, &node->icon_texture, sizeof(node->icon_texture));
			hash_bytes(hash, &node->slice_left, sizeof(node->slice_left));
			hash_bytes(hash, &node->slice_top, sizeof(node->slice_top));
			hash_bytes(hash, &node->slice_right, sizeof(node->slice_right));
			hash_bytes(hash, &node->slice_bottom, sizeof(node->slice_bottom));
			hash_bytes(hash, &node->text_font, sizeof(node->text_font));
			hash_bytes(hash, &node->text_wrap, sizeof(node->text_wrap));
			//多行文本框影响文本起始纵坐标（第九百六十六轮）
			hash_bytes(hash, &node->multiline, sizeof(node->multiline));
			hash_bytes(hash, &node->line_height, sizeof(node->line_height));
			hash_bytes(hash, &node->letter_spacing, sizeof(node->letter_spacing));
			hash_bytes(hash, &node->text_indent, sizeof(node->text_indent));
			hash_bytes(hash, &node->text_shadow.offset_x, sizeof(node->text_shadow.offset_x));
			hash_bytes(hash, &node->text_shadow.offset_y, sizeof(node->text_shadow.offset_y));
			hash_bytes(hash, &node->text_shadow.color, sizeof(node->text_shadow.color));
			//行内着色区间（数量 + 边界 + 颜色）
			const std::size_t span_count = node->text_spans.size();
			hash_bytes(hash, &span_count, sizeof(span_count));
			for (const Ui_Text_Span& span : node->text_spans)
			{
				hash_bytes(hash, &span.begin, sizeof(span.begin));
				hash_bytes(hash, &span.end, sizeof(span.end));
				hash_bytes(hash, &span.color, sizeof(span.color));
				hash_bytes(hash, &span.font, sizeof(span.font));
			}
			hash_bytes(hash, &node->text_valign, sizeof(node->text_valign));
			hash_bytes(hash, &node->text_halign, sizeof(node->text_halign));
			hash_bytes(hash, &node->text_ellipsis, sizeof(node->text_ellipsis));
			hash_bytes(hash, &node->visible, sizeof(node->visible));
			hash_bytes(hash, &node->enabled, sizeof(node->enabled));
			hash_bytes(hash, &node->hovered, sizeof(node->hovered));
			hash_bytes(hash, &node->pressed, sizeof(node->pressed));
			hash_bytes(hash, &node->focused, sizeof(node->focused));
			hash_bytes(hash, &node->system_focused, sizeof(node->system_focused));
			hash_bytes(hash, &node->checked, sizeof(node->checked));
			hash_bytes(hash, &node->value, sizeof(node->value));
			hash_bytes(hash, &node->display_value, sizeof(node->display_value));
			hash_bytes(hash, &node->anim_speed, sizeof(node->anim_speed));
			hash_bytes(hash, &node->padding, sizeof(node->padding));
			hash_bytes(hash, &node->margin, sizeof(node->margin));
			hash_bytes(hash, &node->stretch, sizeof(node->stretch));
			hash_bytes(hash, &node->flow_wrap, sizeof(node->flow_wrap));
			hash_bytes(hash, &node->flow_justify, sizeof(node->flow_justify));
			hash_bytes(hash, &node->selected, sizeof(node->selected));
			hash_bytes(hash, &node->select_multi, sizeof(node->select_multi));
			hash_string(hash, node->tooltip);
			//提示可见性（到达延时后变化一次，避免逐帧重建）
			const bool tooltip_ready = node->hovered && node->hover_time >= 0.5f;
			hash_bytes(hash, &tooltip_ready, sizeof(tooltip_ready));
			//下拉状态与选项（第九百六十五轮）
			hash_bytes(hash, &node->combo_index, sizeof(node->combo_index));
			hash_bytes(hash, &node->combo_open, sizeof(node->combo_open));
			hash_bytes(hash, &node->combo_scroll, sizeof(node->combo_scroll));
			const std::size_t combo_count = node->combo_items.size();
			hash_bytes(hash, &combo_count, sizeof(combo_count));
			for (const std::string& item : node->combo_items)
				hash_string(hash, item);
			hash_bytes(hash, &node->vertical, sizeof(node->vertical));
			hash_bytes(hash, &node->clip_children, sizeof(node->clip_children));
			//网格布局参数（影响子项矩形；第九百八十七轮）
			hash_bytes(hash, &node->grid_columns, sizeof(node->grid_columns));
			hash_bytes(hash, &node->grid_gap, sizeof(node->grid_gap));
			hash_bytes(hash, &node->fit_children, sizeof(node->fit_children));
			hash_bytes(hash, &node->caret, sizeof(node->caret));
			hash_bytes(hash, &node->select_begin, sizeof(node->select_begin));
			hash_bytes(hash, &node->select_end, sizeof(node->select_end));
			hash_string(hash, node->label);
			hash_string(hash, node->text);
			hash_string(hash, node->preedit);
		}
		return hash;
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

		//登记绘制记录（相邻同色合批；第九百六十九轮）
		solid_draw_append(color, index_base, 6);
	}

	//登记纯色绘制记录（相邻同色同裁剪合并）
	void Ui_Renderer::solid_draw_append(const Ui_Color& color, uint32_t first_index, uint32_t index_count)
	{
		//上一条为同色且索引连续的纯色记录：合并（裁剪取并集，几何仍在各自矩形内）
		if (!draws.empty())
		{
			Draw_Record& last = draws.back();
			if (!last.text && last.texture == 0
				&& last.first_index + last.index_count == first_index
				&& last.color.r == color.r && last.color.g == color.g
				&& last.color.b == color.b && last.color.a == color.a
				&& last.clip_round_radius == current_round_radius
				&& last.clip_round.x == current_round_rect.x && last.clip_round.y == current_round_rect.y
				&& last.clip_round.w == current_round_rect.w && last.clip_round.h == current_round_rect.h)
			{
				last.index_count += index_count;
				last.clip = rect_union(last.clip, current_clip);
				stats.batch_merges += 1;
				return;
			}
		}
		Draw_Record record{};
		record.first_index = first_index;
		record.index_count = index_count;
		record.color = color;
		record.text = false;
		record.clip = current_clip;
		record.clip_round = current_round_rect;
		record.clip_round_radius = current_round_radius;
		draws.push_back(record);
	}

	//登记纹理绘制记录（相邻同纹理同色合并；第九百七十六轮）
	void Ui_Renderer::texture_draw_append(uint64_t texture, bool text, const Ui_Color& color,
		uint32_t first_index, uint32_t index_count)
	{
		//上一条为同纹理同类别同色且索引连续：合并（裁剪取并集）
		if (!draws.empty())
		{
			Draw_Record& last = draws.back();
			if (last.texture == texture && last.text == text
				&& last.first_index + last.index_count == first_index
				&& last.color.r == color.r && last.color.g == color.g
				&& last.color.b == color.b && last.color.a == color.a
				&& last.clip_round_radius == current_round_radius
				&& last.clip_round.x == current_round_rect.x && last.clip_round.y == current_round_rect.y
				&& last.clip_round.w == current_round_rect.w && last.clip_round.h == current_round_rect.h)
			{
				last.index_count += index_count;
				last.clip = rect_union(last.clip, current_clip);
				stats.batch_merges += 1;
				return;
			}
		}
		Draw_Record record{};
		record.first_index = first_index;
		record.index_count = index_count;
		record.color = color;
		record.texture = texture;
		record.text = text;
		record.clip = current_clip;
		record.clip_round = current_round_rect;
		record.clip_round_radius = current_round_radius;
		draws.push_back(record);
	}

	//追加一个圆角矩形并登记纯色绘制
	void Ui_Renderer::rounded_quad_append(const Ui_Rect& rect, float radius, const Ui_Color& color)
	{
		//零面积 / 非正半径按直角矩形处理（既有路径逐字节不变）
		if (rect.w <= 0.0f || rect.h <= 0.0f || radius <= 0.0f)
		{
			quad_append(rect, color);
			return;
		}
		//半径不超过短边一半
		const float short_side = std::min(rect.w, rect.h);
		float r = std::min(radius, short_side * 0.5f);
		//极限半径不足半像素时仍按直角处理
		if (r < 0.5f)
		{
			quad_append(rect, color);
			return;
		}

		//矩形边界
		const float x0 = rect.x;
		const float y0 = rect.y;
		const float x1 = rect.x + rect.w;
		const float y1 = rect.y + rect.h;
		//每角圆弧分段数
		const int segments = 8;
		//单位圆采样表惰性预算（4 角 × (分段 + 1)，角度公式与旧实现一致）
		if (!arc_table_ready)
		{
			const float pi = 3.14159265358979323846f;
			const float starts[4] = { -90.0f, 0.0f, 90.0f, 180.0f };
			const std::size_t per_corner = static_cast<std::size_t>(segments) + 1;
			arc_cos.assign(per_corner * 4, 0.0f);
			arc_sin.assign(per_corner * 4, 0.0f);
			for (std::size_t corner = 0; corner < 4; ++corner)
			{
				for (int i = 0; i <= segments; ++i)
				{
					const float degree = starts[corner] + 90.0f * static_cast<float>(i) / static_cast<float>(segments);
					const float radian = degree * pi / 180.0f;
					arc_cos[corner * per_corner + static_cast<std::size_t>(i)] = std::cos(radian);
					arc_sin[corner * per_corner + static_cast<std::size_t>(i)] = std::sin(radian);
				}
			}
			arc_table_ready = true;
		}

		//周长采样点（复用成员缓冲，顺时针，屏幕坐标 Y 向下）
		round_points_x.clear();
		round_points_y.clear();
		const std::size_t per_corner = static_cast<std::size_t>(segments) + 1;
		/*
		追加一个 90 度圆弧（含首尾）
		cx / cy 为圆心，corner 选采样表起始段；角度按屏幕坐标（Y 向下）解释。
		*/
		auto arc_append = [&](float cx, float cy, std::size_t corner) -> void
			{
				for (int i = 0; i <= segments; ++i)
				{
					const std::size_t sample = corner * per_corner + static_cast<std::size_t>(i);
					round_points_x.push_back(cx + r * arc_cos[sample]);
					round_points_y.push_back(cy + r * arc_sin[sample]);
				}
			};
		//右上 → 右下 → 左下 → 左上（首尾相接构成闭合轮廓）
		arc_append(x1 - r, y0 + r, 0);
		arc_append(x1 - r, y1 - r, 1);
		arc_append(x0 + r, y1 - r, 2);
		arc_append(x0 + r, y0 + r, 3);

		//顶点基址与索引基址
		const uint32_t base = static_cast<uint32_t>(cpu_vertices.size());
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());
		//中心点（凸形状，扇形三角化）
		Vertex center{};
		center.position[0] = (x0 + x1) * 0.5f;
		center.position[1] = (y0 + y1) * 0.5f;
		center.position[2] = 0.0f;
		cpu_vertices.push_back(center);
		//周长点
		for (std::size_t i = 0; i < round_points_x.size(); ++i)
		{
			Vertex vertex{};
			vertex.position[0] = round_points_x[i];
			vertex.position[1] = round_points_y[i];
			vertex.position[2] = 0.0f;
			cpu_vertices.push_back(vertex);
		}
		//扇形三角形
		const uint32_t point_count = static_cast<uint32_t>(round_points_x.size());
		for (uint32_t i = 0; i < point_count; ++i)
		{
			const uint32_t current = base + 1 + i;
			const uint32_t next = base + 1 + (i + 1) % point_count;
			cpu_indices.push_back(base);
			cpu_indices.push_back(current);
			cpu_indices.push_back(next);
		}

		//登记绘制记录（相邻同色合批；第九百六十九轮）
		solid_draw_append(color, index_base, point_count * 3);
	}

	//追加带描边的背景（先描边后填充）
	void Ui_Renderer::background_append(const Ui_Rect& rect, float radius, float border_width,
		const Ui_Color& border_color, const Ui_Color& fill_color)
	{
		//无描边：直接填充
		if (border_width <= 0.0f)
		{
			rounded_quad_append(rect, radius, fill_color);
			return;
		}
		//描边宽不超过短边一半，避免内矩形反向
		const float short_side = std::min(rect.w, rect.h);
		const float width = std::min(border_width, short_side * 0.5f);
		//外圈描边
		rounded_quad_append(rect, radius, border_color);
		//内圈填充（半径随内缩递减）
		const Ui_Rect inner{ rect.x + width, rect.y + width,
			rect.w - width * 2.0f, rect.h - width * 2.0f };
		const float inner_radius = radius > width ? radius - width : 0.0f;
		rounded_quad_append(inner, inner_radius, fill_color);
	}

	//追加一个纹理矩形并登记纹理绘制
	void Ui_Renderer::quad_textured_append(uint64_t texture, const Ui_Rect& rect, const Ui_Color& color)
	{
		//整图路径：UV 铺满 0..1
		quad_textured_append_uv(texture, rect, 0.0f, 0.0f, 1.0f, 1.0f, color);
	}

	//追加一个指定 UV 范围的纹理矩形
	void Ui_Renderer::quad_textured_append_uv(uint64_t texture, const Ui_Rect& rect,
		float u0, float v0, float u1, float v1, const Ui_Color& color)
	{
		//零面积或无效纹理不产生几何
		if (texture == 0 || rect.w <= 0.0f || rect.h <= 0.0f)
			return;
		//登记前记录索引基址
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());
		//只追加几何
		quad_textured_uv_emit(texture, rect, u0, v0, u1, v1);
		//索引未增长说明子块退化被跳过
		if (cpu_indices.size() == index_base)
			return;

		//登记纹理绘制记录（相邻同纹理同色合并；第九百七十六轮）
		texture_draw_append(texture, false, color, index_base,
			static_cast<uint32_t>(cpu_indices.size()) - index_base);
	}

	//只追加纹理矩形几何、不登记绘制记录
	void Ui_Renderer::quad_textured_uv_emit(uint64_t texture, const Ui_Rect& rect,
		float u0, float v0, float u1, float v1)
	{
		//零面积或无效纹理不产生几何
		if (texture == 0 || rect.w <= 0.0f || rect.h <= 0.0f)
			return;
		//顶点基址
		const uint32_t base = static_cast<uint32_t>(cpu_vertices.size());

		//矩形边界
		const float x0 = rect.x;
		const float y0 = rect.y;
		const float x1 = rect.x + rect.w;
		const float y1 = rect.y + rect.h;

		//四角顶点（按给定 UV 范围取样）
		Vertex vertex{};
		vertex.position[0] = x0; vertex.position[1] = y0; vertex.position[2] = 0.0f;
		vertex.uv[0] = u0; vertex.uv[1] = v0;
		cpu_vertices.push_back(vertex);
		vertex.position[0] = x1; vertex.position[1] = y0;
		vertex.uv[0] = u1; vertex.uv[1] = v0;
		cpu_vertices.push_back(vertex);
		vertex.position[0] = x1; vertex.position[1] = y1;
		vertex.uv[0] = u1; vertex.uv[1] = v1;
		cpu_vertices.push_back(vertex);
		vertex.position[0] = x0; vertex.position[1] = y1;
		vertex.uv[0] = u0; vertex.uv[1] = v1;
		cpu_vertices.push_back(vertex);

		//两个三角形
		cpu_indices.push_back(base + 0);
		cpu_indices.push_back(base + 1);
		cpu_indices.push_back(base + 2);
		cpu_indices.push_back(base + 0);
		cpu_indices.push_back(base + 2);
		cpu_indices.push_back(base + 3);
	}

	//追加图片九宫格
	void Ui_Renderer::image_slice_append(uint64_t texture, const Ui_Rect& rect,
		float left, float top, float right, float bottom, const Ui_Color& color)
	{
		//无切分：按整图拉伸
		if (left <= 0.0f && top <= 0.0f && right <= 0.0f && bottom <= 0.0f)
		{
			quad_textured_append(texture, rect, color);
			return;
		}
		//无效纹理或零面积不绘制
		if (texture == 0 || rect.w <= 0.0f || rect.h <= 0.0f)
			return;
		//切分之和不超过 1（控件侧已钳制，这里再兜底）
		if (left + right > 1.0f)
			right = 1.0f - left;
		if (top + bottom > 1.0f)
			bottom = 1.0f - top;

		//目标矩形上的三列 / 三行边界
		const float x0 = rect.x;
		const float y0 = rect.y;
		const float x1 = rect.x + rect.w;
		const float y1 = rect.y + rect.h;
		const float column[4] = { x0, x0 + rect.w * left, x1 - rect.w * right, x1 };
		const float row[4] = { y0, y0 + rect.h * top, y1 - rect.h * bottom, y1 };
		//UV 上的三列 / 三行边界（Y 向下，与顶点一致）
		const float u[4] = { 0.0f, left, 1.0f - right, 1.0f };
		const float v[4] = { 0.0f, top, 1.0f - bottom, 1.0f };

		//九块子矩形共用一条绘制记录（同纹理 / 同色 / 同裁剪，几何连续）
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());
		for (int row_index = 0; row_index < 3; ++row_index)
		{
			for (int column_index = 0; column_index < 3; ++column_index)
			{
				const Ui_Rect cell{ column[column_index], row[row_index],
					column[column_index + 1] - column[column_index],
					row[row_index + 1] - row[row_index] };
				quad_textured_uv_emit(texture, cell,
					u[column_index], v[row_index], u[column_index + 1], v[row_index + 1]);
			}
		}
		//全部子块退化则不登记
		if (cpu_indices.size() == index_base)
			return;

		//登记单条纹理绘制记录（九宫格九块合批；第九百零九 / 九百七十六轮）
		texture_draw_append(texture, false, color, index_base,
			static_cast<uint32_t>(cpu_indices.size()) - index_base);
	}

	//查找文本几何缓存条目
	Ui_Renderer::Text_Cache_Entry* Ui_Renderer::text_cache_find(uint64_t font_handle, const std::string& text,
		float center_area_width)
	{
		for (Text_Cache_Entry& entry : text_cache)
		{
			//键：字体句柄 + 文本 + 居中区宽度
			if (entry.font_handle != font_handle || entry.center_area_width != center_area_width)
				continue;
			//文本一致即命中
			if (entry.text == text)
				return &entry;
		}
		return nullptr;
	}

	//写入文本几何缓存条目
	Ui_Renderer::Text_Cache_Entry* Ui_Renderer::text_cache_insert(uint64_t font_handle, const std::string& text,
		float center_area_width, Mesh_Data&& mesh, uint32_t width)
	{
		//容量已满则淘汰最久未使用条目
		if (text_cache.size() >= text_cache_limit)
		{
			std::size_t oldest = 0;
			for (std::size_t index = 1; index < text_cache.size(); ++index)
			{
				if (text_cache[index].stamp < text_cache[oldest].stamp)
					oldest = index;
			}
			text_cache.erase(text_cache.begin() + static_cast<std::ptrdiff_t>(oldest));
		}
		//登记新条目
		Text_Cache_Entry entry{};
		entry.font_handle = font_handle;
		entry.text = text;
		entry.center_area_width = center_area_width;
		entry.width = width;
		entry.mesh = std::move(mesh);
		entry.stamp = ++text_cache_stamp;
		text_cache.push_back(std::move(entry));
		return &text_cache.back();
	}

	//查找文本块布局缓存条目
	Ui_Renderer::Block_Cache_Entry* Ui_Renderer::block_cache_find(uint64_t font_handle, const std::string& text,
		float area_width, float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis,
		Ui_Align valign, const std::vector<Ui_Text_Span>& spans)
	{
		//区域宽度量化到 0.5 像素（亚像素抖动不失效；第九百八十三轮）
		const float width_key = std::round(area_width * 2.0f) * 0.5f;
		for (Block_Cache_Entry& entry : block_cache)
		{
			//键：字体 + 文本 + 区域宽度 + 行高 + 字间距 + 缩进 + 换行 + 省略 + 对齐 + 区间
			if (entry.font_handle != font_handle || entry.area_width != width_key
				|| entry.line_height != line_height || entry.letter_spacing != letter_spacing
				|| entry.text_indent != text_indent
				|| entry.wrap != wrap || entry.ellipsis != ellipsis || entry.valign != valign)
				continue;
			//区间数量与逐项字段须一致
			if (entry.spans.size() != spans.size())
				continue;
			bool span_equal = true;
			for (std::size_t index = 0; index < spans.size(); ++index)
			{
				const Ui_Text_Span& left = entry.spans[index];
				const Ui_Text_Span& right = spans[index];
				if (left.begin != right.begin || left.end != right.end || left.font != right.font
					|| left.color.r != right.color.r || left.color.g != right.color.g
					|| left.color.b != right.color.b || left.color.a != right.color.a)
				{
					span_equal = false;
					break;
				}
			}
			if (!span_equal)
				continue;
			//文本一致即命中
			if (entry.text == text)
				return &entry;
		}
		return nullptr;
	}

	//写入文本块布局缓存条目
	Ui_Renderer::Block_Cache_Entry* Ui_Renderer::block_cache_insert(uint64_t font_handle, const std::string& text,
		float area_width, float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis,
		Ui_Align valign, const std::vector<Ui_Text_Span>& spans, std::vector<std::string>&& lines)
	{
		//容量已满则淘汰最久未使用条目
		if (block_cache.size() >= block_cache_limit)
		{
			std::size_t oldest = 0;
			for (std::size_t index = 1; index < block_cache.size(); ++index)
			{
				if (block_cache[index].stamp < block_cache[oldest].stamp)
					oldest = index;
			}
			block_cache.erase(block_cache.begin() + static_cast<std::ptrdiff_t>(oldest));
		}
		//登记新条目
		Block_Cache_Entry entry{};
		entry.font_handle = font_handle;
		entry.text = text;
		//区域宽度按 0.5 像素量化存储，与查找口径一致（第九百八十三轮）
		entry.area_width = std::round(area_width * 2.0f) * 0.5f;
		entry.line_height = line_height;
		entry.letter_spacing = letter_spacing;
		entry.text_indent = text_indent;
		entry.spans = spans;
		entry.wrap = wrap;
		entry.ellipsis = ellipsis;
		entry.valign = valign;
		entry.lines = std::move(lines);
		entry.stamp = ++block_cache_stamp;
		block_cache.push_back(std::move(entry));
		return &block_cache.back();
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

		//查询文本几何缓存
		Text_Cache_Entry* entry = text_cache_find(font_handle, text, center_area_width);
		if (entry != nullptr)
		{
			//刷新使用序号并计命中
			entry->stamp = ++text_cache_stamp;
			++stats.text_cache_hits;
		}
		else
		{
			++stats.text_cache_misses;
			//生成文字几何（像素坐标，原点在文本左上角）
			Mesh_Data mesh{};
			if (!Font_Context::text_geometry_build(mesh, *record, text))
				return false;
			//度量单行宽度用于居中
			uint32_t width = 0;
			uint32_t height = 0;
			Font_Context::text_measure(*record, text, width, height);
			//写入缓存并取条目
			entry = text_cache_insert(font_handle, text, center_area_width, std::move(mesh), width);
		}

		//居中时按目标区宽度平移
		float offset_x = pixel_x;
		if (center_area_width > 0.0f)
			offset_x += (center_area_width - static_cast<float>(entry->width)) * 0.5f;

		//写入顶点与索引（索引按当前顶点基址偏移，绘制记录取索引基址）
		const uint32_t base = static_cast<uint32_t>(cpu_vertices.size());
		const uint32_t index_base = static_cast<uint32_t>(cpu_indices.size());
		for (const Vertex& source : entry->mesh.vertices)
		{
			Vertex vertex = source;
			vertex.position[0] += offset_x;
			vertex.position[1] += pixel_y;
			vertex.position[2] = 0.0f;
			cpu_vertices.push_back(vertex);
		}
		for (uint32_t index : entry->mesh.indices)
			cpu_indices.push_back(base + index);

		//登记文字绘制记录（同图集同色相邻合并；第九百七十六轮）
		texture_draw_append(record->atlas_texture, true, color, index_base,
			static_cast<uint32_t>(entry->mesh.indices.size()));

		//登记本帧使用的字形图集
		text_atlas = record->atlas_texture;
		return true;
	}

	//追加按字间距逐码点绘制的单行文本
	bool Ui_Renderer::text_append_spaced(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
		float letter_spacing, const Ui_Color& color)
	{
		//无间距时沿用整体绘制（保留整串几何缓存）
		if (letter_spacing == 0.0f)
			return text_append(font_handle, text, pixel_x, pixel_y, 0.0f, color);
		//字体模块缺失
		if (font == nullptr)
			return false;
		//取可写字体记录用于字形度量
		Font_Record* record = font->font_get_mutable(font_handle);
		if (record == nullptr)
			return false;

		//逐码点绘制并推进笔位（末字符不追加间距）
		bool appended = false;
		float pen_x = pixel_x;
		for (std::size_t index = 0; index < text.size();)
		{
			//解码当前码点
			uint32_t codepoint = 0;
			const std::size_t length = utf8_decode_one(text, index, codepoint);
			//绘制当前码点（空白字符无墨迹，text_append 返回假但不影响推进）
			appended = text_append(font_handle, text.substr(index, length), pen_x, pixel_y, 0.0f, color)
				|| appended;
			//按字形推进量前进（未烘焙字形按 0 处理）
			const Font_Glyph* glyph = Font_Context::glyph_seek(*record, codepoint);
			pen_x += glyph != nullptr ? glyph->advance : 0.0f;
			index += length;
			//非末字符追加间距
			if (index < text.size())
				pen_x += letter_spacing;
		}
		return appended;
	}

	//追加多行文本块
	bool Ui_Renderer::text_append_block(uint64_t font_handle, const std::string& text, const Ui_Rect& area,
		float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis,
		Ui_Align valign, Ui_Align halign, const std::vector<Ui_Text_Span>& spans, float opacity,
		const Ui_Color& color)
	{
		//字体未设置或字体模块缺失
		if (font_handle == 0 || font == nullptr)
			return false;
		Font_Record* record = font->font_get_mutable(font_handle);
		if (record == nullptr)
			return false;
		//确保全部字形已烘焙（供换行度量使用）
		font->glyphs_ensure(*record, text);

		//查询文本块布局缓存
		Block_Cache_Entry* entry = block_cache_find(font_handle, text, area.w, line_height, letter_spacing,
			text_indent, wrap, ellipsis, valign, spans);
		if (entry != nullptr)
		{
			//刷新使用序号并计命中
			entry->stamp = ++block_cache_stamp;
			++stats.block_cache_hits;
		}
		else
		{
			++stats.block_cache_misses;
			//按换行符分行
			std::vector<std::string> lines{};
			std::string current{};
			for (char ch : text)
			{
				if (ch == '\n')
				{
					lines.push_back(current);
					current.clear();
				}
				else
					current.push_back(ch);
			}
			lines.push_back(current);

			//自动换行：按 UTF-8 码点贪心累积，超出区域宽度即另起一行
			if (wrap && area.w > 0.0f)
			{
				std::vector<std::string> wrapped{};
				for (const std::string& line : lines)
				{
					std::string cursor{};
					for (std::size_t index = 0; index < line.size();)
					{
						//按首字节推断 UTF-8 码点长度
						const unsigned char lead = static_cast<unsigned char>(line[index]);
						std::size_t length = 1;
						if ((lead & 0xE0) == 0xC0)
							length = 2;
						else if ((lead & 0xF0) == 0xE0)
							length = 3;
						else if ((lead & 0xF8) == 0xF0)
							length = 4;
						//截断时回退到剩余长度
						if (index + length > line.size())
							length = line.size() - index;
						const std::string candidate = cursor + line.substr(index, length);
						//按字间距度量候选行宽
						const float measured_width = text_width_spaced(*record, candidate, letter_spacing);
						//首行可用宽度扣减缩进（尚无收束行时按首行）
						const float line_limit = wrapped.empty()
							? std::max(area.w - text_indent, 0.0f) : area.w;
						//当前行非空且加入后超宽：本行收束并另起一行
						if (!cursor.empty() && measured_width > line_limit)
						{
							//词边界：游标内最近空格（有则优先在此断行，空格不留在行尾）
							const std::size_t space = cursor.rfind(' ');
							if (space != std::string::npos && space > 0)
							{
								wrapped.push_back(cursor.substr(0, space));
								cursor = cursor.substr(space + 1) + line.substr(index, length);
							}
							else
							{
								wrapped.push_back(cursor);
								cursor = line.substr(index, length);
							}
						}
						else
							cursor = candidate;
						index += length;
					}
					wrapped.push_back(cursor);
				}
				lines.swap(wrapped);
			}

			//单行省略：非自动换行时把超宽行尾部裁掉并追加省略号
			if (ellipsis && !wrap && area.w > 0.0f)
			{
				const std::string ellipsis_text = "\xE2\x80\xA6";
				font->glyphs_ensure(*record, ellipsis_text);
				for (std::size_t line_index = 0; line_index < lines.size(); ++line_index)
				{
					std::string& line = lines[line_index];
					//首行可用宽度扣减缩进
					const float line_limit = line_index == 0
						? std::max(area.w - text_indent, 0.0f) : area.w;
					//按字间距度量当前行宽
					const float line_width = text_width_spaced(*record, line, letter_spacing);
					//未超宽不处理
					if (line_width <= line_limit)
						continue;
					std::string base = line;
					//逐码点从尾部回退，直到省略号可以放下
					while (!base.empty())
					{
						std::size_t cut = base.size() - 1;
						while (cut > 0 && (static_cast<unsigned char>(base[cut]) & 0xC0) == 0x80)
							--cut;
						base.erase(cut);
						//按字间距判断省略号是否可放下
						if (text_width_spaced(*record, base + ellipsis_text, letter_spacing) <= line_limit)
							break;
					}
					line = base + ellipsis_text;
				}
			}

			//写入布局缓存
			entry = block_cache_insert(font_handle, text, area.w, line_height, letter_spacing, text_indent,
				wrap, ellipsis, valign, spans, std::move(lines));
		}

		//行高（0 取字体高度）
		const float base_height = line_height > 0.0f ? line_height : static_cast<float>(record->pixel_height);
		//各行在原文中的字节偏移（换算行内区间用）
		std::vector<std::size_t> line_bases(entry->lines.size(), 0);
		std::size_t line_search = 0;
		for (std::size_t index = 0; index < entry->lines.size(); ++index)
		{
			const std::string& line = entry->lines[index];
			line_bases[index] = line_search;
			if (line.empty())
				continue;
			//按顺序在原串中定位本行
			const std::size_t found = text.find(line, line_search);
			if (found != std::string::npos)
			{
				line_bases[index] = found;
				line_search = found + line.size();
			}
			else
				line_search += line.size();
		}
		//逐行行高：显式行高优先，否则取该行涉及字体的最大像素高度
		std::vector<float> line_heights(entry->lines.size(), base_height);
		if (line_height <= 0.0f && !spans.empty())
		{
			for (std::size_t index = 0; index < entry->lines.size(); ++index)
			{
				const std::size_t line_begin = line_bases[index];
				const std::size_t line_end = line_begin + entry->lines[index].size();
				for (const Ui_Text_Span& span : spans)
				{
					//不相交或未指定字体跳过
					if (span.font == 0 || span.end <= line_begin || span.begin >= line_end)
						continue;
					//取区间字体像素高度
					const Font_Record* span_record = font->font_get(span.font);
					if (span_record != nullptr
						&& static_cast<float>(span_record->pixel_height) > line_heights[index])
						line_heights[index] = static_cast<float>(span_record->pixel_height);
				}
			}
		}
		//整体高度与起始纵坐标
		float total_height = 0.0f;
		for (float line_height_px : line_heights)
			total_height += line_height_px;
		float y = area.y;
		if (valign == Ui_Align::Center)
			y += (area.h - total_height) * 0.5f;
		else if (valign == Ui_Align::End)
			y += area.h - total_height;
		//逐行绘制
		bool appended = false;
		//是否首行（仅首行应用缩进）
		bool first_line = true;
		for (std::size_t line_index = 0; line_index < entry->lines.size(); ++line_index)
		{
			const std::string& line = entry->lines[line_index];
			if (!line.empty())
			{
				//当前行在原文中的字节偏移
				const std::size_t line_base = line_bases[line_index];
				//水平对齐偏移（Stretch 按 Start；混排字号按区间字体求和；第九百六十七轮）
				float line_x = area.x;
				if (halign == Ui_Align::Center || halign == Ui_Align::End)
				{
					float line_width = 0.0f;
					//无区间：按控件字体整体度量
					if (spans.empty())
						line_width = text_width_spaced(*record, line, letter_spacing);
					else
					{
						//收集切分点
						std::vector<std::size_t> cuts{};
						cuts.push_back(0);
						cuts.push_back(line.size());
						for (const Ui_Text_Span& span : spans)
						{
							if (span.end <= line_base || span.begin >= line_base + line.size())
								continue;
							std::size_t local_begin = span.begin > line_base ? span.begin - line_base : 0;
							std::size_t local_end = span.end > line_base ? span.end - line_base : 0;
							if (local_end > line.size())
								local_end = line.size();
							cuts.push_back(utf8_align_forward(line, local_begin));
							cuts.push_back(utf8_align_forward(line, local_end));
						}
						std::sort(cuts.begin(), cuts.end());
						cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
						//逐段按该段字体度量
						for (std::size_t index = 0; index + 1 < cuts.size(); ++index)
						{
							const std::size_t seg_begin = cuts[index];
							const std::size_t seg_end = cuts[index + 1];
							if (seg_end <= seg_begin)
								continue;
							uint64_t segment_font = font_handle;
							for (const Ui_Text_Span& span : spans)
							{
								if (span.begin <= line_base + seg_begin && span.end >= line_base + seg_end
									&& span.font != 0)
									segment_font = span.font;
							}
							Font_Record* segment_record = font->font_get_mutable(segment_font);
							const std::string segment = line.substr(seg_begin, seg_end - seg_begin);
							line_width += segment_record != nullptr
								? text_width_spaced(*segment_record, segment, letter_spacing)
								: text_width_spaced(*record, segment, letter_spacing);
						}
					}
					line_x += halign == Ui_Align::Center
						? (area.w - line_width) * 0.5f
						: area.w - line_width;
				}
				//首行整体右移缩进
				if (first_line)
					line_x += text_indent;
				//无行内区间：整行绘制
				if (spans.empty())
				{
					appended = text_append_spaced(font_handle, line, line_x, y, letter_spacing, color)
						|| appended;
				}
				else
				{
					//收集切分点（行首、行尾与各区间边界）
					std::vector<std::size_t> cuts{};
					cuts.push_back(0);
					cuts.push_back(line.size());
					for (const Ui_Text_Span& span : spans)
					{
						//不相交的区间跳过
						if (span.end <= line_base || span.begin >= line_base + line.size())
							continue;
						//换算为行内偏移并夹到行范围、对齐字符边界
						std::size_t local_begin = span.begin > line_base ? span.begin - line_base : 0;
						std::size_t local_end = span.end > line_base ? span.end - line_base : 0;
						if (local_end > line.size())
							local_end = line.size();
						cuts.push_back(utf8_align_forward(line, local_begin));
						cuts.push_back(utf8_align_forward(line, local_end));
					}
					//排序去重后逐段绘制
					std::sort(cuts.begin(), cuts.end());
					cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
					for (std::size_t index = 0; index + 1 < cuts.size(); ++index)
					{
						const std::size_t seg_begin = cuts[index];
						const std::size_t seg_end = cuts[index + 1];
						//空段跳过
						if (seg_end <= seg_begin)
							continue;
						//区间按加入顺序覆盖（后者优先）
						Ui_Color segment_color = color;
						uint64_t segment_font = font_handle;
						for (const Ui_Text_Span& span : spans)
						{
							if (span.begin <= line_base + seg_begin && span.end >= line_base + seg_end)
							{
								segment_color = with_opacity(span.color, opacity);
								//区间字体非 0 时接管本段
								if (span.font != 0)
									segment_font = span.font;
							}
						}
						const std::string segment = line.substr(seg_begin, seg_end - seg_begin);
						appended = text_append_spaced(segment_font, segment, line_x, y, letter_spacing,
							segment_color) || appended;
						//按该段字体的字间距推进笔位
						Font_Record* segment_record = font->font_get_mutable(segment_font);
						if (segment_record != nullptr)
							line_x += text_width_spaced(*segment_record, segment, letter_spacing);
					}
				}
			}
			first_line = false;
			y += line_heights[line_index];
		}
		return appended;
	}

	//绘制工具提示（悬停累计 >= 0.5 秒）
	void Ui_Renderer::tooltip_append(const Ui_Node& node, uint64_t font_handle, VkExtent2D extent)
	{
		//未达延时、无文本或字体缺失
		if (!node.hovered || node.hover_time < 0.5f || node.tooltip.empty() || font == nullptr)
			return;
		Font_Record* record = font->font_get_mutable(font_handle);
		if (record == nullptr)
			return;
		//惰性补烘提示文本字形
		font->glyphs_ensure(*record, node.tooltip);
		uint32_t text_width = 0;
		uint32_t text_height = 0;
		Font_Context::text_measure(*record, node.tooltip, text_width, text_height);
		//提示框尺寸（内边距 4）
		const float box_width = static_cast<float>(text_width) + 8.0f;
		const float box_height = static_cast<float>(text_height > 0 ? text_height : record->pixel_height) + 8.0f;
		//默认在控件下方，越界上翻
		float box_x = node.rect.x;
		float box_y = node.rect.y + node.rect.h + 4.0f;
		if (box_y + box_height > static_cast<float>(extent.height))
			box_y = node.rect.y - box_height - 4.0f;
		if (box_x + box_width > static_cast<float>(extent.width))
			box_x = static_cast<float>(extent.width) - box_width;
		if (box_x < 0.0f)
			box_x = 0.0f;
		if (box_y < 0.0f)
			box_y = 0.0f;
		//提示背景
		quad_append(Ui_Rect{ box_x, box_y, box_width, box_height },
			Ui_Color{ 0.10f, 0.10f, 0.14f, 0.95f });
		//提示文本
		text_append(font_handle, node.tooltip, box_x + 4.0f, box_y + 4.0f, 0.0f,
			Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f });
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

		//UI 缩放：几何以逻辑像素生成，绘制时按比例放大到物理像素（第九百七十一轮）
		const float ui_scale = ui.ui_scale_get() > 0.0f ? ui.ui_scale_get() : 1.0f;
		//逻辑尺寸（布局与裁剪均以逻辑像素工作）
		const VkExtent2D logical_extent{
			static_cast<uint32_t>(std::ceil(static_cast<float>(extent.width) / ui_scale)),
			static_cast<uint32_t>(std::ceil(static_cast<float>(extent.height) / ui_scale)) };

		//复位本帧统计
		stats = Ui_Render_Stats{};

		//稳定帧判定：绘制相关字段未变化时复用上一帧几何
		const uint64_t fingerprint = fingerprint_build(ui);
		const bool cache_hit = has_cache && fingerprint == last_fingerprint;
		stats.cache_hits = cache_hit ? 1u : 0u;
		if (!cache_hit)
		{
			//复位本帧 CPU 数据
			cpu_vertices.clear();
			cpu_indices.clear();
			draws.clear();
			text_atlas = 0;

			//UI 全局字体句柄（控件未单独指定时使用）
			const uint64_t font_handle = ui.font_get();

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
		//窗口裁剪矩形
		const Ui_Rect window_clip{ 0.0f, 0.0f,
			static_cast<float>(logical_extent.width), static_cast<float>(logical_extent.height) };
		//各槽位的继承裁剪（下标 = 句柄 - 1；父先于子，故一次正序即可传播）
		inherited_clips.assign(count, window_clip);
		//各槽位继承的圆角裁剪（矩形 + 半径；第九百七十二轮）
		inherited_round_rects.assign(count, window_clip);
		inherited_round_radii.assign(count, 0.0f);
		for (std::size_t index = 0; index < count; ++index)
		{
			const Ui_Node* node = ui.node_at(index);
			//跳过空槽与已销毁节点（其子节点随销毁一并失效）
			if (node == nullptr)
				continue;

			//自身矩形与窗口取交
			const Ui_Rect own_clip = rect_intersect(node->rect, window_clip);
			//父继承裁剪（根节点或无父节点时按窗口）
			Ui_Rect inherit_clip = window_clip;
			//父级圆角裁剪（默认无）
			Ui_Rect round_rect = window_clip;
			float round_radius = 0.0f;
			if (node->parent != 0 && node->parent <= count)
			{
				const std::size_t parent_index = static_cast<std::size_t>(node->parent - 1);
				inherit_clip = inherited_clips[parent_index];
				round_rect = inherited_round_rects[parent_index];
				round_radius = inherited_round_radii[parent_index];
			}
			//有效裁剪 = 自身矩形 ∩ 继承裁剪
			const Ui_Rect clip = rect_intersect(own_clip, inherit_clip);
			//裁剪开关开启时，子节点继续承受本节点有效裁剪；否则仅向下传递已有继承
			inherited_clips[index] = node->clip_children ? clip : inherit_clip;
			//圆角裁剪：开启裁剪且带圆角的容器把自身圆角传递给子节点
			const bool round_clip = node->clip_children && node->corner_radius > 0.0f;
			if (round_clip)
			{
				inherited_round_rects[index] = node->rect;
				inherited_round_radii[index] = std::min(node->corner_radius,
					std::min(node->rect.w, node->rect.h) * 0.5f);
			}
			else
			{
				inherited_round_rects[index] = round_rect;
				inherited_round_radii[index] = round_radius;
			}

			//跳过不可见节点与根节点（其裁剪仍已向下传递）
			if (!node->visible || node->type == Ui_Node_Type::Root)
				continue;
			//裁剪区域为空则不绘制（含文字溢出被完全裁掉的情形）
			if (clip.w <= 0.0f || clip.h <= 0.0f)
				continue;
			//整体落在父级圆角切除区：跳过绘制
			if (rect_in_rounded_corner(clip, round_rect, round_radius))
				continue;
			//本控件后续几何按此裁剪
			current_clip = clip;
			//继承父级圆角裁剪（第九百七十五轮：着色器按圆角丢弃片元）
			current_round_rect = round_rect;
			current_round_radius = round_radius;

			//控件专用字体（0 回退全局字体）与字体高度
			const uint64_t node_font = node->text_font != 0 ? node->text_font : font_handle;
			const Font_Record* node_record = (font != nullptr && node_font != 0) ? font->font_get(node_font) : nullptr;
			const float node_text_height = node_record != nullptr
				? static_cast<float>(node_record->pixel_height) : 0.0f;

			switch (node->type)
			{
			//面板：矩形背景（样式角色启用时取主题色）
			case Ui_Node_Type::Panel:
			{
				const Ui_Color panel_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				//纹理面板：复用九宫格纹理路径（颜色作着色）
				if (node->texture != 0)
				{
					image_slice_append(node->texture, node->rect, node->slice_left, node->slice_top,
						node->slice_right, node->slice_bottom, with_opacity(panel_color, node->opacity));
					break;
				}
				//圆角与描边由背景辅助处理（半径 0 时退回直角路径）
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(panel_color, node->opacity));
				break;
			}
			//纯文本标签：多行、自动换行与垂直对齐（单行默认居中，保持既有观感）
			case Ui_Node_Type::Label:
			{
				//内容区按内边距内缩（第九百四十七轮）
				Ui_Rect content_rect = node->rect;
				const float inset = node->padding > 0.0f ? node->padding : 0.0f;
				if (inset > 0.0f)
				{
					content_rect.x += inset;
					content_rect.y += inset;
					content_rect.w = content_rect.w - inset * 2.0f > 0.0f
						? content_rect.w - inset * 2.0f : 0.0f;
					content_rect.h = content_rect.h - inset * 2.0f > 0.0f
						? content_rect.h - inset * 2.0f : 0.0f;
				}
				//选中高亮（第九百六十二轮）
				if (node->selected)
				{
					Ui_Color highlight = ui.theme_get().accent;
					highlight.a = 0.25f;
					quad_append(content_rect, with_opacity(highlight, node->opacity));
				}
				//文本阴影：先画偏移副本（第九百四十轮；第九百六十八轮彩色阴影继承区间色）
				if (node->text_shadow.color.a > 0.0f)
				{
					Ui_Rect shadow_rect = content_rect;
					shadow_rect.x += node->text_shadow.offset_x;
					shadow_rect.y += node->text_shadow.offset_y;
					//区间阴影继承各自文字色，透明度取阴影声明值
					std::vector<Ui_Text_Span> shadow_spans = node->text_spans;
					for (Ui_Text_Span& span : shadow_spans)
						span.color.a = node->text_shadow.color.a;
					text_append_block(node_font, node->text, shadow_rect, node->line_height,
						node->letter_spacing, node->text_indent, node->text_wrap, node->text_ellipsis,
						node->text_valign, node->text_halign, shadow_spans, node->opacity,
						with_opacity(node->text_shadow.color, node->opacity));
				}
				//正文
				text_append_block(node_font, node->text, content_rect, node->line_height,
					node->letter_spacing, node->text_indent, node->text_wrap, node->text_ellipsis,
					node->text_valign, node->text_halign, node->text_spans, node->opacity,
					with_opacity(node->text_color, node->opacity));
				//工具提示（第九百六十三轮）
				tooltip_append(*node, node_font, logical_extent);
				break;
			}
			//图片：纹理铺满矩形（不参与交互）
			case Ui_Node_Type::Image:
				//九宫格切分全 0 时退化为整图拉伸
				image_slice_append(node->texture, node->rect, node->slice_left, node->slice_top,
					node->slice_right, node->slice_bottom, with_opacity(node->color, node->opacity));
				break;
			//按钮：背景 + 居中标签
			case Ui_Node_Type::Button:
			{
				//样式角色启用时按状态取主题色；自定义角色沿用亮度调整
				Ui_Color color = node->color;
				if (node->role != Ui_Role::Custom)
				{
					Ui_Visual_State state = Ui_Visual_State::Normal;
					if (!node->enabled)
						state = Ui_Visual_State::Disabled;
					else if (node->pressed)
						state = Ui_Visual_State::Pressed;
					else if (node->hovered)
						state = Ui_Visual_State::Hovered;
					color = ui_theme_color(ui.theme_get(), node->role, state);
				}
				else if (node->pressed)
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
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(color, node->opacity));
				//图标（第九百五十八轮）
				float label_x = node->rect.x;
				float label_width = node->rect.w;
				if (node->icon_texture != 0)
				{
					const float icon_size = node->rect.h - 8.0f > 0.0f ? node->rect.h - 8.0f : 0.0f;
					const Ui_Rect icon_rect{ node->rect.x + 4.0f, node->rect.y + 4.0f, icon_size, icon_size };
					quad_textured_append(node->icon_texture, icon_rect,
						with_opacity(Ui_Color{ 1.0f, 1.0f, 1.0f, 1.0f }, node->opacity));
					label_x = icon_rect.x + icon_rect.w + 4.0f;
					label_width = node->rect.x + node->rect.w - label_x;
					if (label_width < 0.0f)
						label_width = 0.0f;
				}
				//标签垂直居中
				if (!node->label.empty())
					text_append(node_font, node->label, label_x,
						node->rect.y + (node->rect.h - node_text_height) * 0.5f, label_width,
						with_opacity(node->text_color, node->opacity));
				//工具提示（第九百六十三轮）
				tooltip_append(*node, node_font, logical_extent);
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
				const Ui_Color box_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(box_color, node->opacity));
				const float padding = 6.0f;
				const float text_x = node->rect.x + padding;
				const float text_y = node->multiline ? node->rect.y + padding
					: node->rect.y + (node->rect.h - node_text_height) * 0.5f;
				//行推进步长（多行按行高逐行绘制）
				const float line_step = node->line_height > 0.0f ? node->line_height
					: static_cast<float>(node_text_height);
				//按换行拆分文本行（记录每行起始字节偏移；第九百六十六轮）
				std::vector<std::pair<std::size_t, std::string>> text_lines{};
				std::size_t line_begin = 0;
				for (;;)
				{
					const std::size_t newline = node->text.find('\n', line_begin);
					//末行
					if (newline == std::string::npos)
					{
						text_lines.emplace_back(line_begin, node->text.substr(line_begin));
						break;
					}
					text_lines.emplace_back(line_begin, node->text.substr(line_begin, newline - line_begin));
					line_begin = newline + 1;
				}
				//逐行绘制选中高亮与文本
				for (std::size_t line_index = 0; line_index < text_lines.size(); ++line_index)
				{
					const std::size_t line_start = text_lines[line_index].first;
					const std::string& line_text = text_lines[line_index].second;
					const float line_y = text_y + static_cast<float>(line_index) * line_step;
					//选中高亮：取选区与本行的交集（绘制在文本之下）
					if (node_record != nullptr && node->select_end > node->select_begin)
					{
						const std::size_t overlap_begin = node->select_begin > line_start
							? node->select_begin : line_start;
						const std::size_t line_end = line_start + line_text.size();
						const std::size_t overlap_end = node->select_end < line_end
							? node->select_end : line_end;
						//交集非空才绘制
						if (overlap_end > overlap_begin && overlap_end <= node->text.size())
						{
							uint32_t begin_width = 0;
							uint32_t end_width = 0;
							uint32_t measure_height = 0;
							//度量交集两端在本行内的前缀宽度
							Font_Context::text_measure(*node_record,
								line_text.substr(0, overlap_begin - line_start), begin_width, measure_height);
							Font_Context::text_measure(*node_record,
								line_text.substr(0, overlap_end - line_start), end_width, measure_height);
							//宽度为正才绘制
							if (end_width > begin_width)
							{
								Ui_Color highlight = ui.theme_get().accent;
								highlight.a = 0.45f;
								Ui_Rect highlight_rect{ text_x + static_cast<float>(begin_width),
									text_lines.size() == 1 && !node->multiline ? node->rect.y + 3.0f : line_y + 1.0f,
									static_cast<float>(end_width - begin_width),
									text_lines.size() == 1 && !node->multiline ? node->rect.h - 6.0f : line_step - 2.0f };
								quad_append(highlight_rect, with_opacity(highlight, node->opacity));
							}
						}
					}
					//绘制本行文本
					text_append(node_font, line_text, text_x, line_y, 0.0f,
						with_opacity(node->text_color, node->opacity));
				}
				//光标位置夹到串尾（第九百六十六轮按换行数定位所在行）
				const std::size_t caret = node->caret <= node->text.size() ? node->caret : node->text.size();
				std::size_t caret_line = 0;
				for (std::size_t index = 0; index < caret; ++index)
				{
					//统计光标之前的换行数
					if (node->text[index] == '\n')
						caret_line += 1;
				}
				//定位光标行起点
				std::size_t caret_line_start = 0;
				for (std::size_t index = 0; index < caret_line && caret_line_start < node->text.size(); ++index)
					caret_line_start = node->text.find('\n', caret_line_start) + 1;
				//光标行纵坐标
				const float caret_y = text_y + static_cast<float>(caret_line) * line_step;
				//度量光标之前（本行内）的文本宽度
				uint32_t caret_width = 0;
				uint32_t caret_height = 0;
				if (node_record != nullptr && caret > caret_line_start)
					Font_Context::text_measure(*node_record,
						node->text.substr(caret_line_start, caret - caret_line_start), caret_width, caret_height);
				//输入法组合文本绘制在光标处（第九百三十六轮）
				float preedit_width = 0.0f;
				if (!node->preedit.empty())
				{
					Ui_Color preedit_color = ui.theme_get().accent;
					text_append(node_font, node->preedit, text_x + static_cast<float>(caret_width),
						caret_y, 0.0f, with_opacity(preedit_color, node->opacity));
					uint32_t measured_width = 0;
					uint32_t measured_height = 0;
					//度量组合文本宽度，供光标右移
					if (node_record != nullptr)
					{
						Font_Context::text_measure(*node_record, node->preedit,
							measured_width, measured_height);
						preedit_width = static_cast<float>(measured_width);
					}
				}
				//焦点态绘制光标
				if (node->focused)
				{
					Ui_Rect caret_rect{};
					caret_rect.x = text_x + static_cast<float>(caret_width) + preedit_width;
					caret_rect.y = (!node->multiline && text_lines.size() == 1) ? node->rect.y + 4.0f : caret_y + 1.0f;
					caret_rect.w = 2.0f;
					caret_rect.h = (!node->multiline && text_lines.size() == 1) ? node->rect.h - 8.0f : line_step - 2.0f;
					Ui_Color caret_color{ 1.0f, 1.0f, 1.0f, 1.0f };
					quad_append(caret_rect, with_opacity(caret_color, node->opacity));
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
				//悬停 / 按下时方框略亮（样式角色启用时由主题状态色接管）
				Ui_Color box_color = node->color;
				if (node->role != Ui_Role::Custom)
				{
					Ui_Visual_State state = Ui_Visual_State::Normal;
					if (!node->enabled)
						state = Ui_Visual_State::Disabled;
					else if (node->pressed || node->hovered)
						state = Ui_Visual_State::Hovered;
					box_color = ui_theme_color(ui.theme_get(), node->role, state);
				}
				else if (node->pressed || node->hovered)
				{
					box_color.r = std::min(1.0f, box_color.r + 0.20f);
					box_color.g = std::min(1.0f, box_color.g + 0.20f);
					box_color.b = std::min(1.0f, box_color.b + 0.20f);
				}
				background_append(box_rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(box_color, node->opacity));
				//选中态绘制内块
				if (node->checked)
				{
					const float inset = 4.0f;
					Ui_Rect inner_rect{ box_rect.x + inset, box_rect.y + inset,
						box_rect.w - inset * 2.0f, box_rect.h - inset * 2.0f };
					quad_append(inner_rect, with_opacity(focus_color, node->opacity));
				}
				//右侧标签
				if (!node->label.empty())
					text_append(node_font, node->label, box_rect.x + box_rect.w + 6.0f,
						node->rect.y + (node->rect.h - node_text_height) * 0.5f, 0.0f,
						with_opacity(node->text_color, node->opacity));
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//单选按钮：圆形空心环 + 选中圆点 + 右侧标签
			case Ui_Node_Type::Radio:
			{
				//圆形直径取控件高度上限 20 像素，垂直居中
				const float box_size = std::min(node->rect.h, 20.0f);
				Ui_Rect box_rect{ node->rect.x, node->rect.y + (node->rect.h - box_size) * 0.5f,
					box_size, box_size };
				//选中环取强调色，未选中环取节点颜色（默认中灰）
				const Ui_Color ring_color = node->checked
					? ui.theme_get().accent
					: (node->role == Ui_Role::Custom ? node->color
						: ui_theme_color(ui.theme_get(), node->role,
							node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled));
				//深色内底 + 1.5 像素环
				background_append(box_rect, box_size * 0.5f, 1.5f,
					with_opacity(ring_color, node->opacity),
					with_opacity(Ui_Color{ 0.02f, 0.02f, 0.05f, 0.95f }, node->opacity));
				//选中：中心实心圆点
				if (node->checked)
				{
					const float dot_size = box_size * 0.5f;
					Ui_Rect dot_rect{ box_rect.x + (box_size - dot_size) * 0.5f,
						box_rect.y + (box_size - dot_size) * 0.5f, dot_size, dot_size };
					rounded_quad_append(dot_rect, dot_size * 0.5f,
						with_opacity(ui.theme_get().accent, node->opacity));
				}
				//右侧标签
				if (!node->label.empty())
					text_append(node_font, node->label, box_rect.x + box_rect.w + 6.0f,
						node->rect.y + (node->rect.h - node_text_height) * 0.5f, 0.0f,
						with_opacity(node->text_color, node->opacity));
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//开关：圆角轨道 + 左右滑块
			case Ui_Node_Type::Switch:
			{
				//轨道高取控件高度上限 20 像素，宽度铺满矩形
				const float track_h = std::min(node->rect.h, 20.0f);
				Ui_Rect track_rect{ node->rect.x, node->rect.y + (node->rect.h - track_h) * 0.5f,
					node->rect.w, track_h };
				//未选中轨道用节点颜色，选中取强调色
				const Ui_Color track_color = node->checked ? ui.theme_get().accent
					: (node->role == Ui_Role::Custom ? node->color
						: ui_theme_color(ui.theme_get(), node->role,
							node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled));
				rounded_quad_append(track_rect, track_h * 0.5f,
					with_opacity(track_color, node->opacity));
				//滑块直径取轨道高减 4，选中靠右、未选中靠左
				const float knob_size = std::max(track_h - 4.0f, 2.0f);
				const float knob_x = node->checked
					? track_rect.x + track_rect.w - knob_size - 2.0f
					: track_rect.x + 2.0f;
				Ui_Rect knob_rect{ knob_x, track_rect.y + (track_h - knob_size) * 0.5f,
					knob_size, knob_size };
				rounded_quad_append(knob_rect, knob_size * 0.5f,
					with_opacity(Ui_Color{ 0.95f, 0.95f, 0.98f, 1.0f }, node->opacity));
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//列表控件：背景 + 等分行 + 当前项高亮 + 行文本
			case Ui_Node_Type::List_Box:
			{
				const Ui_Color box_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(box_color, node->opacity));
				const std::size_t count = node->combo_items.size();
				//空列表只有背景
				if (count > 0)
				{
					const float row_height = node->rect.h / static_cast<float>(count);
					for (std::size_t index = 0; index < count; ++index)
					{
						const Ui_Rect row_rect{ node->rect.x,
							node->rect.y + row_height * static_cast<float>(index),
							node->rect.w, row_height };
						//当前项高亮
						if (index == node->combo_index)
						{
							Ui_Color highlight = ui.theme_get().accent;
							highlight.a = 0.35f;
							quad_append(row_rect, with_opacity(highlight, node->opacity));
						}
						text_append(node_font, node->combo_items[index],
							row_rect.x + 6.0f,
							row_rect.y + (row_rect.h - node_text_height) * 0.5f,
							row_rect.w - 12.0f, with_opacity(node->text_color, node->opacity));
					}
				}
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//滑动条：轨道 + 进度填充 + 滑块
			case Ui_Node_Type::Slider:
			{
				//纵向判定：滚动条看绑定声明，普通滑动条看节点方向
				const bool scroll_linked = node->scroll_target != 0;
				const bool vertical = scroll_linked ? node->scroll_vertical : node->vertical;
				//普通纵向滑动条自下而上（滚动条保持自上而下）
				const bool reversed = vertical && !scroll_linked;
				//轨道（纵向高 4 像素宽 / 横向宽 4 像素高；样式角色启用时取主题色）
				const Ui_Rect track_rect = vertical
					? Ui_Rect{ node->rect.x + node->rect.w * 0.5f - 2.0f, node->rect.y, 4.0f, node->rect.h }
					: Ui_Rect{ node->rect.x, node->rect.y + node->rect.h * 0.5f - 2.0f, node->rect.w, 4.0f };
				const Ui_Color track_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				quad_append(track_rect, with_opacity(track_color, node->opacity));
				//已选进度
				if (vertical)
				{
					const float fill_height = node->rect.h * node->display_value;
					if (fill_height > 0.0f)
					{
						//自下而上时填充贴底
						const float fill_y = reversed
							? node->rect.y + node->rect.h - fill_height
							: node->rect.y;
						quad_append(Ui_Rect{ track_rect.x, fill_y, 4.0f, fill_height },
							with_opacity(focus_color, node->opacity));
					}
				}
				else
				{
					const float fill_width = node->rect.w * node->display_value;
					if (fill_width > 0.0f)
						quad_append(Ui_Rect{ node->rect.x, track_rect.y, fill_width, 4.0f },
							with_opacity(focus_color, node->opacity));
				}
				//滑块（横向 10 × 18，纵向 18 × 10，居中于当前值）
				const Ui_Rect knob_rect = vertical
					? Ui_Rect{ node->rect.x + node->rect.w * 0.5f - 9.0f,
						node->rect.y + node->rect.h * (reversed ? 1.0f - node->display_value : node->display_value) - 5.0f,
						18.0f, 10.0f }
					: Ui_Rect{ node->rect.x + node->rect.w * node->display_value - 5.0f,
						node->rect.y + node->rect.h * 0.5f - 9.0f, 10.0f, 18.0f };
				Ui_Color knob_color{ 0.85f, 0.88f, 0.95f, 1.0f };
				if (node->pressed || node->hovered)
				{
					knob_color.r = 1.0f;
					knob_color.g = 1.0f;
					knob_color.b = 1.0f;
				}
				rounded_quad_append(knob_rect, node->corner_radius, with_opacity(knob_color, node->opacity));
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				//系统焦点环
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			//进度条：背景 + 按值填充前景 + 可选居中标签
			case Ui_Node_Type::Progress:
			{
				//背景按角色取主题色（Custom 用节点颜色）
				const Ui_Color background_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(background_color, node->opacity));
				//前景按取值比例（0..1 夹紧）填充；纵向自底向上
				const float ratio = node->display_value < 0.0f ? 0.0f
					: (node->display_value > 1.0f ? 1.0f : node->display_value);
				if (ratio > 0.0f)
				{
					const Ui_Rect fill = node->vertical
						? Ui_Rect{ node->rect.x, node->rect.y + node->rect.h * (1.0f - ratio),
							node->rect.w, node->rect.h * ratio }
						: Ui_Rect{ node->rect.x, node->rect.y, node->rect.w * ratio, node->rect.h };
					rounded_quad_append(fill, node->corner_radius,
						with_opacity(ui.theme_get().accent, node->opacity));
				}
				//居中标签
				if (!node->label.empty())
					text_append(node_font, node->label, node->rect.x,
						node->rect.y + (node->rect.h - node_text_height) * 0.5f, node->rect.w,
						with_opacity(node->text_color, node->opacity));
				break;
			}
			//下拉选择框：背景 + 当前项 + 箭头 + 展开列表（第九百六十五轮）
			case Ui_Node_Type::Combo:
			{
				const Ui_Color box_color = node->role == Ui_Role::Custom ? node->color
					: ui_theme_color(ui.theme_get(), node->role,
						node->enabled ? Ui_Visual_State::Normal : Ui_Visual_State::Disabled);
				background_append(node->rect, node->corner_radius, node->border_width,
					with_opacity(node->border_color, node->opacity),
					with_opacity(box_color, node->opacity));
				//当前项文本
				if (node->combo_index < node->combo_items.size())
					text_append(node_font, node->combo_items[node->combo_index],
						node->rect.x + 6.0f, node->rect.y + (node->rect.h - node_text_height) * 0.5f,
						node->rect.w - 20.0f, with_opacity(node->text_color, node->opacity));
				//右侧箭头（小方块）
				quad_append(Ui_Rect{ node->rect.x + node->rect.w - 14.0f,
					node->rect.y + node->rect.h * 0.5f - 2.0f, 8.0f, 4.0f },
					with_opacity(node->text_color, node->opacity));
				//展开列表（最多 8 行可视，按滚动行偏移；第九百八十轮）
				if (node->combo_open)
				{
					constexpr std::size_t VISIBLE_ROWS = 8;
					const std::size_t visible = node->combo_items.size() < VISIBLE_ROWS
						? node->combo_items.size() : VISIBLE_ROWS;
					for (std::size_t row = 0; row < visible; ++row)
					{
						const std::size_t index = node->combo_scroll + row;
						//越界行不绘制
						if (index >= node->combo_items.size())
							break;
						const Ui_Rect row_rect{ node->rect.x,
							node->rect.y + node->rect.h * static_cast<float>(row + 1),
							node->rect.w, node->rect.h };
						quad_append(row_rect,
							with_opacity(Ui_Color{ 0.12f, 0.12f, 0.16f, 0.98f }, node->opacity));
						//选中行高亮
						if (index == node->combo_index)
						{
							Ui_Color highlight = ui.theme_get().accent;
							highlight.a = 0.35f;
							quad_append(row_rect, with_opacity(highlight, node->opacity));
						}
						text_append(node_font, node->combo_items[index],
							row_rect.x + 6.0f, row_rect.y + (row_rect.h - node_text_height) * 0.5f,
							row_rect.w - 12.0f, with_opacity(node->text_color, node->opacity));
					}
				}
				//焦点环
				if (node->focused)
					focus_ring(node->rect);
				if (node->system_focused)
					system_focus_ring(node->rect);
				break;
			}
			default:
				break;
			}
		}
			//几何重建完成，记录指纹
			last_fingerprint = fingerprint;
			has_cache = true;
		}

		//记录本帧几何统计
		stats.vertices = static_cast<uint32_t>(cpu_vertices.size());
		stats.indices = static_cast<uint32_t>(cpu_indices.size());
		stats.draws = static_cast<uint32_t>(draws.size());
		for (const Draw_Record& record : draws)
		{
			//文字记录同时计入纹理分类
			if (record.text)
				++stats.text_draws;
			//按是否使用纹理分类
			if (record.text || record.texture != 0)
				++stats.texture_draws;
			else
				++stats.solid_draws;
		}

		//无任何绘制内容
		if (cpu_indices.empty())
			return;

		//确保帧槽缓冲并上传
		Slot_Buffers& buffers = slots[slot];
		if (!slot_ensure(buffers, static_cast<uint32_t>(cpu_vertices.size()), static_cast<uint32_t>(cpu_indices.size())))
			return;
		//该槽已有相同内容时跳过上传
		if (buffers.uploaded && buffers.uploaded_fingerprint == fingerprint)
			stats.upload_skips = 1;
		else
		{
			std::memcpy(buffers.vertex_mapped, cpu_vertices.data(), cpu_vertices.size() * sizeof(Vertex));
			std::memcpy(buffers.index_mapped, cpu_indices.data(), cpu_indices.size() * sizeof(uint32_t));
			buffers.uploaded_fingerprint = fingerprint;
			buffers.uploaded = true;
		}

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

		//推送常量（像素坐标 → NDC 矩阵 + 颜色 + 圆角裁剪）
		struct Push_Constant
		{
			float transform[16];
			float color[4];
			//圆角裁剪矩形（逻辑像素：x / y / w / h）
			float clip_rect[4];
			//圆角参数（x = 半径，y = 启用标记，z = UI 缩放，w = 保留）
			float clip_round[4];
		};
		/*
		像素坐标 → NDC 的仿射矩阵（列主序）
		Vulkan 视口把 NDC y 映射为「向下递增」的帧缓冲行号，故**不做 Y 翻转**：
		x' = 2 / 宽 × px - 1；y' = 2 / 高 × py - 1。
		*/
		Push_Constant push{};
		const float width = static_cast<float>(extent.width);
		const float height = static_cast<float>(extent.height);
		push.transform[0] = 2.0f * ui_scale / width;
		push.transform[5] = 2.0f * ui_scale / height;
		push.transform[10] = 1.0f;
		push.transform[12] = -1.0f;
		push.transform[13] = -1.0f;
		push.transform[15] = 1.0f;

		//当前已绑定管线（0 = 未绑定；1 = 纯色；2 = 文字）
		int active_pipeline = 0;
		//逐条绘制
		for (const Draw_Record& record : draws)
		{
			//按控件裁剪矩形设置 scissor（逻辑像素换算到物理像素，向外取整）
			VkRect2D scissor{};
			scissor.offset = { static_cast<int32_t>(std::floor(record.clip.x * ui_scale)),
				static_cast<int32_t>(std::floor(record.clip.y * ui_scale)) };
			scissor.extent = { static_cast<uint32_t>(std::ceil(record.clip.w * ui_scale)),
				static_cast<uint32_t>(std::ceil(record.clip.h * ui_scale)) };
			//零面积裁剪跳过该绘制
			if (scissor.extent.width == 0 || scissor.extent.height == 0)
				continue;
			vkCmdSetScissor(command_buffer, 0, 1, &scissor);

			//纹理绘制（文字或图片）
			if (record.text || record.texture != 0)
			{
				//无纹理管线或纹理句柄无效则跳过
				if (text_vk == VK_NULL_HANDLE || record.texture == 0 || texture == nullptr)
					continue;
				//纹理变化时重写 UI 专用描述符集
				if (buffers.bound_atlas != record.texture)
				{
					VkImageView view = VK_NULL_HANDLE;
					VkSampler sampler = VK_NULL_HANDLE;
					if (texture->texture_bind_get(record.texture, view, sampler))
					{
						pipeline->ui_texture_bind(slot, sampler, view);
						buffers.bound_atlas = record.texture;
						++stats.texture_binds;
					}
				}
				//取 UI 专用采样集
				const VkDescriptorSet ui_set = pipeline->ui_set_get(slot);
				if (ui_set == VK_NULL_HANDLE)
					continue;
				//切换管线
				if (active_pipeline != 2)
				{
					vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, text_vk);
					active_pipeline = 2;
				}
				/*
				每条纹理绘制都重绑描述符集
				同帧内不同控件可能使用不同字体 / 图片纹理，描述符集内容是逐条重写的；
				已绑定的描述符集在重写后必须重新绑定才会生效。
				*/
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
					1, 1, &ui_set, 0, nullptr);
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

			//写入颜色与圆角裁剪并绘制（第九百七十五轮）
			push.color[0] = record.color.r;
			push.color[1] = record.color.g;
			push.color[2] = record.color.b;
			push.color[3] = record.color.a;
			push.clip_rect[0] = record.clip_round.x;
			push.clip_rect[1] = record.clip_round.y;
			push.clip_rect[2] = record.clip_round.w;
			push.clip_rect[3] = record.clip_round.h;
			push.clip_round[0] = record.clip_round_radius;
			push.clip_round[1] = record.clip_round_radius > 0.0f ? 1.0f : 0.0f;
			push.clip_round[2] = ui_scale;
			push.clip_round[3] = 0.0f;
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
