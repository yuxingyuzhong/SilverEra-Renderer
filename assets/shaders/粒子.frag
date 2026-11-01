#version 450
/*
粒子片元着色器（第三百一十四轮；深研方向二 阶段 B）
程序化圆盘 + 软边（不采样任何纹理），输出暖黄色粒子；Alpha 混合由管线档决定。
颜色 / 尺寸随年龄的曲线在第三百一十五轮接入。
*/
layout(location = 0) in vec2 in_uv;

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
	out_color = vec4(1.0, 0.75, 0.35, alpha);
}
