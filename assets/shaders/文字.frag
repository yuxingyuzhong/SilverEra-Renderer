#version 450
/*
文字片元着色器
采样字形图集（描述符集 1）：图集 RGB 恒为白、A 为字形覆盖率；
文字颜色由**推送常量**（offset 64 起的 vec4）给出 —— 覆盖率同时乘到 RGB 与 alpha 上，
故文字既能着色（RGB），又能保留「覆盖率即源 alpha」的平滑边缘（配合管线的 Alpha 混合档）。
描边由推送常量 offset 80 的 vec4 给出（第一百四十轮）：x = UV 宽度（0 = 关闭），yzw = 描边颜色；
描边覆盖率取四邻域最大覆盖率与自身覆盖率之差，默认关闭时输出与旧版逐像素一致。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集（描述符集 1，与场景管线的反照率贴图同槽位）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

//推送常量：前 64 字节为矩阵（本片元不用），64 起为文字颜色，80 起为描边参数
layout(push_constant) uniform Push_Text
{
	layout(offset = 64) vec4 color;
	layout(offset = 80) vec4 outline;
} push_text;

void main()
{
	//字形覆盖率（图集 alpha）
	float coverage = texture(glyph_atlas, in_uv).a;
	//文字本体（RGB 乘覆盖率、alpha 乘颜色 alpha 与覆盖率）
	vec4 text_part = vec4(push_text.color.rgb, push_text.color.a) * coverage;
	//描边半宽（UV；0 = 关闭）
	float half_width = push_text.outline.x;
	//未启用描边：输出与旧版逐像素一致
	if (half_width > 0.0)
	{
		//四邻域最大覆盖率（UV 夹到图集范围内，避免采样越界环绕）
		vec2 step_uv = vec2(half_width, half_width);
		float neighbors = max(
			max(texture(glyph_atlas, clamp(in_uv + vec2(step_uv.x, 0.0), vec2(0.0), vec2(1.0))).a,
				texture(glyph_atlas, clamp(in_uv - vec2(step_uv.x, 0.0), vec2(0.0), vec2(1.0))).a),
			max(texture(glyph_atlas, clamp(in_uv + vec2(0.0, step_uv.y), vec2(0.0), vec2(1.0))).a,
				texture(glyph_atlas, clamp(in_uv - vec2(0.0, step_uv.y), vec2(0.0), vec2(1.0))).a));
		//描边环覆盖率（邻域扩出、自身未覆盖的部分）
		float ring = clamp(neighbors - coverage, 0.0, 1.0);
		//描边环与文字本体按 over 合成（文字覆盖处保留文字色）
		vec4 ring_part = vec4(push_text.outline.yzw * ring, ring);
		out_color = ring_part + text_part * (1.0 - ring_part.a);
		return;
	}
	out_color = text_part;
}
