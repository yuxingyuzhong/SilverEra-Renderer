#version 450
/*
UI 纯色片元着色器
颜色由推送常量 offset 64 起的 vec4 给出（RGBA，0..1）；
配合 Alpha 混合档直接写颜色附件。
*/
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push_UI
{
	layout(offset = 64) vec4 color;
} push_ui;

void main()
{
	out_color = push_ui.color;
}
