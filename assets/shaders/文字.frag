#version 450
/*
文字片元着色器
采样字形图集（描述符集 1）：图集 RGB 恒为白、A 为字形覆盖率，故直接输出纹理色即可；
配合管线的 **Alpha 混合档**，覆盖率即源 alpha，与背景做半透明混合（故文字边缘平滑）。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集（描述符集 1，与场景管线的反照率贴图同槽位）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

void main()
{
	out_color = texture(glyph_atlas, in_uv);
}
