#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//纹理模块完整定义（字形图集的创建与销毁）
#include "src/render/Texture/纹理.h"

//文件读取
//std::floor
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
	//文件内：单个待打包字形（光栅化阶段的临时结构）
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

	/*
	光栅化单个码点
	写出该码点的笔位推进（无论有无位图），并返回 1 通道位图（空白字符返回空指针，由调用方释放非空位图）。
	scale_x 传 0 表示沿用 scale_y。
	*/
	static unsigned char* glyph_bitmap_get(const stbtt_fontinfo& info, float scale, uint32_t codepoint,
		float& advance_out, int& width_out, int& height_out, int& x_offset_out, int& y_offset_out)
	{
		//水平度量（笔位推进与左侧留白）
		int advance = 0;
		int left_side_bearing = 0;
		stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &left_side_bearing);
		advance_out = static_cast<float>(advance) * scale;
		//初始化位图度量
		width_out = 0;
		height_out = 0;
		x_offset_out = 0;
		y_offset_out = 0;
		//光栅化
		return stbtt_GetCodepointBitmap(&info, 0.0f, scale, static_cast<int>(codepoint),
			&width_out, &height_out, &x_offset_out, &y_offset_out);
	}

	//把位图覆盖率写入图集指定位置的 A 通道（RGB 已在图集初始化时置为白）
	static void glyph_blit(Font_Record& record, uint32_t atlas_x, uint32_t atlas_y,
		int width, int height, const unsigned char* bitmap)
	{
		//逐像素拷贝覆盖率
		for (int row = 0; row < height; ++row)
		{
			for (int col = 0; col < width; ++col)
			{
				//本像素覆盖率
				uint8_t coverage = bitmap[static_cast<size_t>(row) * width + col];
				//零覆盖率跳过（省去无谓写入）
				if (coverage == 0)
					continue;
				//图集内目标下标（A 通道）
				size_t index = (static_cast<size_t>(atlas_y + row) * record.atlas_width + (atlas_x + col)) * 4 + 3;
				record.atlas_pixels[index] = coverage;
			}
		}
	}

	//登记字形度量（uv 由像素矩形与图集尺寸推得；x0/y0 为「相对文本原点、Y 向下」的左上角）
	static void glyph_metric_set(Font_Record& record, Font_Glyph& glyph, uint32_t atlas_x, uint32_t atlas_y,
		uint32_t width, uint32_t height, int x_offset, int y_offset, float baseline)
	{
		glyph.u0 = static_cast<float>(atlas_x) / static_cast<float>(record.atlas_width);
		glyph.v0 = static_cast<float>(atlas_y) / static_cast<float>(record.atlas_height);
		glyph.u1 = static_cast<float>(atlas_x + width) / static_cast<float>(record.atlas_width);
		glyph.v1 = static_cast<float>(atlas_y + height) / static_cast<float>(record.atlas_height);
		glyph.x0 = static_cast<float>(x_offset);
		glyph.y0 = static_cast<float>(y_offset) + baseline;
		glyph.x1 = glyph.x0 + static_cast<float>(width);
		glyph.y1 = glyph.y0 + static_cast<float>(height);
		//记录图集原始矩形（扩容重算 uv 时必需）
		glyph.atlas_x = atlas_x;
		glyph.atlas_y = atlas_y;
		glyph.atlas_w = width;
		glyph.atlas_h = height;
		glyph.has_ink = true;
	}

	/*
	字形图集扩容（第三百九十九轮）
	补烘时空间耗尽不再「跳过字形」，而是把图集高度翻倍：
	① 新建更大的像素缓冲，逐行拷贝旧内容（宽度不变，故旧行偏移全保留）；
	② 按新高度重算全部已烘焙字形的 uv（v 分量随高度变化）；
	③ 重建图集纹理，旧纹理移入退役列表（可能仍被在途帧引用，不在本帧销毁）。
	返回假表示纹理重建失败（旧图集与全部字形度量保持原样，调用方按「空间不足」处理）。
	*/
	static bool atlas_grow(Font_Record& record, Texture_Context& texture)
	{
		//新高度（翻倍并对齐到 4 的倍数）
		uint32_t new_height = record.atlas_height * 2;
		new_height = (new_height + 3) / 4 * 4;
		//扩容上限（第九百七十八轮）：超过上限不再扩容，调用方按空间不足跳过字形
		constexpr uint32_t ATLAS_MAX_HEIGHT = 8192;
		if (new_height > ATLAS_MAX_HEIGHT)
		{
			logger.warn("字体:: 字形图集已达高度上限 {}（请求 {}）", ATLAS_MAX_HEIGHT, new_height);
			return false;
		}
		//新像素缓冲（RGB 恒白、A 为覆盖率；新增区域全透明）
		vector<uint8_t> pixels(static_cast<size_t>(record.atlas_width) * new_height * 4, 0);
		for (size_t index = 0; index < pixels.size(); index += 4)
		{
			pixels[index] = 255;
			pixels[index + 1] = 255;
			pixels[index + 2] = 255;
			pixels[index + 3] = 0;
		}
		//拷贝旧图集（宽度相同，逐行整段拷贝）
		const size_t old_row_bytes = static_cast<size_t>(record.atlas_width) * 4;
		const size_t old_bytes = old_row_bytes * record.atlas_height;
		if (old_bytes > 0 && record.atlas_pixels.size() >= old_bytes)
			::memcpy(pixels.data(), record.atlas_pixels.data(), old_bytes);

		//重建图集纹理（失败则整体回滚，不改变记录）
		uint64_t new_texture = 0;
		if (!texture.texture_create_from_memory(new_texture, pixels.data(), record.atlas_width, new_height))
		{
			logger.error("字体:: 字形图集扩容时纹理重建失败（{}×{}）", record.atlas_width, new_height);
			return false;
		}
		//旧纹理退役（延迟归还，避免在途帧采样已销毁纹理）
		if (record.atlas_texture != 0)
		{
			record.retired_atlas_textures.push_back(record.atlas_texture);
			//退役列表超限时销毁最旧项（第九百七十八轮）
			while (record.retired_atlas_textures.size() > 8)
			{
				texture.texture_destroy(record.retired_atlas_textures.front());
				record.retired_atlas_textures.erase(record.retired_atlas_textures.begin());
			}
		}
		record.atlas_texture = new_texture;
		record.atlas_pixels.swap(pixels);
		record.atlas_height = new_height;
		record.atlas_grow_count += 1;

		//按新高度重算全部字形 uv（u 不变，v 按新高度缩放）
		for (auto& entry : record.glyphs)
		{
			Font_Glyph& glyph = entry.second;
			//无墨迹字形（空白 / 未支持 / 跳过）没有图集矩形，跳过
			if (!glyph.has_ink)
				continue;
			glyph.v0 = static_cast<float>(glyph.atlas_y) / static_cast<float>(record.atlas_height);
			glyph.v1 = static_cast<float>(glyph.atlas_y + glyph.atlas_h) / static_cast<float>(record.atlas_height);
		}
		logger.info("字体:: 字形图集已扩容到 {}×{}", record.atlas_width, record.atlas_height);
		return true;
	}

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

	//当前有效字体数量（第一百一十三轮）
	uint32_t Font_Context::font_count(void) const
	{
		//逐条统计有效记录
		uint32_t count = 0;
		for (const Font_Record& record : records)
			if (record.valid)
				++count;
		return count;
	}

	//销毁（幂等）
	void Font_Context::destroy(void)
	{
		//逐条归还字形图集纹理
		for (Font_Record& record : records)
		{
			//纹理模块就绪时归还当前图集与全部退役图集
			if (texture != nullptr)
			{
				if (record.atlas_texture != 0)
					texture->texture_destroy(record.atlas_texture);
				for (uint64_t retired : record.retired_atlas_textures)
					texture->texture_destroy(retired);
			}
			//整条记录复位
			record = Font_Record{};
		}
		records.clear();
		texture = nullptr;
	}

	//按码点取字形度量
	const Font_Glyph* Font_Context::glyph_seek(const Font_Record& record, uint32_t codepoint)
	{
		//在已烘焙字形表中查找（未烘焙则无度量）
		std::unordered_map<uint32_t, Font_Glyph>::const_iterator it = record.glyphs.find(codepoint);
		if (it == record.glyphs.end())
			return nullptr;
		return &it->second;
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
		逐个字形光栅化（ASCII 可打印字符）
		先用 stb 取各字形的 1 通道位图（调用方负责释放），再统一做行式打包，
		以便一次算出图集高度并只创建一张纹理。
		*/
		const uint32_t atlas_width = 512;
		vector<Pending_Glyph> pending;
		//待落地记录（全部成功才登记）
		Font_Record record{};
		//逐码点光栅化
		for (uint32_t codepoint = FONT_CODEPOINT_FIRST; codepoint <= FONT_CODEPOINT_LAST; ++codepoint)
		{
			//本字形度量（无论有无位图都要写 advance）
			Font_Glyph glyph{};
			int glyph_width = 0;
			int glyph_height = 0;
			int glyph_x_offset = 0;
			int glyph_y_offset = 0;
			unsigned char* mono = glyph_bitmap_get(info, scale, codepoint, glyph.advance,
				glyph_width, glyph_height, glyph_x_offset, glyph_y_offset);
			//空白字符（空格等）无位图，仅保留 advance 并登记「已烘焙」
			if (mono == nullptr || glyph_width <= 0 || glyph_height <= 0)
			{
				if (mono != nullptr)
					stbtt_FreeBitmap(mono, nullptr);
				record.glyphs[codepoint] = glyph;
				continue;
			}
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
			//先登记 advance（uv / 像素矩形随后在打包后补齐）
			record.glyphs[codepoint] = glyph;
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

		/*
		图集高度
		ASCII 占用高度之外再留 3 倍余量（非 ASCII 码点如中文按需补烘时追加进同一张图集），
		下限 512 像素，向上取整到 4 的倍数；
		补烘仍不足时按 atlas_grow 翻倍扩容并重算全部 uv（第三百九十九轮）。
		*/
		uint32_t ascii_height = cursor_y + shelf_height + 1;
		/*
		预留余量由「× 2」提为「× 4」（第三百九十九轮）
		中文标签量大（二十余展区 ＋ 叠加层），× 2 常在补烘途中耗尽，触发多次扩容；
		× 4 把首轮容量抬到约 500 行像素，常规展示场景一次即可容纳全部中文。
		*/
		uint32_t atlas_height = ascii_height * 4;
		//下限与对齐
		if (atlas_height < 512)
			atlas_height = 512;
		atlas_height = (atlas_height + 3) / 4 * 4;

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

		//记录图集与打包游标（惰性补烘据此继续打包）
		record.pixel_height = pixel_height;
		record.baseline = baseline;
		record.atlas_width = atlas_width;
		record.atlas_height = atlas_height;
		record.atlas_pixels = std::move(pixels);
		record.cursor_x = cursor_x;
		record.cursor_y = cursor_y;
		record.shelf_height = shelf_height;
		//保留字体文件字节（惰性补烘需重新定位字形头）
		record.font_bytes = std::move(bytes);

		//逐字形把覆盖率写入 A 通道并补齐度量
		for (Pending_Glyph& item : pending)
		{
			//写入覆盖率
			glyph_blit(record, item.atlas_x, item.atlas_y, item.width, item.height, item.bitmap);
			//补齐本字形度量（uv 与像素矩形）
			Font_Glyph& glyph = record.glyphs[item.codepoint];
			glyph_metric_set(record, glyph, item.atlas_x, item.atlas_y,
				static_cast<uint32_t>(item.width), static_cast<uint32_t>(item.height),
				item.x_offset, item.y_offset, baseline);
			//释放本字形位图
			stbtt_FreeBitmap(item.bitmap, nullptr);
		}
		pending.clear();

		//创建图集纹理（走既有纹理模块的 RGBA8 内存像素路径）
		uint64_t atlas_texture = 0;
		if (!texture->texture_create_from_memory(atlas_texture, record.atlas_pixels.data(), atlas_width, atlas_height))
		{
			logger.error("字体:: 字形图集纹理创建失败 {}", path);
			return false;
		}
		record.atlas_texture = atlas_texture;
		record.valid = true;

		//分配句柄（优先复用第一个空槽）
		uint64_t handle = 0;
		for (size_t index = 0; index < records.size(); ++index)
		{
			//若该槽已空
			if (!records[index].valid)
			{
				records[index] = std::move(record);
				handle = static_cast<uint64_t>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			records.push_back(std::move(record));
			handle = static_cast<uint64_t>(records.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("字体:: 已加载 {}（字号 {}，图集 {}×{}，句柄 {}）", path, pixel_height, atlas_width, atlas_height, handle);
		return true;
	}

	/*
	惰性补烘文本所需字形
	把文本中出现而尚未烘焙的码点光栅化并追加进图集；确有字形写入时重建图集纹理。
	*/
	bool Font_Context::glyphs_ensure(Font_Record& record, const std::string& text)
	{
		//前置：记录与纹理模块必须可用
		if (!record.valid || texture == nullptr)
			return false;
		//解码文本得到去重码点
		vector<uint32_t> codepoints;
		if (!text_codepoints(text, codepoints))
			return true;
		//收集尚未烘焙的码点
		vector<uint32_t> missing;
		for (uint32_t codepoint : codepoints)
		{
			if (record.glyphs.find(codepoint) == record.glyphs.end())
				missing.push_back(codepoint);
		}
		//无需补烘
		if (missing.empty())
			return true;
		//字体文件字节必须保留（惰性补烘据此重新定位字形头）
		if (record.font_bytes.empty())
		{
			logger.error("字体:: 未保留字体文件字节，无法惰性补烘码点");
			return false;
		}

		//重新初始化字形头（各次补烘互不依赖状态）
		stbtt_fontinfo info{};
		int font_offset = stbtt_GetFontOffsetForIndex(record.font_bytes.data(), 0);
		if (font_offset < 0 || !stbtt_InitFont(&info, record.font_bytes.data(), font_offset))
		{
			logger.error("字体:: 惰性补烘时字体解析失败");
			return false;
		}
		//按字号求缩放系数
		float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(record.pixel_height));

		//本次是否写入了字形像素（决定是否需要重建图集纹理）
		bool atlas_changed = false;
		//逐码点补烘
		for (uint32_t codepoint : missing)
		{
			/*
			字体不含该码点则直接跳过（诊断计数 +1）
			以 FindGlyphIndex 判「是否真正支持」：空白字符（如空格）返回非 0，故不会被误计。
			*/
			if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0)
			{
				logger.warn("字体:: 字体不含码点 {}，已跳过", codepoint);
				++record.skipped_codepoints;
				record.glyphs[codepoint] = Font_Glyph{};
				continue;
			}
			//本字形度量（无论有无位图都要写 advance）
			Font_Glyph glyph{};
			int glyph_width = 0;
			int glyph_height = 0;
			int glyph_x_offset = 0;
			int glyph_y_offset = 0;
			unsigned char* mono = glyph_bitmap_get(info, scale, codepoint, glyph.advance,
				glyph_width, glyph_height, glyph_x_offset, glyph_y_offset);
			//空白 / 无位图字形：登记 advance 并结束本码点
			if (mono == nullptr || glyph_width <= 0 || glyph_height <= 0)
			{
				if (mono != nullptr)
					stbtt_FreeBitmap(mono, nullptr);
				record.glyphs[codepoint] = glyph;
				continue;
			}
			//字形过宽（超出图集宽度）：跳过（登记为无墨迹，避免重复尝试）
			if (static_cast<uint32_t>(glyph_width) + 2 > record.atlas_width)
			{
				logger.warn("字体:: 补烘字形 {} 过宽（{} 像素），已跳过", codepoint, glyph_width);
				++record.skipped_codepoints;
				stbtt_FreeBitmap(mono, nullptr);
				record.glyphs[codepoint] = glyph;
				continue;
			}
			//本行放不下则换行
			if (record.cursor_x + static_cast<uint32_t>(glyph_width) + 2 > record.atlas_width)
			{
				record.cursor_x = 0;
				record.cursor_y += record.shelf_height + 1;
				record.shelf_height = 0;
			}
			//图集高度不足（空间耗尽）：先扩容再重试，仍不足才跳过（第三百九十九轮）
			while (record.cursor_y + static_cast<uint32_t>(glyph_height) + 1 > record.atlas_height)
			{
				if (!atlas_grow(record, *texture))
					break;
			}
			//扩容后仍放不下（字形本身高于图集，或扩容失败）：跳过该码点
			if (record.cursor_y + static_cast<uint32_t>(glyph_height) + 1 > record.atlas_height)
			{
				logger.warn("字体:: 字形图集空间不足，字形 {} 已跳过", codepoint);
				++record.skipped_codepoints;
				stbtt_FreeBitmap(mono, nullptr);
				record.glyphs[codepoint] = glyph;
				continue;
			}
			//定位并写入
			uint32_t atlas_x = record.cursor_x;
			uint32_t atlas_y = record.cursor_y;
			glyph_blit(record, atlas_x, atlas_y, glyph_width, glyph_height, mono);
			//补齐度量并登记
			glyph_metric_set(record, glyph, atlas_x, atlas_y, static_cast<uint32_t>(glyph_width),
				static_cast<uint32_t>(glyph_height), glyph_x_offset, glyph_y_offset, record.baseline);
			record.glyphs[codepoint] = glyph;
			//推进打包游标
			record.cursor_x += static_cast<uint32_t>(glyph_width) + 2;
			if (static_cast<uint32_t>(glyph_height) > record.shelf_height)
				record.shelf_height = static_cast<uint32_t>(glyph_height);
			//释放位图
			stbtt_FreeBitmap(mono, nullptr);
			atlas_changed = true;
		}

		//若本次未写入任何字形像素则无需重建纹理
		if (!atlas_changed)
			return true;

		/*
		重建图集纹理
		旧图集纹理不可立即销毁（可能仍被在途帧的描述符集引用），故移入「已退役列表」，待字体销毁时统一归还。
		*/
		uint64_t rebuilt_texture = 0;
		if (!texture->texture_create_from_memory(rebuilt_texture, record.atlas_pixels.data(),
			record.atlas_width, record.atlas_height))
		{
			logger.error("字体:: 字形图集纹理重建失败");
			return false;
		}
		record.retired_atlas_textures.push_back(record.atlas_texture);
		//退役列表超限时销毁最旧项（第九百七十八轮）
		while (record.retired_atlas_textures.size() > 8)
		{
			texture->texture_destroy(record.retired_atlas_textures.front());
			record.retired_atlas_textures.erase(record.retired_atlas_textures.begin());
		}
		record.atlas_texture = rebuilt_texture;
		logger.info("字体:: 字形图集已补烘（现存 {} 个字形，图集 {}×{}）",
			static_cast<uint32_t>(record.glyphs.size()), record.atlas_width, record.atlas_height);
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
		//归还图集纹理与全部退役图集纹理
		if (texture != nullptr)
		{
			if (record.atlas_texture != 0)
				texture->texture_destroy(record.atlas_texture);
			for (uint64_t retired : record.retired_atlas_textures)
				texture->texture_destroy(retired);
		}
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

	//取累计被跳过的码点数
	uint64_t Font_Context::font_skipped_codepoints_get(uint64_t handle) const
	{
		//句柄无效则返回 0
		if (handle == 0 || handle > records.size())
			return 0;
		//取目标记录
		const Font_Record& record = records[handle - 1];
		//已空则返回 0
		if (!record.valid)
			return 0;
		return record.skipped_codepoints;
	}

	//取可写字体记录
	Font_Record* Font_Context::font_get_mutable(uint64_t handle)
	{
		//句柄无效则返回空
		if (handle == 0 || handle > records.size())
			return nullptr;
		//取目标记录
		Font_Record& record = records[handle - 1];
		//已空则返回空
		if (!record.valid)
			return nullptr;
		return &record;
	}
}