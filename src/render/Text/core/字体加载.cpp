#include "src/render/Text/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//纹理模块完整定义（字形图集的创建与销毁）
#include "src/render/Texture/纹理.h"

//文件读取
#include <fstream>
//std::floor
#include <cmath>
/*
stb_truetype 实现（**全层唯一**定义 STB_TRUETYPE_IMPLEMENTATION 的编译单元）
第三方头以警告抑制包裹包含：本层要求「零本层新增警告」，第三方库自身的告警不属本层代码问题。
*/
#define STB_TRUETYPE_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable : 4244 4456 4457 4701 4996)
#include "external/stb/stb_truetype.h"
#pragma warning(pop)

namespace engine
{
	//析构
	Font_Context::~Font_Context(void)
	{
		//归还全部字形图集纹理
		destroy();
	}

	//构建（记录纹理模块）
	bool Font_Context::build(Texture_Context& texture)
	{
		//前置：纹理模块必须可用（字形图集经它上传）
		if (!texture.valid())
		{
			logger.error("字体:: 构建时纹理模块不可用");
			return false;
		}
		this->texture = &texture;
		logger.info("字体:: 上下文构建完成");
		return true;
	}

	//是否已构建完成
	bool Font_Context::valid(void) const
	{
		return texture != nullptr;
	}

	//销毁（幂等）
	void Font_Context::destroy(void)
	{
		//逐条归还字形图集纹理
		for (Font_Record& record : records)
		{
			//纹理模块就绪且图集有效时归还
			if (texture != nullptr && record.atlas_texture != 0)
				texture->texture_destroy(record.atlas_texture);
			//整条记录复位
			record = Font_Record{};
		}
		records.clear();
		texture = nullptr;
	}

	//按码点取字形度量
	const Font_Glyph* Font_Context::glyph_seek(const Font_Record& record, uint32_t codepoint)
	{
		//超出支持范围则无度量
		if (codepoint < FONT_CODEPOINT_FIRST || codepoint > FONT_CODEPOINT_LAST)
			return nullptr;
		return &record.glyphs[codepoint - FONT_CODEPOINT_FIRST];
	}

	//从 TTF 文件加载字体
	bool Font_Context::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
	{
		//默认无有效句柄
		handle_out = 0;
		//前置：模块与字号校验
		if (!valid())
		{
			logger.error("字体:: 加载字体时模块尚未构建");
			return false;
		}
		if (pixel_height == 0)
		{
			logger.error("字体:: 字号必须为正");
			return false;
		}

		//以二进制方式打开并定位到末尾取文件长度
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		//若文件不存在或不可读
		if (!file)
		{
			logger.error("字体:: 打不开字体文件 {}", path);
			return false;
		}
		//文件字节数
		std::streamsize byte_count = file.tellg();
		//若文件为空
		if (byte_count <= 0)
		{
			logger.error("字体:: 字体文件为空 {}", path);
			return false;
		}
		//回到文件开头并整块读入
		file.seekg(0, std::ios::beg);
		vector<uint8_t> bytes(static_cast<size_t>(byte_count));
		//若读取失败
		if (!file.read(reinterpret_cast<char*>(bytes.data()), byte_count))
		{
			logger.error("字体:: 读取字体文件失败 {}", path);
			return false;
		}
		file.close();

		//初始化字形信息（TTC 等多字体重文件取第 0 号）
		stbtt_fontinfo info{};
		int font_offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
		//若字体头定位失败
		if (font_offset < 0 || !stbtt_InitFont(&info, bytes.data(), font_offset))
		{
			logger.error("字体:: 字体解析失败 {}", path);
			return false;
		}

		//按字号求缩放系数
		float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(pixel_height));
		//垂直度量（求基线偏移：使 y = 0 对应文本顶端）
		int ascent = 0;
		int descent = 0;
		int line_gap = 0;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
		float baseline = std::floor(static_cast<float>(ascent) * scale + 0.5f);

