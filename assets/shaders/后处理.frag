#version 450
/*
后处理片元着色器（同帧多 pass 的第二个 pass）
把「源纹理」（描述符集 1：前一 pass 的离屏目标颜色）逐像素 1:1 取回并写出，
从而完成「场景 pass 渲染到目标 → 本 pass 呈现到交换链」的同帧多 pass 闭环。

取像素用 texelFetch ＋ 帧缓冲坐标（gl_FragCoord.xy），与顶点朝向 / 纹理坐标无关，
故不受视口 Y 方向约定影响；要求源目标范围与呈现范围一致（1:1），越界时钳到边缘。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：前一 pass 的离屏目标颜色
layout(set = 1, binding = 0) uniform sampler2D source_image;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//按帧缓冲坐标取源像素（1:1；钳到 [0, size-1]）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), source_size - ivec2(1));
	out_color = texelFetch(source_image, coord, 0);
}
