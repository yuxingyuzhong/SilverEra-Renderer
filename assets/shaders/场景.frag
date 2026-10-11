#version 450
//场景片元着色器
//来自顶点着色器的纹理坐标
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//反照率贴图（描述符集 1）：线性过滤 ＋ 重复寻址
layout(set = 1, binding = 0) uniform sampler2D albedo;

void main()
{
	out_color = texture(albedo, in_uv);
}
