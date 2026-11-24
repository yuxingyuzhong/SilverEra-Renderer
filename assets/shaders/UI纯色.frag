#version 450
/*
UI 纯色片元着色器
颜色由推送常量 offset 64 起的 vec4 给出（RGBA，0..1）；
圆角裁剪由 offset 80 的矩形与 offset 96 的参数给出（第九百七十五轮）：
x = 半径（逻辑像素）、y = 启用标记、z = UI 缩放；fragment 坐标按缩放还原为逻辑像素。
配合 Alpha 混合档直接写颜色附件。
*/
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push_UI
{
	layout(offset = 64) vec4 color;
	layout(offset = 80) vec4 clip_rect;
	layout(offset = 96) vec4 clip_round;
} push_ui;

void main()
{
	//圆角裁剪：片元落在圆角矩形外则丢弃
	if (push_ui.clip_round.y > 0.5)
	{
		vec2 point = gl_FragCoord.xy / push_ui.clip_round.z;
		vec2 center = push_ui.clip_rect.xy + push_ui.clip_rect.zw * 0.5;
		vec2 half_extent = push_ui.clip_rect.zw * 0.5;
		float radius = push_ui.clip_round.x;
		vec2 delta = abs(point - center) - (half_extent - vec2(radius));
		float distance_to_edge = length(max(delta, vec2(0.0))) + min(max(delta.x, delta.y), 0.0) - radius;
		if (distance_to_edge > 0.0)
			discard;
	}
	out_color = push_ui.color;
}
