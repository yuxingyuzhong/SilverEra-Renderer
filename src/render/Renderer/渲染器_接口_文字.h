#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：文字
	*/
	class Renderer_Text : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		从 TTF 文件加载字体（字号 = 字形像素高度）
		字形被光栅化并打包为 RGBA8 字形图集（RGB 恒为白、A 为字形覆盖率），图集由纹理模块持有。
		成功写出句柄（≥ 1）；未初始化 / 文件不存在 / 为空 / 解析失败 / 图集创建失败时返回假、句柄保持 0。
		*/
		bool font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height);
		//卸载字体（句柄无效则忽略；同时清除该字体的文本网格缓存）
		void font_unload(uint64_t handle);
		/*
		设置文字颜色（RGBA，分量 0..1；逐分量钳制：负值 / 非数归 0、大于 1 归 1）
		颜色由门面记录，自**下一次** text_draw / text_draw_layout 起生效（经推送常量 offset 64 下发）；
		未设置时默认白色 (1,1,1,1)；未初始化或 color 为空时静默返回。
		*/
		void text_color_set(const float color[4]);
		/*
		设置文字描边（第一百四十轮）
		uv_width 为描边半宽（图集 UV 单位，0 = 关闭；按 0..0.05 钳制），color 为描边色（分量 0..1 逐分量钳制）。
		自**下一次** text_draw / text_draw_layout 起生效（经推送常量 offset 80 下发）；width 为 0 时与旧版逐像素一致。
		未初始化或 color 为空时静默返回。
		*/
		void text_outline_set(float uv_width, const float color[3]);

		/*
		绘制文本（UTF-8 文本；**像素坐标**，原点在窗口左上、Y 向下，pixel_x / pixel_y 为文本左上角）
		内部：懒登记文字管线（Alpha 混合档）→ 绑字形图集为当前纹理 → 按 (字体, 文本) 缓存网格 →
		以「像素坐标 → NDC」矩阵登记一个绘制项；实际绘制在下一帧 frame_render 时发生。
		空文本返回真但不登记绘制项；未初始化 / 字体句柄无效 / 文本无可见字形时返回假。
		*/
		bool text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y);
		/*
		度量单行文本（像素）
		宽度按字形推进累加、**不含尾随空白**；高度恒为字体像素高度。
		未初始化 / 字体句柄无效 / 文本无可见字形时两出参置 0 并返回假；出参指针可为空（该侧不写出）。
		*/
		bool text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const;
		/*
		按布局绘制文本（UTF-8；像素坐标，原点在窗口左上、Y 向下，pixel_x / pixel_y 为**首行左上角**）
		换行：layout.max_width > 0 时逐字符断行（显式 '\n' 亦断行）；max_width == 0 时单行、align 无效果。
		对齐：以行为单位按 layout.align 平移（基准为 max_width）；行距取 layout.line_height（0 = 字体像素高度）。
		内部与 text_draw 共用「懒登记文字管线 → 绑图集 → 按 (字体, 文本, 布局) 缓存网格 → 登记绘制项」链路。
		空文本返回真但不登记绘制项；未初始化 / 字体句柄无效 / 文本无可见字形时返回假。
		*/
		bool text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
			const Text_Layout& layout);
		/*
		取该字体累计被跳过的码点数（诊断入口；未初始化 / 句柄无效返回 0）
		口径：只统计「补烘阶段判定为无法呈现」的码点（字体不含该码点、字形过宽、图集空间不足），
		按码点去重计（同一码点至多计一次），UTF-8 解码失败的字节不计；计数只增不减。
		*/
		uint64_t font_skipped_codepoints(uint64_t font) const;
		/*
		文字网格缓存命中 / 未命中次数（第七十五轮新增；自 init 起累计）
		同一（字体, 文本, 布局）第二次及以后绘制即计一次命中；缓存满淘汰不影响累计值。
		未初始化时均返回 0。
		*/
		uint64_t text_cache_hits(void) const;
		uint64_t text_cache_misses(void) const;
		/*
		文本网格缓存条目上限（第八十四轮新增）
		capacity 钳到 [1, 1024]（0 按 1）；缩容时立即淘汰最久未用项（网格延迟到无在途帧引用后归还）。
		默认 64。未初始化时 set 静默忽略、get 返回 0。
		*/
		void text_cache_capacity_set(uint32_t capacity);
		uint32_t text_cache_capacity(void) const;
		//当前文本网格缓存条目数（第八十四轮新增；只读诊断，未初始化为 0）
		uint32_t text_cache_size(void) const;

	};
}
