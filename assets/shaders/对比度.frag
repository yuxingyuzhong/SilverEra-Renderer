#version 450
/*
后处理片元着色器 · 对比度（链式后处理预设）
逐通道围绕中灰 0.5 线性放大：rgb = clamp((rgb − 0.5) × k + 0.5, 0, 1)，k 取 1.5。
效果：亮部更亮、暗部更暗（端点方向确定，不依赖具体曲线）；alpha 恒置 1。

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
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	vec3 source = source_texel.rgb;
	//对比度增益（固定常量）
	const float gain = 1.5;
	//围绕中灰放大并钳到 [0,1]
	vec3 adjusted = clamp((source - vec3(0.5)) * gain + vec3(0.5), vec3(0.0), vec3(1.0));
	out_color = vec4(adjusted, source_texel.a);
}
