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

	//生成字符串的文字几何
	bool Font_Context::text_geometry_build(Mesh_Data& data_out, const Font_Record& record, const std::string& text)
	{
		//先清空出参（失败时不留下任何几何）
		data_out.vertices.clear();
		data_out.indices.clear();
		//前置：字体记录必须有效
		if (!record.valid)
			return false;

		//笔位（相对文本原点，像素；Y 向下）
		float pen_x = 0.0f;
		//本串是否至少产出过一个可见字形
		bool any_ink = false;
		//读取位置
		size_t offset = 0;
		while (offset < text.size())
		{
			//本码点
			uint32_t codepoint = 0;
			//解码失败则已跳过 1 字节，直接继续
			if (!utf8_decode(text, offset, codepoint))
				continue;
			//取字形度量（超出支持范围则跳过该码点，仅不推进笔位）
			const Font_Glyph* glyph = glyph_seek(record, codepoint);
			if (glyph == nullptr)
				continue;

			//有可见像素才产出四边形（空白字符只推进笔位）
			if (glyph->has_ink && glyph->x1 > glyph->x0 && glyph->y1 > glyph->y0)
			{
				any_ink = true;
				//本字形的顶点基址
				uint32_t base = static_cast<uint32_t>(data_out.vertices.size());
				//四角（顺序与既有四边形一致：左上 → 右上 → 右下 → 左下）
				Vertex top_left{};
				top_left.position[0] = pen_x + glyph->x0;
				top_left.position[1] = glyph->y0;
				top_left.position[2] = 0.0f;
				top_left.uv[0] = glyph->u0;
				top_left.uv[1] = glyph->v0;

				Vertex top_right{};
				top_right.position[0] = pen_x + glyph->x1;
				top_right.position[1] = glyph->y0;
				top_right.position[2] = 0.0f;
				top_right.uv[0] = glyph->u1;
				top_right.uv[1] = glyph->v0;

				Vertex bottom_right{};
				bottom_right.position[0] = pen_x + glyph->x1;
				bottom_right.position[1] = glyph->y1;
				bottom_right.position[2] = 0.0f;
				bottom_right.uv[0] = glyph->u1;
				bottom_right.uv[1] = glyph->v1;

				Vertex bottom_left{};
				bottom_left.position[0] = pen_x + glyph->x0;
				bottom_left.position[1] = glyph->y1;
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

			//推进笔位
			pen_x += glyph->advance;
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
}
