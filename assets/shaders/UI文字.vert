#version 450
/*
UI 文字顶点着色器
顶点属性：位置（vec3，窗口像素坐标，z = 0）＋ 纹理坐标（vec2，字形图集 uv）；
推送常量 offset 0 起为「像素坐标 → NDC」矩阵（列主序 mat4）。
*/
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

//传递给片元着色器的图集坐标
layout(location = 0) out vec2 out_uv;

layout(push_constant) uniform Push_UI
{
	mat4 transform;
} push_ui;

void main()
{
	out_uv = in_uv;
	gl_Position = push_ui.transform * vec4(in_position, 1.0);
}
