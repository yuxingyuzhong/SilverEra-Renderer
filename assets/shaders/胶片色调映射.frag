#version 450
/*
后处理片元着色器 · 胶片色调映射（Hable / Uncharted 2 filmic，方向九第 739／741 轮）

口径
1. 输入源为上一级输出（RGBA8 附件采样），先钳到 [0,1]；
2. 逐通道应用 Hable filmic 曲线（无色彩矩阵，天然中性，不会产生通道色偏）；
3. 白点默认 11.2、曝光偏置默认 2.0；两者可经「逐级额外四参槽」（offset 128）覆盖
   （白点 ≤ 0 回落 11.2，偏置 ≤ 0 回落 2.0），未设参时与第 739 轮逐字节一致；
4. 输出落在 [0,1]，alpha 恒 1；仅作新增预设，不替代既有 ACES / Reinhard / 指数算子。
*/
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
	//逐级额外四参槽（offset 128）：
	//x = 白点（≤ 0 回落 11.2）、y = 曝光偏置（≤ 0 回落 2.0）、z = 与 ACES 的混合比例（0 = 纯胶片）
	layout(offset = 128) vec4 curve_extra;
} push_size;

//默认白点与曝光偏置（第七百四十轮参数化前口径）
const float DEFAULT_WHITE_POINT = 11.2;
const float DEFAULT_EXPOSURE_BIAS = 2.0;

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

//简化 ACES 曲线（与 色调映射.frag 同口径；第 742 轮用于 look 混合）
float aces(float x)
{
	float v = clamp(x, 0.0, 1.0);
	return (v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14);
}

//胶片曲线（白点归一化）
vec3 filmic(vec3 color, float white_point, float exposure_bias)
{
	//只钳下界（第七百四十五轮）：8 位源本就 ≤ 1，结果逐字节不变；16F 源可保留 >1 高光
	vec3 biased = max(color, vec3(0.0)) * exposure_bias;
	vec3 current = vec3(hable(biased.r), hable(biased.g), hable(biased.b));
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
	//白点：参数 ≤ 0 时回落默认口径（保证未设参时逐字节不变）
	float white_point = push_size.curve_extra.x > 0.0 ? push_size.curve_extra.x : DEFAULT_WHITE_POINT;
	//曝光偏置：参数 ≤ 0 时回落默认口径
	float exposure_bias = push_size.curve_extra.y > 0.0 ? push_size.curve_extra.y : DEFAULT_EXPOSURE_BIAS;
	//胶片结果
	vec3 filmic_rgb = filmic(source.rgb, white_point, exposure_bias);
	//与 ACES 的混合比例（0 = 纯胶片；默认 0，保证未设参时逐字节不变）
	float look_mix = clamp(push_size.curve_extra.z, 0.0, 1.0);
	//混合目标（简化 ACES 逐通道）
	vec3 aces_rgb = vec3(aces(source.r), aces(source.g), aces(source.b));
	out_color = vec4(mix(filmic_rgb, aces_rgb, look_mix), 1.0);
}
