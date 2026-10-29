#version 450
//反向外扩描边 · 片元着色器（第二百二十八轮）
//只输出描边色；外扩几何的背面即物体的外轮廓。
layout(location = 0) out vec4 out_color;

/*
描边参数（推送常量 offset 112 的 vec4）
x = 外扩宽度，yzw = 描边色（RGB）。
*/
layout(push_constant) uniform Outline_Push
{
	layout(offset = 112) vec4 outline_params;
} outline;

void main()
{
	out_color = vec4(outline.outline_params.yzw, 1.0);
}
