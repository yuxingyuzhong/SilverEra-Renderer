#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

//游戏引擎命名空间
namespace engine
{
	//从 TTF 文件加载字体
	bool Render_Ui::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 加载字体失败：尚未初始化");
			return false;
		}
		//转交字体模块（光栅化字形并打包图集）
		return state().font.font_load_file(handle_out, path, pixel_height);
	}

	//卸载字体
	void Render_Ui::font_unload(uint64_t handle)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//先清除该字体的文本网格缓存（网格模块就绪时归还网格）
		state().text.font_meshes_destroy(handle);
		//归还字体（含图集纹理）
		state().font.font_destroy(handle);
	}

	//绘制文本（默认单行左对齐布局）
	bool Render_Ui::text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y)
	{
		//默认布局即单行左对齐（不换行、行高取字体像素高度）
		return text_draw_layout(font, text, pixel_x, pixel_y, Text_Layout{});
	}

	//度量单行文本
	bool Render_Ui::text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const
	{
		//默认零度量
		if (width_out != nullptr)
			*width_out = 0.0f;
		if (height_out != nullptr)
			*height_out = 0.0f;
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//取字体记录（只读）
		const Font_Record* record = state().font.font_get(font);
		if (record == nullptr)
			return false;
		//像素度量（整型）
		uint32_t width = 0;
		uint32_t height = 0;
		if (!Font_Context::text_measure(*record, text, width, height))
			return false;
		//写出浮点出参
		if (width_out != nullptr)
			*width_out = static_cast<float>(width);
		if (height_out != nullptr)
			*height_out = static_cast<float>(height);
		return true;
	}

	//按布局绘制文本
	bool Render_Ui::text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
		const Text_Layout& layout)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//标记进入文字绘制（第五十二轮：文字网格不参与阴影投影）
		state().in_text_draw = true;
		//转发文字绘制子系统
		const bool drawn = state().text.draw_layout(font, text, pixel_x, pixel_y, layout);
		//恢复场景绘制标记
		state().in_text_draw = false;
		return drawn;
	}

	//设置文字颜色
	void Render_Ui::text_color_set(const float color[4])
	{
		//未初始化或出参为空则静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || color == nullptr)
			return;
		//转发文字绘制子系统（逐分量钳制）
		state().text.color_set(color);
	}

	//设置文字描边
	void Render_Ui::text_outline_set(float uv_width, const float color[3])
	{
		//未初始化或颜色为空则静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || color == nullptr)
			return;
		//半宽按 0..0.05 钳制（负值 / 非数归 0，避免无意义的大范围采样）
		if (!(uv_width > 0.0f))
			uv_width = 0.0f;
		if (uv_width > 0.05f)
			uv_width = 0.05f;
		state().text_outline_uv_width = uv_width;
		//描边色逐分量钳制到 0..1
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = color[index];
			//非数 / 负值归 0、大于 1 归 1
			if (!(value > 0.0f))
				value = 0.0f;
			if (value > 1.0f)
				value = 1.0f;
			state().text_outline_color[index] = value;
		}
	}

	//取该字体累计被跳过的码点数
	uint64_t Render_Ui::font_skipped_codepoints(uint64_t font) const
	{
		//未初始化则视为 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().font.font_skipped_codepoints_get(font);
	}

	//文字网格缓存命中次数
	uint64_t Render_Ui::text_cache_hits(void) const
	{
		//未初始化则视为 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().text.mesh_cache_hits();
	}

	//文本网格缓存条目上限设置
	void Render_Ui::text_cache_capacity_set(uint32_t capacity)
	{
		//未初始化则静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().text.mesh_cache_capacity_set(capacity);
	}

	//文本网格缓存条目上限
	uint32_t Render_Ui::text_cache_capacity(void) const
	{
		//未初始化则视为 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().text.mesh_cache_capacity();
	}

	//当前文本网格缓存条目数
	uint32_t Render_Ui::text_cache_size(void) const
	{
		//未初始化则视为 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().text.mesh_cache_size();
	}

	//文字网格缓存未命中次数
	uint64_t Render_Ui::text_cache_misses(void) const
	{
		//未初始化则视为 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		return state().text.mesh_cache_misses();
	}
}
