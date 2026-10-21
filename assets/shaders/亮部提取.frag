#version 450
/*
后处理片元着色器 · 亮部提取（泛光链的一级）
把源纹理逐像素 1:1 取回，按 Rec.709 亮度取「亮部」：权重在阈值上下各一个软膝间用 smoothstep 过渡，
输出为原色乘以该权重 —— 亮部保留原色、暗部归零，供后续模糊级做泛光扩散。

口径：阈值 0.6、软膝 0.1（线性亮度 ≥ 0.7 全保留、≤ 0.5 全归零）；alpha 恒置 1。
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
	//Rec.709 亮度
	float luma = dot(source, vec3(0.2126, 0.7152, 0.0722));
	//亮度阈值与软膝宽度（线性空间）
	const float threshold = 0.6;
	const float knee = 0.1;
	//软膝权重（0 = 暗部、1 = 亮部）
	float weight = smoothstep(threshold - knee, threshold + knee, luma);
	//按权重保留亮部
	out_color = vec4(source * weight, 1.0);
}