		/*
		逐个字形光栅化
		先用 stb 取各字形的 1 通道位图（调用方负责释放），再统一做行式打包，
		以便一次算出图集高度并只创建一张纹理。
		*/
		const uint32_t atlas_width = 512;
		//待打包字形（临时表）
		struct Pending_Glyph
		{
			//码点
			uint32_t codepoint = 0;
			//位图宽 / 高
			int width = 0;
			int height = 0;
			//位图相对笔位的偏移
			int x_offset = 0;
			int y_offset = 0;
			//位图（1 通道，调用方释放）
			unsigned char* bitmap = nullptr;
			//图集位置（打包后填充）
			uint32_t atlas_x = 0;
			uint32_t atlas_y = 0;
		};
		vector<Pending_Glyph> pending;
		//待落地记录（全部成功才登记）
		Font_Record record{};

		//逐码点光栅化（ASCII 可打印字符）
		for (uint32_t codepoint = FONT_CODEPOINT_FIRST; codepoint <= FONT_CODEPOINT_LAST; ++codepoint)
		{
			//取本字形的缩放后度量
			int advance = 0;
			int left_side_bearing = 0;
			stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &left_side_bearing);
			//本字形度量（无论有无位图都要写 advance）
			Font_Glyph& glyph = record.glyphs[codepoint - FONT_CODEPOINT_FIRST];
			glyph.advance = static_cast<float>(advance) * scale;

