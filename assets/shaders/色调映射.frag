#version 450
/*
后处理片元着色器 · 色调映射（链式后处理预设）
按简化 ACES 曲线逐通道压缩高光：x * (2.51x + 0.03) / (x * (2.43x + 0.59) + 0.14)。

口径：
1. 输入先钳到 [0,1]（本层后处理源为 8 位附件，不会超过 1）；
2. 输出仍落在 [0,1]，故可直接写给 sRGB / UNORM 附件；
3. alpha 恒置 1；只做逐像素映射，不做曝光 / gamma 参数化。
典型值：x=1 → ≈0.80（高光被压缩）、x=0.18 → ≈0.20（暗区近似线性）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

//简化 ACES 曲线（单通道）
float aces(float x)
{
	/*
	只钳下界（第七百四十五轮）：8 位源本就 ≤ 1，去掉上钳后结果逐字节不变；
	16F 源则可保留 >1 的高光，由曲线真实压缩。
	*/
	float v = max(x, 0.0);
	return (v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//按帧缓冲坐标取源像素（1:1；钳到 [0, size-1]）
		//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时为点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色
	vec4 source = texelFetch(source_image, coord, 0);
	//逐通道映射
	vec3 mapped = vec3(aces(source.r), aces(source.g), aces(source.b));
	out_color = vec4(mapped, 1.0);
}
