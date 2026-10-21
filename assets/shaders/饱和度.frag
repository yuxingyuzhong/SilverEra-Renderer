#version 450
/*
后处理片元着色器 · 饱和度（链式后处理预设）
按 Rec.709 亮度把颜色向「远离灰轴」的方向推：rgb = mix(vec3(luma), rgb, s)，s 取 1.5。
效果：色偏区域的通道差（如 b − r）被放大，纯灰（三通道相等）保持不变；alpha 恒置 1。

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
	vec3 source = texelFetch(source_image, coord, 0).rgb;
	//Rec.709 亮度（灰轴上的对应点）
	float luma = dot(source, vec3(0.2126, 0.7152, 0.0722));
	//饱和度倍率（固定常量）
	const float saturation = 1.5;
	//沿「灰轴 → 原色」方向外推
	vec3 adjusted = clamp(mix(vec3(luma), source, saturation), vec3(0.0), vec3(1.0));
	out_color = vec4(adjusted, 1.0);
}