			//光栅化本字形（scale_x 传 0 表沿用 scale_y）
			int glyph_width = 0;
			int glyph_height = 0;
			int glyph_x_offset = 0;
			int glyph_y_offset = 0;
			unsigned char* mono = stbtt_GetCodepointBitmap(&info, 0.0f, scale, static_cast<int>(codepoint),
				&glyph_width, &glyph_height, &glyph_x_offset, &glyph_y_offset);
			//空白字符（空格等）无位图，仅保留 advance
			if (mono == nullptr || glyph_width <= 0 || glyph_height <= 0)
				continue;
			//若字形过宽（超出图集宽度，含 2 像素 padding）
			if (static_cast<uint32_t>(glyph_width) + 2 > atlas_width)
			{
				logger.error("字体:: 字形 {} 过宽（{} 像素），超出图集宽度 {}", codepoint, glyph_width, atlas_width);
				//回收本字形与全部已光栅化位图，整体失败
				stbtt_FreeBitmap(mono, nullptr);
				for (Pending_Glyph& item : pending)
					stbtt_FreeBitmap(item.bitmap, nullptr);
				return false;
			}
			//登记待打包
			Pending_Glyph item{};
			item.codepoint = codepoint;
			item.width = glyph_width;
			item.height = glyph_height;
			item.x_offset = glyph_x_offset;
			item.y_offset = glyph_y_offset;
			item.bitmap = mono;
			pending.push_back(item);
		}

		/*
		行式打包（shelf packing）并在图集内定位
		每字形左右各留 1 像素间隔，行高取该行最高字形，避免相邻字形采样渗色。
		*/
		uint32_t cursor_x = 0;
		uint32_t cursor_y = 0;
		uint32_t shelf_height = 0;
		for (Pending_Glyph& item : pending)
		{
			//本行放不下则换行
			if (cursor_x + static_cast<uint32_t>(item.width) + 2 > atlas_width)
			{
				cursor_x = 0;
				cursor_y += shelf_height + 1;
				shelf_height = 0;
			}
			item.atlas_x = cursor_x;
			item.atlas_y = cursor_y;
			cursor_x += static_cast<uint32_t>(item.width) + 2;
			//刷新本行高度
			if (static_cast<uint32_t>(item.height) > shelf_height)
				shelf_height = static_cast<uint32_t>(item.height);
		}
		//图集高度（含末行 + 底部间隔，向上取整到 4 的倍数）
		uint32_t atlas_height = cursor_y + shelf_height + 1;
		atlas_height = (atlas_height + 3) / 4 * 4;
		//防御：至少 4 像素高
		if (atlas_height < 4)
			atlas_height = 4;

		/*
		构造图集像素（RGBA8）
		RGB 恒为白（便于片元着色器直接输出纹理色并靠 A 作覆盖率），A 为字形覆盖率；
		非字形区域 A 为 0（完全透明）。
		*/
		vector<uint8_t> pixels(static_cast<size_t>(atlas_width) * atlas_height * 4, 0);
		for (size_t index = 0; index < pixels.size(); index += 4)
		{
			pixels[index] = 255;
			pixels[index + 1] = 255;
			pixels[index + 2] = 255;
			pixels[index + 3] = 0;
		}
		//逐字形把覆盖率写入 A 通道并登记度量
		for (Pending_Glyph& item : pending)
		{
			//逐像素拷贝覆盖率
			for (int row = 0; row < item.height; ++row)
			{
				for (int col = 0; col < item.width; ++col)
				{
					//本像素覆盖率
					uint8_t coverage = item.bitmap[static_cast<size_t>(row) * item.width + col];
					//零覆盖率跳过（省去无谓写入）
					if (coverage == 0)
						continue;
					//图集内目标下标（A 通道）
					size_t index = (static_cast<size_t>(item.atlas_y + row) * atlas_width
						+ (item.atlas_x + col)) * 4 + 3;
					pixels[index] = coverage;
				}
			}
			//登记本字形度量
			Font_Glyph& glyph = record.glyphs[item.codepoint - FONT_CODEPOINT_FIRST];
			glyph.u0 = static_cast<float>(item.atlas_x) / static_cast<float>(atlas_width);
			glyph.v0 = static_cast<float>(item.atlas_y) / static_cast<float>(atlas_height);
			glyph.u1 = static_cast<float>(item.atlas_x + item.width) / static_cast<float>(atlas_width);
			glyph.v1 = static_cast<float>(item.atlas_y + item.height) / static_cast<float>(atlas_height);
			glyph.x0 = static_cast<float>(item.x_offset);
			glyph.y0 = static_cast<float>(item.y_offset) + baseline;
			glyph.x1 = glyph.x0 + static_cast<float>(item.width);
			glyph.y1 = glyph.y0 + static_cast<float>(item.height);
			glyph.has_ink = true;
			//释放本字形位图
			stbtt_FreeBitmap(item.bitmap, nullptr);
		}
		pending.clear();

		//创建图集纹理（走既有纹理模块的 RGBA8 内存像素路径）
		uint64_t atlas_texture = 0;
		if (!texture->texture_create_from_memory(atlas_texture, pixels.data(), atlas_width, atlas_height))
		{
			logger.error("字体:: 字形图集纹理创建失败 {}", path);
			return false;
		}

		//填充记录
		record.pixel_height = pixel_height;
		record.baseline = baseline;
		record.atlas_texture = atlas_texture;
		record.atlas_width = atlas_width;
		record.atlas_height = atlas_height;
		record.valid = true;

		//分配句柄（优先复用第一个空槽）
		uint64_t handle = 0;
		for (size_t index = 0; index < records.size(); ++index)
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
		logger.info("字体:: 已加载 {}（字号 {}，图集 {}×{}，句柄 {}）", path, pixel_height, atlas_width, atlas_height, handle);
		return true;
	}

	//销毁字体
	void Font_Context::font_destroy(uint64_t handle)
	{
		//句柄无效则忽略
		if (handle == 0 || handle > records.size())
			return;
		//取目标记录
		Font_Record& record = records[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//归还图集纹理
		if (texture != nullptr && record.atlas_texture != 0)
			texture->texture_destroy(record.atlas_texture);
		//整条记录复位（标记槽为空）
		record = Font_Record{};
		logger.info("字体:: 已销毁（句柄 {}）", handle);
	}

	//取字体记录
	const Font_Record* Font_Context::font_get(uint64_t handle) const
	{
		//句柄无效则返回空
		if (handle == 0 || handle > records.size())
			return nullptr;
		//取目标记录
		const Font_Record& record = records[handle - 1];
		//已空则返回空
		if (!record.valid)
			return nullptr;
		return &record;
	}
}
