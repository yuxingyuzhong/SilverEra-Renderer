#version 450
/*
UI 文字 / 图片片元着色器
采样绑定纹理（set 1 binding 0）：字形图集 RGB 恒为白、A 为覆盖率；
图片则以纹理 RGB 着色。推送颜色同时乘到 RGB 与 A，字形图集路径结果与旧版一致。
圆角裁剪参数与纯色着色器一致（第九百七十五轮）：片元落在圆角矩形外则丢弃。
*/
layout(location = 0) in vec2 in_uv;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//字形图集 / 图片纹理（与场景管线同 set 1 布局，UI 阶段使用独立的 UI 采样集）
layout(set = 1, binding = 0) uniform sampler2D glyph_atlas;

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
	//取纹理采样（UV 夹到 [0,1] 避免环绕）
	vec4 texel = texture(glyph_atlas, clamp(in_uv, vec2(0.0), vec2(1.0)));
	out_color = vec4(push_ui.color.rgb * texel.rgb, push_ui.color.a * texel.a);
}
