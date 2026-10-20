#version 450
/*
后处理片元着色器 · 亮度阈值（链式后处理的二级）
把「源纹理」（描述符集 1：上一级的输出）逐像素 1:1 取回后按 Rec.709 亮度（0.2126 / 0.7152 / 0.0722）
以 0.5 为阈值二值化为纯白 / 纯黑，得到饱和到色域两端的结果 —— 该结果与被采样附件的编码无关，
故适合用作「两级后处理叠加效果」的像素级验收（如 反色 → 亮度阈值：白底黑块）。

取像素用 texelFetch ＋ 帧缓冲坐标（gl_FragCoord.xy），与顶点朝向 / 纹理坐标无关，
故不受视口 Y 方向约定影响；要求源目标范围与本级宿范围一致（1:1），越界时钳到边缘。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//按帧缓冲坐标取源像素（1:1；钳到 [0, size-1]）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), source_size - ivec2(1));
	//取源颜色（sRGB 附件下 texelFetch 返回线性化值）
	vec3 source = texelFetch(source_image, coord, 0).rgb;
	//Rec.709 亮度
	float luma = dot(source, vec3(0.2126, 0.7152, 0.0722));
	//按 0.5 阈值二值化（亮 → 纯白、暗 → 纯黑；alpha 归一）
	out_color = vec4(luma >= 0.5 ? vec3(1.0) : vec3(0.0), 1.0);
}
