#version 450
/*
后处理片元着色器 · 灰度（链式后处理预设）
把源纹理逐像素 1:1 取回后按 Rec.709 亮度加权去色（保持 alpha 为 1）。

用途：作为后处理链的一级（可单独使用，也可与描边 / 色调映射串联）。
取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）。
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
	//Rec.709 亮度
	float luma = dot(source.rgb, vec3(0.2126, 0.7152, 0.0722));
	//灰度输出
	//透传源 alpha（第 790 轮：逐材质色调编号不在中间级丢失）
	out_color = vec4(vec3(luma), source.a);
}
