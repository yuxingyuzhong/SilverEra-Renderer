#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 与 VMA 头
本头属渲染层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头。
*/
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
//UI 控件树（SDL-free 公共头）
#include "Engine/EngineRenderer/src/render/UI/用户界面.h"
//网格数据类型（UI 顶点与文字几何）
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"
//依赖模块（仅作引用入参）
#include "Engine/EngineRenderer/src/render/Memory/内存上下文.h"
#include "Engine/EngineRenderer/src/render/Pipeline/管线.h"
#include "Engine/EngineRenderer/src/render/Texture/纹理.h"
#include "Engine/EngineRenderer/src/render/Text/字体.h"

//游戏引擎命名空间
namespace engine
{
	/*
	UI 单帧绘制统计（第八百九十九轮）
	顶点 / 索引为 CPU 侧生成的几何量；draws 为绘制记录数；
	text_draws 为文字记录数；texture_binds 为本帧重写的 UI 描述符次数。
	*/
	struct Ui_Render_Stats
	{
		//顶点数
		uint32_t vertices = 0;
		//索引数
		uint32_t indices = 0;
		//绘制记录数
		uint32_t draws = 0;
		//纯色绘制记录数（第九百一十三轮）
		uint32_t solid_draws = 0;
		//纹理绘制记录数（图片 / 文字 / 纹理面板）
		uint32_t texture_draws = 0;
		//文字绘制记录数
		uint32_t text_draws = 0;
		//纹理描述符重写次数
		uint32_t texture_binds = 0;
		//本帧是否命中稳定帧缓存（1 = 未重建几何）
		uint32_t cache_hits = 0;
		//本帧是否跳过缓冲上传（1 = 当前槽内容已是最新）
		uint32_t upload_skips = 0;
		//本帧文本几何缓存命中次数（第九百零二轮）
		uint32_t text_cache_hits = 0;
		//本帧文本几何缓存未命中次数
		uint32_t text_cache_misses = 0;
		//本帧文本块布局缓存命中次数（第九百零三轮）
		uint32_t block_cache_hits = 0;
		//本帧文本块布局缓存未命中次数
		uint32_t block_cache_misses = 0;
	};

	/*
	UI 渲染器（每帧把控件树转成绘制命令）
	只负责 GPU 侧：每帧槽一组动态顶点 / 索引缓冲、两个 UI 管线（纯色 / 文字）与字形图集绑定；
	控件树与交互由 Ui_Context 维护。
	绘制发生在帧链路的 UI 阶段（无深度附件、载入保留），故管线必须关闭深度测试。
	*/
	class Ui_Renderer
	{
	public:
		//构造（不创建任何 GPU 资源）
		Ui_Renderer(void) = default;
		//析构（内部调用 shutdown，保证资源不泄漏）
		~Ui_Renderer(void);
		//不可拷贝、不可移动（持有 GPU 资源）
		Ui_Renderer(const Ui_Renderer&) = delete;
		Ui_Renderer& operator=(const Ui_Renderer&) = delete;

