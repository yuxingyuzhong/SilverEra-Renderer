#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//网格数据（文字几何生成需要 Vertex / Mesh_Data；Vulkan-free）
#include "Application/Renderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	//纹理上下文（前置声明；仅作引用入参，用于字形图集的上传与销毁）
	class Texture_Context;

	//支持的字形码点范围（ASCII 可打印字符 32..126）
	inline constexpr uint32_t FONT_CODEPOINT_FIRST = 32;
	inline constexpr uint32_t FONT_CODEPOINT_LAST = 126;
	inline constexpr uint32_t FONT_GLYPH_COUNT = FONT_CODEPOINT_LAST - FONT_CODEPOINT_FIRST + 1;

	/*
	字形度量
	图集 uv 与「相对文本原点（左上角、Y 向下）的像素矩形」均在字体加载时按字号烘焙；
	advance 为笔位推进量（像素）。
	*/
	struct Font_Glyph
	{
		//图集 uv（左上 / 右下）
		float u0 = 0.0f;
		float v0 = 0.0f;
		float u1 = 0.0f;
		float v1 = 0.0f;
		//像素矩形（x0 / y0 为左上，x1 / y1 为右下；相对文本原点）
		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = 0.0f;
		float y1 = 0.0f;
		//笔位推进（像素）
		float advance = 0.0f;
		//字形是否有可见像素（空白字符为假，但仍携带 advance）
		bool has_ink = false;
	};

	/*
	字体记录
	持有字号、字形图集纹理句柄与全部字形度量。
	**不保留字体文件字节**：度量与位图已在加载时烘焙完毕，故记录可脱离字体文件独立存活。
	*/
	struct Font_Record
	{
		//字号（字形像素高度）
		uint32_t pixel_height = 0;
		//基线相对文本原点的像素偏移（= ascent × scale 取整，使 y = 0 为文本顶端）
		float baseline = 0.0f;
		//字形图集纹理句柄（由纹理模块持有）
		uint64_t atlas_texture = 0;
		//图集宽度（像素）
		uint32_t atlas_width = 0;
		//图集高度（像素）
		uint32_t atlas_height = 0;
		//字形度量（下标 = 码点 - FONT_CODEPOINT_FIRST）
		std::array<Font_Glyph, FONT_GLYPH_COUNT> glyphs{};
		//记录是否有效
		bool valid = false;
	};

	/*
	字体上下文
	从 TTF 文件光栅化字形并打包为图集（RGBA8：RGB 恒白，A 为覆盖率），供文字绘制取用。
	生命周期：build → font_load_file → 使用 → destroy；destroy 幂等，析构兜底。
	句柄自 1 起（0 表无效），下标 = 句柄 - 1。
	*/
	class Font_Context
	{
	public:
		//构造（不加载任何字体）
		Font_Context(void) = default;
		//析构（内部调用 destroy，保证图集纹理不泄漏）
		~Font_Context(void);
		//不可拷贝、不可移动（持有纹理句柄与模块引用）
		Font_Context(const Font_Context&) = delete;
		Font_Context& operator=(const Font_Context&) = delete;

		//构建（记录纹理模块，供字形图集上传与销毁）
		bool build(Texture_Context& texture);
		//销毁（幂等；销毁全部字体记录与其图集纹理）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		/*
		从 TTF 文件加载字体并构建 ASCII 字形图集
		字形图集为 RGBA8：RGB 恒为白、A 为字形覆盖率；字形间隔 1 像素 padding 以防邻字渗色。
		成功写出句柄（≥ 1）；文件不存在 / 为空 / 解析失败 / 图集创建失败时返回假且句柄保持 0，
		不留下任何半初始化记录。
		*/
		bool font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height);
		//销毁字体（句柄无效则忽略；一并归还其图集纹理）
		void font_destroy(uint64_t handle);
		//取字体记录（句柄无效返回 nullptr）
		const Font_Record* font_get(uint64_t handle) const;

		//按码点取字形度量（超出支持范围返回 nullptr）
		static const Font_Glyph* glyph_seek(const Font_Record& record, uint32_t codepoint);
		/*
		生成字符串的文字几何
		像素坐标系：原点在**文本左上角**、Y 向下；顶点 position 为像素坐标（z = 0），uv 为图集 uv。
		文本中不含任何可见字形（空串 / 全空白 / 全为不支持码点）时返回假，且不写出任何几何。
		*/
		static bool text_geometry_build(Mesh_Data& data_out, const Font_Record& record, const std::string& text);
	private:
		//纹理模块（字形图集上传 / 销毁）
		Texture_Context* texture = nullptr;
		//字体记录集合（下标 = 句柄 - 1）
		std::vector<Font_Record> records;
	};
}
