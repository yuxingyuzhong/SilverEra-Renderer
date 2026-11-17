#version 450
/*
后处理片元着色器 · 胶片色调映射（Hable / Uncharted 2 filmic，方向九第 739 轮）

口径
1. 输入源为上一级输出（RGBA8 sRGB 附件采样），先钳到 [0,1]；
2. 逐通道应用 Hable filmic 曲线（无色彩矩阵，天然中性，不会产生通道色偏）；
3. 以白点 11.2、曝光偏置 2.0 归一化，输出落在 [0,1]，alpha 恒 1；
4. 仅作新增预设，不替代既有 ACES / Reinhard / 指数算子。
*/
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

//Hable filmic 基础曲线（单通道；A..F 为公开拟合系数）
float hable(float x)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

//胶片曲线（白点归一化）
vec3 filmic(vec3 color)
{
	//白点与曝光偏置（公开口径）
	const float white_point = 11.2;
	const float exposure_bias = 2.0;
	vec3 current = vec3(hable(clamp(color.r, 0.0, 1.0) * exposure_bias),
		hable(clamp(color.g, 0.0, 1.0) * exposure_bias),
		hable(clamp(color.b, 0.0, 1.0) * exposure_bias));
	float white_scale = hable(white_point);
	return clamp(current / vec3(max(white_scale, 1e-5)), 0.0, 1.0);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色并映射
	vec4 source = texelFetch(source_image, coord, 0);
	out_color = vec4(filmic(source.rgb), 1.0);
}
