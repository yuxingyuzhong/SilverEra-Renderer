#version 450
/*
后处理片元着色器 · 对比度（链式后处理预设）
逐通道围绕中灰 0.5 线性放大：rgb = clamp((rgb − 0.5) × k + 0.5, 0, 1)，k 取 1.5。
效果：亮部更亮、暗部更暗（端点方向确定，不依赖具体曲线）；alpha 恒置 1。

取像素用 texelFetch ＋ 帧缓冲坐标，与顶点朝向 / 纹理坐标无关；要求源与宿范围一致（1:1）。
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
	//取源颜色
	vec3 source = texelFetch(source_image, coord, 0).rgb;
	//对比度增益（固定常量）
	const float gain = 1.5;
	//围绕中灰放大并钳到 [0,1]
	vec3 adjusted = clamp((source - vec3(0.5)) * gain + vec3(0.5), vec3(0.0), vec3(1.0));
	out_color = vec4(adjusted, 1.0);
}
