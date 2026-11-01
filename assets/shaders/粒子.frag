#version 450
/*
粒子片元着色器（第三百一十四轮；第三百一十五轮补年龄曲线）
程序化圆盘 + 软边（不采样任何纹理）；颜色随归一化年龄由暖白渐变到暗红，透明度随后半程淡出。
*/
layout(location = 0) in vec2 in_uv;
//归一化年龄（0 = 刚发射、1 = 寿命尽头）
layout(location = 1) flat in float in_age;

layout(location = 0) out vec4 out_color;

void main()
{
	//四边形局部坐标（-1..1）
	vec2 local = in_uv * 2.0 - 1.0;
	float radius = length(local);
	//圆盘软边（半径 0.65 → 1 之间淡出）
	float alpha = smoothstep(1.0, 0.65, radius);
	if (alpha <= 0.0)
		discard;
	//年龄曲线：暖白（年轻）→ 橙黄 → 暗红（衰老）
	float age = clamp(in_age, 0.0, 1.0);
	vec3 young_color = vec3(1.0, 0.92, 0.62);
	vec3 old_color = vec3(1.0, 0.22, 0.05);
	vec3 color = mix(young_color, old_color, age);
	//后 40% 生命期线性淡出透明度
	float fade = age < 0.6 ? 1.0 : (1.0 - 0.75 * (age - 0.6) / 0.4);
	out_color = vec4(color, alpha * fade);
}
