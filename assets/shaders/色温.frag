#version 450
/*
后处理片元着色器 · 色温 / 色调偏移（第一百一十轮；链式后处理预设）
在 RGB 空间施加冷暖和绿-品红偏移：
1. x = 色温（-1 冷 / +1 暖）：暖色增红减蓝，冷色相反；
2. y = 色调（-1 品红 / +1 绿）：绿色通道随正负增减，红蓝反向补偿；
3. z = 强度（0..1 线性缩放整体偏移量）。
参数经推送常量 post_param 下发；未设置时使用全局泛光参数，故使用前应显式设置该级参数。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色
	vec3 color = texelFetch(source_image, coord, 0).rgb;

	//参数归一
	float temperature = clamp(push.post_param.x, -1.0, 1.0);
	float tint = clamp(push.post_param.y, -1.0, 1.0);
	float strength = clamp(push.post_param.z, 0.0, 1.0);

	//色温：暖色加红减蓝；色调：绿色随正负增减，红蓝少量反向补偿
	vec3 shift = vec3(
		temperature * 0.15 + tint * 0.05,
		tint * 0.10,
		-temperature * 0.15 + tint * 0.05);
	color += shift * strength;

	out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
