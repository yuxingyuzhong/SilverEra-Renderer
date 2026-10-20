#include "src/render/Text/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	UTF-8 解码
	自 text[offset] 起解出一个码点；成功推进 offset 并写出码点，失败只跳过 1 字节（不中断整串）。
	覆盖 1~4 字节序列；非法首字节 / 续字节格式不符 / 尾部截断均按「跳过 1 字节」处理。
	*/
	static bool utf8_decode(const std::string& text, size_t& offset, uint32_t& codepoint_out)
	{
		//无剩余字节
		if (offset >= text.size())
			return false;
		//首字节
		uint8_t first = static_cast<uint8_t>(text[offset]);
		//单字节（ASCII）
		if (first < 0x80)
		{
			codepoint_out = first;
			++offset;
			return true;
		}
		//码点累加值与后续字节数
		uint32_t codepoint = 0;
		uint32_t continuation_count = 0;
		//两字节（110xxxxx）
		if ((first & 0xE0) == 0xC0)
		{
			codepoint = first & 0x1F;
			continuation_count = 1;
		}
		//三字节（1110xxxx）
		else if ((first & 0xF0) == 0xE0)
		{
			codepoint = first & 0x0F;
			continuation_count = 2;
		}
		//四字节（11110xxx）
		else if ((first & 0xF8) == 0xF0)
		{
			codepoint = first & 0x07;
			continuation_count = 3;
		}
		//非法首字节（含续字节 10xxxxxx 作首字节）
		else
		{
			++offset;
			return false;
		}
		//若后续字节不足（截断）
		if (offset + continuation_count >= text.size())
		{
			++offset;
			return false;
		}
		//逐续字节拼接（10xxxxxx）
		for (uint32_t index = 1; index <= continuation_count; ++index)
		{
			//本续字节
			uint8_t next = static_cast<uint8_t>(text[offset + index]);
			//若续字节格式不符
			if ((next & 0xC0) != 0x80)
			{
				++offset;
				return false;
			}
			codepoint = (codepoint << 6) | (next & 0x3F);
		}
		//推进读取位置并输出码点
		offset += static_cast<size_t>(continuation_count) + 1;
		codepoint_out = codepoint;
		return true;
	}

	/*
	把 UTF-8 文本解码为去重码点序列
	解码失败字节被跳过（与几何生成同口径）；返回是否至少解出一个码点。
	*/
	bool Font_Context::text_codepoints(const std::string& text, std::vector<uint32_t>& codepoints_out)
	{
		//清空出参
		codepoints_out.clear();
		//读取位置
		size_t offset = 0;
		while (offset < text.size())
		{
			//本码点
			uint32_t codepoint = 0;
			//解码失败则已跳过 1 字节，直接继续
			if (!utf8_decode(text, offset, codepoint))
				continue;
			//去重后收集
			if (std::find(codepoints_out.begin(), codepoints_out.end(), codepoint) == codepoints_out.end())
				codepoints_out.push_back(codepoint);
		}
		return !codepoints_out.empty();
	}

	//度量单行文本（宽 = 末个有墨迹字形的右缘，故不含尾随空白；高 = 字体像素高度）
	bool Font_Context::text_measure(const Font_Record& record, const std::string& text,
		uint32_t& width_out, uint32_t& height_out)
	{
		//默认零度量
		width_out = 0;
		height_out = 0;
		//前置：字体记录必须有效
		if (!record.valid)
			return false;

		//笔位（相对文本原点）
		float pen_x = 0.0f;
		//末个有墨迹字形的右缘（尾随空白不推高该值，故不计入宽度）
		float ink_right = 0.0f;
		//本串是否至少解出一个可呈现字形
		bool any_glyph = false;
		//读取位置
		size_t offset = 0;
		while (offset < text.size())
		{
			//本码点
			uint32_t codepoint = 0;
			//解码失败则已跳过 1 字节，直接继续
			if (!utf8_decode(text, offset, codepoint))
				continue;
			//取字形度量（尚未烘焙 / 不支持则跳过）
			const Font_Glyph* glyph = glyph_seek(record, codepoint);
			if (glyph == nullptr)
				continue;
			any_glyph = true;
			//推进笔位
			pen_x += glyph->advance;
			//有墨迹的字形才刷新右缘
			if (glyph->has_ink && glyph->x1 > glyph->x0)
				ink_right = pen_x;
		}
		//无任何可呈现字形则视为零度量
		if (!any_glyph || ink_right <= 0.0f)
			return false;
		//四舍五入取整（+0.5 后截断）
		width_out = static_cast<uint32_t>(ink_right + 0.5f);
		height_out = record.pixel_height;
		return true;
	}

	/*
	按布局生成文字几何
	两个阶段：① 逐字符切行（显式 '\n' 与 max_width 超限）② 逐行按对齐平移并在行顶偏移下产出四边形。
	*/
	bool Font_Context::text_layout_build(Mesh_Data& data_out, const Font_Record& record, const std::string& text,
		const Text_Layout& layout)
	{
		//先清空出参（失败时不留下任何几何）
		data_out.vertices.clear();
		data_out.indices.clear();
		//前置：字体记录必须有效
		if (!record.valid)
			return false;

		//行距（0 表取字体像素高度）
		float line_height = layout.line_height > 0.0f
			? layout.line_height : static_cast<float>(record.pixel_height);
		//换行宽度上限（0 表不换行）
		float max_width = layout.max_width > 0.0f ? layout.max_width : 0.0f;
		//字距（计入行宽，故参与换行与对齐）
		float tracking = layout.tracking;

		//按行收集字形（每行一个数组）
		std::vector<std::vector<const Font_Glyph*>> lines;
		lines.emplace_back();
		//各行行宽（advance 之和）
		std::vector<float> line_widths;
		//当前行已累计的推进量
		float line_advance = 0.0f;
		//读取位置
		size_t offset = 0;
		while (offset < text.size())
		{
			//本码点
			uint32_t codepoint = 0;
			//解码失败则已跳过 1 字节，直接继续
			if (!utf8_decode(text, offset, codepoint))
				continue;
			//显式换行：收束当前行（空行也保留，形成垂直间距）
			if (codepoint == '\n')
			{
				line_widths.push_back(line_advance);
				lines.emplace_back();
				line_advance = 0.0f;
				continue;
			}
			//取字形度量（尚未烘焙 / 不支持则跳过，不推进笔位）
			const Font_Glyph* glyph = glyph_seek(record, codepoint);
			if (glyph == nullptr)
				continue;
			//换行判定：当前行非空且再加入本字形会超限（单字形超宽时独占一行，不在此断行）
			if (max_width > 0.0f && !lines.back().empty() && line_advance + glyph->advance + tracking > max_width)
			{
				line_widths.push_back(line_advance);
				lines.emplace_back();
				line_advance = 0.0f;
			}
			//收容本字形
			lines.back().push_back(glyph);
			line_advance += glyph->advance + tracking;
		}
		//收束最后一行
		line_widths.push_back(line_advance);

		//本串是否至少产出过一个可见字形
		bool any_ink = false;
		//逐行产出
		for (size_t line_index = 0; line_index < lines.size(); ++line_index)
		{
			//本行字形
			const std::vector<const Font_Glyph*>& line = lines[line_index];
			//本行顶部相对文本原点的纵向偏移
			float line_top = static_cast<float>(line_index) * line_height;
			//本行起始笔位（左对齐为 0；居中 / 右对齐按 max_width 平移，负值钳到 0）
			float pen_x = 0.0f;
			//仅在设定换行宽度时对齐才有基准
			if (max_width > 0.0f)
			{
				//本行行宽
				float line_width = line_widths[line_index];
				//居中：两侧留白均分
				if (layout.align == Text_Align::Center)
					pen_x = (max_width - line_width) * 0.5f;
				//右对齐：右端贴齐上限
				else if (layout.align == Text_Align::Right)
					pen_x = max_width - line_width;
				//行宽超过上限（单字形超宽）时不向左溢出
				if (pen_x < 0.0f)
					pen_x = 0.0f;
			}
			//首行缩进：仅第一行整体右移（不计入行宽与换行判据）
			if (line_index == 0)
				pen_x += layout.first_line_indent;
			//逐字形产出四边形
			for (const Font_Glyph* glyph : line)
			{
				//有可见像素才产出四边形（空白字符只推进笔位）
				if (glyph->has_ink && glyph->x1 > glyph->x0 && glyph->y1 > glyph->y0)
				{
					any_ink = true;
					//本字形的顶点基址
					uint32_t base = static_cast<uint32_t>(data_out.vertices.size());
					//四角（顺序与既有四边形一致：左上 → 右上 → 右下 → 左下）
					Vertex top_left{};
					top_left.position[0] = pen_x + glyph->x0;
					top_left.position[1] = line_top + glyph->y0;
					top_left.position[2] = 0.0f;
					top_left.uv[0] = glyph->u0;
					top_left.uv[1] = glyph->v0;

					Vertex top_right{};
					top_right.position[0] = pen_x + glyph->x1;
					top_right.position[1] = line_top + glyph->y0;
					top_right.position[2] = 0.0f;
					top_right.uv[0] = glyph->u1;
					top_right.uv[1] = glyph->v0;

					Vertex bottom_right{};
					bottom_right.position[0] = pen_x + glyph->x1;
					bottom_right.position[1] = line_top + glyph->y1;
					bottom_right.position[2] = 0.0f;
					bottom_right.uv[0] = glyph->u1;
					bottom_right.uv[1] = glyph->v1;

					Vertex bottom_left{};
					bottom_left.position[0] = pen_x + glyph->x0;
					bottom_left.position[1] = line_top + glyph->y1;
					bottom_left.position[2] = 0.0f;
					bottom_left.uv[0] = glyph->u0;
					bottom_left.uv[1] = glyph->v1;

					//追加四顶点
					data_out.vertices.push_back(top_left);
					data_out.vertices.push_back(top_right);
					data_out.vertices.push_back(bottom_right);
					data_out.vertices.push_back(bottom_left);
					//追加两三角形（绕序取「像素空间 Y 向下」下与既有管线正面判定一致的一种）
					data_out.indices.push_back(base + 0);
					data_out.indices.push_back(base + 2);
					data_out.indices.push_back(base + 1);
					data_out.indices.push_back(base + 0);
					data_out.indices.push_back(base + 3);
					data_out.indices.push_back(base + 2);
				}
				//推进笔位（含字距）
				pen_x += glyph->advance + tracking;
			}
		}

		//无任何可见字形则视为无几何
		if (!any_ink)
		{
			data_out.vertices.clear();
			data_out.indices.clear();
			return false;
		}
		return true;
	}

	//生成字符串的文字几何（单行、左对齐；等价于默认布局）
	bool Font_Context::text_geometry_build(Mesh_Data& data_out, const Font_Record& record, const std::string& text)
	{
		//默认布局：不换行、行高取字体像素高度、左对齐
		return text_layout_build(data_out, record, text, Text_Layout{});
	}
}
