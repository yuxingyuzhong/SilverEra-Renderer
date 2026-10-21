#version 450
/*
文字片元着色器
采样字形图集（描述符集 1）：图集 RGB 恒为白、A 为字形覆盖率；
文字颜色由**推送常量**（offset 64 起的 vec4）给出 —— 覆盖率同时乘到 RGB 与 alpha 上，
故文字既能着色（RGB），又能保留「覆盖率即源 alpha」的平滑边缘（配合管线的 Alpha 混合档）。
未设置颜色时门面默认推送白色，输出与旧版逐像素一致。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集（描述符集 1，与场景管线的反照率贴图同槽位）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

//推送常量：前 64 字节为矩阵（本片元不用），后 16 字节为文字颜色
layout(push_constant) uniform Push_Color
{
	layout(offset = 64) vec4 color;
} push_color;

void main()
{
	//字形覆盖率（图集 alpha）
	float coverage = texture(glyph_atlas, in_uv).a;
	//按颜色着色（RGB 乘覆盖率、alpha 乘颜色 alpha 与覆盖率）
	out_color = vec4(push_color.color.rgb, push_color.color.a) * coverage;
}
