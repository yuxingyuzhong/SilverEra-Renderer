#version 450
/*
UI 文字片元着色器
采样字形图集（set 1 binding 0）：图集 RGB 恒为白、A 为覆盖率；
颜色由推送常量 offset 64 起给出，覆盖率乘到 alpha 上以保留平滑边缘。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集（与场景管线同 set 1 布局，UI 阶段使用独立的 UI 采样集）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

layout(push_constant) uniform Push_UI
{
	layout(offset = 64) vec4 color;
} push_ui;

void main()
{
	//取字形覆盖率（UV 夹到 [0,1] 避免环绕）
	float coverage = texture(glyph_atlas, clamp(in_uv, vec2(0.0), vec2(1.0))).a;
	out_color = vec4(push_ui.color.rgb, push_ui.color.a * coverage);
}
