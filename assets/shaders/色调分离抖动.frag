#version 450
/*
后处理片元着色器 · 色调分离 ＋ 有序抖动（第一百一十七轮；链式后处理预设）
把颜色量化到有限级数，并以 4×4 Bayer 矩阵抖动减少色带：
1. x = 量化级数（钳制 2..64）；
2. z = 混入强度（0 = 原样、1 = 完全量化）。
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

//4×4 Bayer 抖动矩阵（值域 0..1）
float bayer_value(ivec2 pixel)
{
	const float matrix[16] = float[16](
		 0.0,  8.0,  2.0, 10.0,
		12.0,  4.0, 14.0,  6.0,
		 3.0, 11.0,  1.0,  9.0,
		15.0,  7.0, 13.0,  5.0);
	ivec2 index = ivec2(pixel.x & 3, pixel.y & 3);
	return matrix[index.y * 4 + index.x] / 16.0;
}

void main()
{
	//源纹理尺寸与像素映射
	ivec2 source_size = textureSize(source_image, 0);
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 color_texel = texelFetch(source_image, coord, 0);
	vec3 color = color_texel.rgb;

	//量化级数与混入强度
	float levels = clamp(push.post_param.x, 2.0, 64.0);
	float strength = clamp(push.post_param.z, 0.0, 1.0);
	//抖动后量化
	float dither = bayer_value(ivec2(gl_FragCoord.xy));
	vec3 quantized = floor(color * levels + dither) / levels;
	vec3 result = mix(color, quantized, strength);

	out_color = vec4(clamp(result, 0.0, 1.0), color_texel.a);
}
