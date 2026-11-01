#version 450
/*
粒子片元着色器（第三百一十四轮；第三百一十五轮补年龄曲线；第三百五十四轮补颜色覆盖）
程序化圆盘 + 软边（不采样任何纹理）；默认颜色随归一化年龄由暖白渐变到暗红，透明度随后半程淡出。
颜色覆盖启用时（推送块 offset 96.x = 1）改为在 color_start / color_end 间按年龄插值。
*/
layout(location = 0) in vec2 in_uv;
//归一化年龄（0 = 刚发射、1 = 寿命尽头）
layout(location = 1) flat in float in_age;

/*
颜色覆盖参数（第三百五十四轮；第三百五十五轮补软边宽度）
64..79 = 新生端颜色 rgb、80..95 = 衰老端颜色 rgb、96..111 的 x = 颜色启用标志、y = 软边宽度；
与场景推送块（色调 / 材质参数）同址，粒子着色器不读其余字段。
*/
layout(push_constant) uniform Push_Particle_Color
{
	layout(offset = 64) vec4 particle_color_start;
	layout(offset = 80) vec4 particle_color_end;
	layout(offset = 96) vec4 particle_style;
	//软粒子深度档位（第三百六十六轮；x = 档位，0 = 关闭）
	layout(offset = 112) vec4 particle_soft;
} push_color;

/*
场景深度（set1 binding 2；第三百六十六轮）
软粒子读 HiZ 历史（约两帧前场景深度）；未启用软粒子时该绑定为占位图（档位为 0，不采样）。
*/
layout(set = 1, binding = 2) uniform sampler2D scene_depth;

layout(location = 0) out vec4 out_color;

void main()
{
	//四边形局部坐标（-1..1）
	vec2 local = in_uv * 2.0 - 1.0;
	float radius = length(local);
	/*
	圆盘软边
	软边宽度 > 0 时内半径 = 1 − 宽度；为 0 时保持第三百一十四轮的 0.65 内半径（画面逐字节不变）。
	*/
	float softness = clamp(push_color.particle_style.y, 0.0, 1.0);
	float inner = softness > 0.0 ? 1.0 - softness : 0.65;
	float alpha = smoothstep(1.0, inner, radius);
	if (alpha <= 0.0)
		discard;
	//年龄口径（0 = 年轻、1 = 衰老）
	float age = clamp(in_age, 0.0, 1.0);
	vec3 color;
	/*
	颜色来源
	覆盖启用：两端颜色插值；未启用：内置暖白（年轻）→ 橙黄 → 暗红（衰老）曲线（画面与第三百一十五轮一致）。
	*/
	if (push_color.particle_style.x > 0.5)
		color = mix(push_color.particle_color_start.rgb, push_color.particle_color_end.rgb, age);
	else
	{
		vec3 young_color = vec3(1.0, 0.92, 0.62);
		vec3 old_color = vec3(1.0, 0.22, 0.05);
		color = mix(young_color, old_color, age);
	}
	//后 40% 生命期线性淡出透明度
	float fade = age < 0.6 ? 1.0 : (1.0 - 0.75 * (age - 0.6) / 0.4);
	float out_alpha = alpha * fade;
	/*
	软粒子（第三百六十六轮）
	档位 > 0 时按「历史场景深度 − 粒子深度」/档位 淡出：粒子贴近或进入几何时趋于透明，消除硬切边。
	*/
	if (push_color.particle_soft.x > 0.0)
	{
		ivec2 size = textureSize(scene_depth, 0);
		ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
		float scene_d = texelFetch(scene_depth, coord, 0).r;
		out_alpha *= clamp((scene_d - gl_FragCoord.z) / push_color.particle_soft.x, 0.0, 1.0);
	}
	out_color = vec4(color, out_alpha);
}