		//绑定依赖模块与 UI 管线句柄（frame_overlap 决定每帧槽缓冲数）
		bool build(VkDevice device, Memory_Context& memory, Pipeline_Context& pipeline,
			Texture_Context& texture, Font_Context& font,
			uint64_t solid_pipeline, uint64_t text_pipeline, uint32_t frame_overlap);
		//销毁（幂等；归还全部每帧槽缓冲）
		void shutdown(void);
		//是否已构建完成
		bool valid(void) const;
		/*
		把控件树绘制到给定命令缓冲
		slot 为当前帧槽；extent 为 UI 阶段颜色附件尺寸（像素）；
		无可见控件时不产生任何绘制命令；字体未设置或字符无字形时只绘制纯色部分。
		*/
		void draw(const Ui_Context& ui, VkCommandBuffer command_buffer, VkExtent2D extent, uint32_t slot);
		//最近一帧绘制统计（第八百九十九轮）
		const Ui_Render_Stats& stats_get(void) const;
		/*
		设置文本几何 / 文本块布局缓存容量（第九百一十二轮）
		参数钳制到 [1, 1024]，并清空既有缓存条目；默认 64 / 32。
		*/
		void cache_limits_set(uint32_t text_capacity, uint32_t block_capacity);
	private:
		//单帧槽缓冲
		struct Slot_Buffers
		{
			//顶点缓冲
			VkBuffer vertex_buffer = VK_NULL_HANDLE;
			VmaAllocation vertex_allocation = VK_NULL_HANDLE;
			void* vertex_mapped = nullptr;
			uint32_t vertex_capacity = 0;
			//索引缓冲
			VkBuffer index_buffer = VK_NULL_HANDLE;
			VmaAllocation index_allocation = VK_NULL_HANDLE;
			void* index_mapped = nullptr;
			uint32_t index_capacity = 0;
			//已绑定的字形图集纹理句柄（避免逐帧重写描述符）
			uint64_t bound_atlas = 0;
			//该槽已上传内容对应的指纹（第九百零一轮）
			uint64_t uploaded_fingerprint = 0;
			//该槽是否已上传过内容
			bool uploaded = false;
		};
		//单条绘制记录
		struct Draw_Record
		{
			//索引起点与数量
			uint32_t first_index = 0;
			uint32_t index_count = 0;
			//颜色（纯色 / 文字共用）
			Ui_Color color{};
			//纹理句柄（0 = 纯色；文字与图片使用纹理管线）
			uint64_t texture = 0;
			//是否文字绘制（真 = 文字管线 + 图集采样）
			bool text = false;
			//该条绘制的裁剪矩形（窗口像素；阶段录制时转为 scissor）
			Ui_Rect clip{};
		};
		//文本几何缓存条目（第九百零二轮）
		struct Text_Cache_Entry
		{
			//字体句柄（键）
			uint64_t font_handle = 0;
			//文本内容（键）
			std::string text{};
			//居中区宽度（键）
			float center_area_width = 0.0f;
			//度量宽度（命中时省去重新度量）
			uint32_t width = 0;
			//已生成的文字几何（原点在文本左上角）
			Mesh_Data mesh{};
			//最近使用序号（淘汰最久未使用者）
			uint64_t stamp = 0;
		};
		//文本块布局缓存条目（第九百零三轮）
		struct Block_Cache_Entry
		{
			//字体句柄（键）
			uint64_t font_handle = 0;
			//原始文本（键）
			std::string text{};
			//区域宽度（键）
			float area_width = 0.0f;
			//行高参数（键；0 与显式值语义不同）
			float line_height = 0.0f;
			//字间距（键；第九百三十八轮）
			float letter_spacing = 0.0f;
			//首行缩进（键；第九百三十九轮）
			float text_indent = 0.0f;
			//是否自动换行（键）
			bool wrap = false;
			//是否尾部省略（键）
			bool ellipsis = false;
			//垂直对齐（键）
			Ui_Align valign = Ui_Align::Center;
			//分行 / 折行 / 省略后的行列表（值）
			std::vector<std::string> lines{};
			//最近使用序号（淘汰最久未使用者）
			uint64_t stamp = 0;
		};

