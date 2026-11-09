#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	//创建离屏渲染目标

	bool Renderer_Text::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 加载字体失败：尚未初始化");
			return false;
		}
		//转交字体模块（光栅化字形并打包图集）
		return impl->font.font_load_file(handle_out, path, pixel_height);
	}

	void Renderer_Text::font_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//先清除该字体的文本网格缓存（网格模块就绪时归还网格）
		impl->text.font_meshes_destroy(handle);
		//归还字体（含图集纹理）
		impl->font.font_destroy(handle);
	}

	bool Renderer_Text::text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y)
	{
		//默认布局即单行左对齐（不换行、行高取字体像素高度）
		return self().text_draw_layout(font, text, pixel_x, pixel_y, Text_Layout{});
	}

	bool Renderer_Text::text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const
	{
		//默认零度量
		if (width_out != nullptr)
			*width_out = 0.0f;
		if (height_out != nullptr)
			*height_out = 0.0f;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//取字体记录（只读）
		const Font_Record* record = impl->font.font_get(font);
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

	bool Renderer_Text::text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
		const Text_Layout& layout)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//标记进入文字绘制（第五十二轮：文字网格不参与阴影投影）
		impl->in_text_draw = true;
		//转发文字绘制子系统
		const bool drawn = impl->text.draw_layout(font, text, pixel_x, pixel_y, layout);
		//恢复场景绘制标记
		impl->in_text_draw = false;
		return drawn;
	}

	void Renderer_Text::text_color_set(const float color[4])
	{
		//未初始化或出参为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//转发文字绘制子系统（逐分量钳制）
		impl->text.color_set(color);
	}

	void Renderer_Text::text_outline_set(float uv_width, const float color[3])
	{
		//未初始化或颜色为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//半宽按 0..0.05 钳制（负值 / 非数归 0，避免无意义的大范围采样）
		if (!(uv_width > 0.0f))
			uv_width = 0.0f;
		if (uv_width > 0.05f)
			uv_width = 0.05f;
		impl->text_outline_uv_width = uv_width;
		//描边色逐分量钳制到 0..1
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = color[index];
			//非数 / 负值归 0、大于 1 归 1
			if (!(value > 0.0f))
				value = 0.0f;
			if (value > 1.0f)
				value = 1.0f;
			impl->text_outline_color[index] = value;
		}
	}

	uint64_t Renderer_Text::font_skipped_codepoints(uint64_t font) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->font.font_skipped_codepoints_get(font);
	}

	uint64_t Renderer_Text::text_cache_hits(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_hits();
	}

	void Renderer_Text::text_cache_capacity_set(uint32_t capacity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->text.mesh_cache_capacity_set(capacity);
	}

	uint32_t Renderer_Text::text_cache_capacity(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_capacity();
	}

	uint32_t Renderer_Text::text_cache_size(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_size();
	}

	uint64_t Renderer_Text::text_cache_misses(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_misses();
	}
}
