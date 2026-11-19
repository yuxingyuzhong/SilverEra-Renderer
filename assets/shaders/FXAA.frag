#version 450
/*
后处理片元着色器 · FXAA（快速近似抗锯齿；第一百四十一轮；第一百五十轮补强度参数）
对「源纹理」（描述符集 1）做 5 抽样亮度对比检测：低对比区域原样输出，
高对比（边缘）像素向四邻域均值混合，从而把硬边过渡为中间亮度。
强度取推送常量 post_param 的第三分量（z，0..1；未专设时沿用全局泛光强度 1.2 → 归一为 1），
为 0 时整段旁路、原样输出。
取像素与 反色.frag 一致：texelFetch ＋ 帧缓冲坐标，1:1 或点采样映射，越界钳到边缘。
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

//按 Rec.601 求亮度
float luma_of(vec3 color)
{
	return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
	//源纹理尺寸
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	//中心像素坐标（钳到范围内）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//中心与四邻域
	vec4 center = texelFetch(source_image, coord, 0);
	//强度为 0 时旁路（原样输出）
	float strength = clamp(push.post_param.z, 0.0, 1.0);
	if (strength <= 0.0001)
	{
		out_color = center;
		return;
	}
	vec4 left = texelFetch(source_image, clamp(coord + ivec2(-1, 0), ivec2(0), source_size - ivec2(1)), 0);
	vec4 right = texelFetch(source_image, clamp(coord + ivec2(1, 0), ivec2(0), source_size - ivec2(1)), 0);
	vec4 up = texelFetch(source_image, clamp(coord + ivec2(0, 1), ivec2(0), source_size - ivec2(1)), 0);
	vec4 down = texelFetch(source_image, clamp(coord + ivec2(0, -1), ivec2(0), source_size - ivec2(1)), 0);

	//中心与邻域亮度
	float center_luma = luma_of(center.rgb);
	float left_luma = luma_of(left.rgb);
	float right_luma = luma_of(right.rgb);
	float up_luma = luma_of(up.rgb);
	float down_luma = luma_of(down.rgb);
	//邻域亮度极值（对比度检测）
	float luma_minimum = min(center_luma, min(min(left_luma, right_luma), min(up_luma, down_luma)));
	float luma_maximum = max(center_luma, max(max(left_luma, right_luma), max(up_luma, down_luma)));
	float contrast = luma_maximum - luma_minimum;

	//低对比区域：原样输出（避免全屏模糊）
	if (contrast < 0.05)
	{
		out_color = center;
		return;
	}
	//四邻域均值与混合量（对比度越高混合越强，上限 0.75 保留细节；再乘强度）
	vec3 average = (left.rgb + right.rgb + up.rgb + down.rgb) * 0.25;
	float blend = clamp(contrast * 1.5, 0.0, 0.75) * strength;
	//混合结果（alpha 不参与混合：逐材质色调编号按中心像素透传）
	out_color = vec4(mix(center.rgb, average, blend), center.a);
}
