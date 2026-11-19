#version 450
/*
后处理片元着色器 · 反色（链式后处理的一级）
把「源纹理」（描述符集 1：上一级 / 场景 pass 的输出）逐像素 1:1 取回后按 1 - rgb 取反，
alpha 置为 1，从而得到可用于叠加验收的一级效果。

取像素用 texelFetch ＋ 帧缓冲坐标（gl_FragCoord.xy），与顶点朝向 / 纹理坐标无关，
故不受视口 Y 方向约定影响；要求源目标范围与本级宿范围一致（1:1），越界时钳到边缘。
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
	//取源颜色（sRGB 附件下 texelFetch 返回线性化值）
	vec4 source = texelFetch(source_image, coord, 0);
	//反色输出（alpha 归一）
	//透传源 alpha（第 790 轮：逐材质色调编号不在中间级丢失）
	out_color = vec4(vec3(1.0) - source.rgb, source.a);
}
