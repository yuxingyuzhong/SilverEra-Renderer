#include "src/render/Text/局部命名空间使用.h"
//文字绘制子系统（宿主回调与网格缓存）
#include "src/render/Text/文字渲染.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//绑定字体模块与宿主回调
	void Text_Renderer::init(Font_Context& font, const Text_Host& host)
	{
		this->font = &font;
		this->host = host;
	}

	//设置文字颜色（逐分量钳制）
	void Text_Renderer::color_set(const float color[4])
	{
		//空指针无内容可写
		if (color == nullptr)
			return;
		//逐分量钳制后写入
		for (std::size_t index = 0; index < 4; ++index)
		{
			float value = color[index];
			//负值 / 非数归 0、大于 1 归 1
			if (!(value >= 0.0f))
				value = 0.0f;
			else if (value > 1.0f)
				value = 1.0f;
			text_color[index] = value;
		}
	}

	//当前文字颜色
	const float* Text_Renderer::color(void) const
	{
		return text_color;
	}

	//文字管线句柄
	uint64_t Text_Renderer::pipeline_get(void) const
	{
		return pipeline;
	}

	//回填文字管线句柄
	void Text_Renderer::pipeline_set(uint64_t value)
	{
		pipeline = value;
	}

	//归还指定字体的网格缓存
	void Text_Renderer::font_meshes_destroy(uint64_t font_handle)
	{
		//逐个检查（缓存通常极小）
		for (auto it = records.begin(); it != records.end(); )
		{
			//非目标字体则前进
			if (it->font != font_handle)
			{
				++it;
				continue;
			}
			//归还网格并移除记录
			if (host.mesh_destroy != nullptr)
				host.mesh_destroy(host.user, it->mesh);
			it = records.erase(it);
		}
	}

	//归还全部网格并复位
	void Text_Renderer::shutdown(void)
	{
		//归还全部缓存网格
		for (auto& record : records)
			//宿主已解绑时无处归还，直接丢弃记录
			if (host.mesh_destroy != nullptr)
				host.mesh_destroy(host.user, record.mesh);
		records.clear();
		//复位管线句柄与颜色
		pipeline = 0;
		for (std::size_t index = 0; index < 4; ++index)
			text_color[index] = 1.0f;
	}

	//按布局绘制文本（像素坐标）
	bool Text_Renderer::draw_layout(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
		const Text_Layout& layout)
	{
		/*
		宿主回调完整性检查
		缺任一回调即无法完成绘制，直接拒绝（避免半程失败留下已补烘的字形却无绘制项）。
		*/
		bool host_ready = font != nullptr
			&& host.pipeline_ensure != nullptr && host.pipeline_bind != nullptr
			&& host.texture_bind != nullptr && host.mesh_create != nullptr
			&& host.mesh_destroy != nullptr && host.mesh_draw != nullptr && host.extent_get != nullptr;
		//若宿主未就绪
		if (!host_ready)
		{
			logger.error("文字渲染:: 绘制失败：宿主回调不完整");
			return false;
		}
		//取可写字体记录（惰性补烘字形需修改记录）
		Font_Record* record = font->font_get_mutable(font_handle);
		if (record == nullptr)
		{
			logger.error("文字渲染:: 绘制失败：字体句柄 {} 无效", font_handle);
			return false;
		}
		//空文本无可绘制项（视为成功）
		if (text.empty())
			return true;
		/*
		惰性补烘文本所需字形
		加载时只烘焙 ASCII；非 ASCII 码点（如中文）在首次绘制遇到时补烘并追加进图集，
		随后图集纹理被重建（旧纹理退役），故须先补烘再取几何 / 绑纹理。
		*/
		if (!font->glyphs_ensure(*record, text))
		{
			logger.error("文字渲染:: 文字字形补烘失败");
			return false;
		}
		//确保文字管线就绪（宿主懒登记）
		uint64_t text_pipeline = host.pipeline_ensure(host.user);
		if (text_pipeline == 0)
		{
			logger.error("文字渲染:: 文字管线登记失败");
			return false;
		}
		pipeline = text_pipeline;
		/*
		取 / 建 (字体, 文本, 布局) 网格
		文字几何逐帧重建代价不必要，故按「字体句柄 ＋ 文本 ＋ 布局五项」缓存网格句柄；
		布局必须参与缓存键，否则同一文本换布局后会命中旧网格（几何不更新）。
		*/
		Mesh_Handle mesh = 0;
		for (const auto& item : records)
		{
			//命中本字体与本文本
			if (item.font != font_handle || item.text != text)
				continue;
			//布局五项须完全一致
			if (item.layout.max_width != layout.max_width
				|| item.layout.line_height != layout.line_height
				|| item.layout.align != layout.align
				|| item.layout.tracking != layout.tracking
				|| item.layout.first_line_indent != layout.first_line_indent)
				continue;
			mesh = item.mesh;
			break;
		}
		//未命中则新建
		if (mesh == 0)
		{
			//按布局生成文字几何（像素坐标，原点在文本左上角）
			Mesh_Data data;
			if (!Font_Context::text_layout_build(data, *record, text, layout))
			{
				logger.error("文字渲染:: 文字几何生成失败（文本无可见字形）");
				return false;
			}
			//上传为网格
			if (!host.mesh_create(host.user, mesh, data))
			{
				logger.error("文字渲染:: 文字网格创建失败");
				return false;
			}
			//登记缓存
			Record item{};
			item.font = font_handle;
			item.text = text;
			item.layout = layout;
			item.mesh = mesh;
			records.push_back(item);
		}

		//绑定文字管线与字形图集纹理（图集成为当前纹理，供 set1 采样）
		host.pipeline_bind(host.user, text_pipeline);
		host.texture_bind(host.user, record->atlas_texture);

		//绘制目标尺寸（像素）
		float screen_width = 0.0f;
		float screen_height = 0.0f;
		host.extent_get(host.user, screen_width, screen_height);
		//尺寸非法则拒绝（避免除零）
		if (!(screen_width > 0.0f) || !(screen_height > 0.0f))
		{
			logger.error("文字渲染:: 绘制失败：绘制目标尺寸非法");
			return false;
		}
		/*
		像素坐标 → NDC 的仿射矩阵（列主序）
		x' = 2 / 屏宽 × px + (2 × pixel_x / 屏宽 − 1)；
		y' = 2 / 屏高 × py + (2 × pixel_y / 屏高 − 1)。
		坐标系口径：Vulkan 视口把 NDC y 映射为「向下递增」的帧缓冲行号，
		故此处**不做 Y 翻转**即可让「像素行号 = 帧缓冲行号」，像素 (x, y) 与屏幕位置一一对应。
		*/
		std::array<float, 16> transform = {};
		transform[0] = 2.0f / screen_width;
		transform[5] = 2.0f / screen_height;
		transform[10] = 1.0f;
		transform[12] = 2.0f * pixel_x / screen_width - 1.0f;
		transform[13] = 2.0f * pixel_y / screen_height - 1.0f;
		transform[15] = 1.0f;

		//登记绘制项（沿用既有网格绘制路径；文字颜色经推送常量下发）
		return host.mesh_draw(host.user, mesh, transform.data(), text_color);
	}
}
