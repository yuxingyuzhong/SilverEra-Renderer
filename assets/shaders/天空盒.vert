#version 450
/*
天空盒顶点着色器（第一百二十五轮）
输入为全屏四边形顶点（NDC 坐标 -1..1）；输出固定 z = 1、w = 1，使深度比较（LESS_OR_EQUAL）
只在「深度仍为清屏值 1」的背景像素处通过，从而不覆盖任何已绘制的几何。
实例矩阵属性照常声明以匹配管线顶点输入（本 pass 恒为单位矩阵且不使用）。
*/
//顶点属性：位置（vec3）＋ 纹理坐标（vec2），步长 32，与渲染层 Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
//实例属性：模型矩阵（4 列 vec4；本 pass 不使用，仅匹配管线声明）
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//传递给片元着色器的 NDC 坐标（用于反推视线方向）
layout(location = 0) out vec2 out_ndc;

void main()
{
	//原样输出 NDC（xy 供方向重建；z 固定 1 表示最远）
	out_ndc = in_position.xy;
	gl_Position = vec4(in_position.xy, 1.0, 1.0);
}
