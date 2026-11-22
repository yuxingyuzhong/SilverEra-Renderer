#version 450
/*
UI 文字 / 图片片元着色器
采样绑定纹理（set 1 binding 0）：字形图集 RGB 恒为白、A 为覆盖率；
图片则以纹理 RGB 着色。推送颜色同时乘到 RGB 与 A，字形图集路径结果与旧版一致。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集 / 图片纹理（与场景管线同 set 1 布局，UI 阶段使用独立的 UI 采样集）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

layout(push_constant) uniform Push_UI
{
	layout(offset = 64) vec4 color;
} push_ui;

void main()
{
	//取纹理采样（UV 夹到 [0,1] 避免环绕）
	vec4 texel = texture(glyph_atlas, clamp(in_uv, vec2(0.0), vec2(1.0)));
	out_color = vec4(push_ui.color.rgb * texel.rgb, push_ui.color.a * texel.a);
}