		//确保帧槽缓冲容量足够（不足时重建）
		bool slot_ensure(Slot_Buffers& slot, uint32_t vertex_count, uint32_t index_count);
		//追加一个矩形（像素坐标）并登记纯色绘制
		void quad_append(const Ui_Rect& rect, const Ui_Color& color);
		//追加一个纹理矩形（UV 铺满 0..1）并登记纹理绘制（第八百九十三轮）
		void quad_textured_append(uint64_t texture, const Ui_Rect& rect, const Ui_Color& color);
		//追加一个指定 UV 范围的纹理矩形（第九百零六轮）
		void quad_textured_append_uv(uint64_t texture, const Ui_Rect& rect,
			float u0, float v0, float u1, float v1, const Ui_Color& color);
		//只追加纹理矩形几何、不登记绘制记录（第九百零九轮）
		void quad_textured_uv_emit(uint64_t texture, const Ui_Rect& rect,
			float u0, float v0, float u1, float v1);
		//追加图片九宫格（9 个子矩形合并为一条绘制记录；切分全 0 时退化为整图）
		void image_slice_append(uint64_t texture, const Ui_Rect& rect,
			float left, float top, float right, float bottom, const Ui_Color& color);
		/*
		追加一个圆角矩形（第八百八十七轮）
		radius <= 0 时退化为 quad_append（保证直角路径逐字节不变）；
		否则按四角圆弧分段 + 中心扇形三角化。
		*/
		void rounded_quad_append(const Ui_Rect& rect, float radius, const Ui_Color& color);
		//追加带描边的背景（先描边后填充；border_width <= 0 时只填充）
		void background_append(const Ui_Rect& rect, float radius, float border_width,
			const Ui_Color& border_color, const Ui_Color& fill_color);
		//追加文本（返回是否登记了绘制；字体未设置 / 无字形时返回假）
		bool text_append(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
			float center_area_width, const Ui_Color& color);
		/*
		追加按字间距逐码点绘制的单行文本（第九百三十八轮）
		letter_spacing 为 0 时退化为 text_append；否则逐码点绘制并追加间距（末字符不追加）。
		*/
		bool text_append_spaced(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
			float letter_spacing, const Ui_Color& color);
		/*
		追加多行文本块（第八百九十四轮）
		按 \n 分行，wrap 时再按区域宽度逐字符换行；line_height <= 0 取字体高度；
		valign 控制多行整体垂直对齐（Start / Center / End）；letter_spacing 计入行宽，
		text_indent 仅首行右移并扣减首行可用宽度；spans 按字节区间覆盖颜色（后者优先）。
		*/
		bool text_append_block(uint64_t font_handle, const std::string& text, const Ui_Rect& area,
			float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis,
			Ui_Align valign, Ui_Align halign, const std::vector<Ui_Text_Span>& spans, float opacity,
			const Ui_Color& color);
		//结束一笔文本绘制（记录当前字形图集句柄）
		void text_atlas_mark(uint64_t font_handle);
		//计算控件树绘制相关字段的指纹（稳定帧缓存判定，第九百轮）
		uint64_t fingerprint_build(const Ui_Context& ui) const;
		//查找文本几何缓存条目（未命中返回空；第九百零二轮）
		Text_Cache_Entry* text_cache_find(uint64_t font_handle, const std::string& text,
			float center_area_width);
		//写入文本几何缓存条目并返回其指针（超出容量时淘汰最久未使用者）
		Text_Cache_Entry* text_cache_insert(uint64_t font_handle, const std::string& text,
			float center_area_width, Mesh_Data&& mesh, uint32_t width);
		//查找文本块布局缓存条目（未命中返回空；第九百零三轮）
		Block_Cache_Entry* block_cache_find(uint64_t font_handle, const std::string& text, float area_width,
			float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis, Ui_Align valign);
		//写入文本块布局缓存条目并返回其指针（超出容量时淘汰最久未使用者）
		Block_Cache_Entry* block_cache_insert(uint64_t font_handle, const std::string& text, float area_width,
			float line_height, float letter_spacing, float text_indent, bool wrap, bool ellipsis, Ui_Align valign,
			std::vector<std::string>&& lines);

	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//内存模块（不持有）
		Memory_Context* memory = nullptr;
		//管线模块（不持有）
		Pipeline_Context* pipeline = nullptr;
		//纹理模块（不持有）
		Texture_Context* texture = nullptr;
		//字体模块（不持有）
		Font_Context* font = nullptr;
		//UI 纯色管线句柄
		uint64_t solid_pipeline = 0;
		//UI 文字管线句柄
		uint64_t text_pipeline = 0;
		//每帧槽缓冲
		std::vector<Slot_Buffers> slots{};
		//本帧 CPU 顶点
		std::vector<Vertex> cpu_vertices{};
		//本帧 CPU 索引
		std::vector<uint32_t> cpu_indices{};
		//本帧绘制记录
		std::vector<Draw_Record> draws{};
		//文本几何缓存（第九百零二轮）
		std::vector<Text_Cache_Entry> text_cache{};
		//文本几何缓存容量上限
		std::size_t text_cache_limit = 64;
		//最近一次缓存使用序号（淘汰依据）
		uint64_t text_cache_stamp = 0;
		//文本块布局缓存（第九百零三轮）
		std::vector<Block_Cache_Entry> block_cache{};
		//文本块布局缓存容量上限
		std::size_t block_cache_limit = 32;
		//最近一次块缓存使用序号
		uint64_t block_cache_stamp = 0;
		//圆角轮廓采样缓冲（复用；第九百零四轮）
		std::vector<float> round_points_x{};
		std::vector<float> round_points_y{};
		//圆角单位圆采样表（4 角 × 每角分段 + 1 的 cos / sin）
		std::vector<float> arc_cos{};
		std::vector<float> arc_sin{};
		//采样表是否已预算
		bool arc_table_ready = false;
		//本帧使用的字形图集纹理句柄（0 = 无文字）
		uint64_t text_atlas = 0;
		//各槽位的继承裁剪（下标 = 句柄 - 1；逐帧复用，第八百八十八轮）
		std::vector<Ui_Rect> inherited_clips{};
		//当前控件的裁剪矩形（追加几何时写入绘制记录）
		Ui_Rect current_clip{};
		//最近一帧绘制统计
		Ui_Render_Stats stats{};
		//最近一次几何重建对应的指纹
		uint64_t last_fingerprint = 0;
		//是否存在可复用的上一帧几何
		bool has_cache = false;
	};
}
