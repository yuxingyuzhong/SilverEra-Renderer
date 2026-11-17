#version 450
/*
后处理片元着色器 · Reinhard 扩展白点色调映射（方向九第 740 轮）

口径
1. 输入源为上一级输出（RGBA8 附件采样），计算 Rec.709 亮度；
2. 扩展 Reinhard：L' = L × (1 + L / Lw²) / (1 + L)，白点 Lw = 2.0；
3. 按 L' / L 等比例缩放三通道，保持色相不变；
4. 输出钳到 [0,1]，alpha 恒 1；仅作新增预设，不替代既有算子。

典型值（中性白 1.0）：L' = 0.625 → sRGB 编码约 207，
介于 ACES（约 232）与 Hable filmic（约 186）之间。

命名说明：文件名取「扩展」以区别于历史遗留的简化 Reinhard 产物。
*/
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

//Rec.709 亮度权重
const vec3 LUMA_WEIGHT = vec3(0.2126, 0.7152, 0.0722);
//扩展 Reinhard 白点（公开口径）
const float WHITE_POINT = 2.0;

//扩展 Reinhard（按亮度缩放，保持色相）
vec3 reinhard_extended(vec3 color)
{
	//输入亮度
	float luminance = dot(color, LUMA_WEIGHT);
	//扩展 Reinhard 映射后的亮度
	float mapped = (luminance * (1.0 + luminance / (WHITE_POINT * WHITE_POINT))) / (1.0 + luminance);
	//按亮度比例缩放三通道（亮度为 0 时保持原色，避免除零）
	float scale = luminance > 1e-5 ? mapped / luminance : 1.0;
	return clamp(color * scale, 0.0, 1.0);
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
	out_color = vec4(reinhard_extended(source.rgb), 1.0);
}
