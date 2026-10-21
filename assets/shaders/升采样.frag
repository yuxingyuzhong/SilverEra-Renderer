#version 450
/*
后处理片元着色器 · 升采样（双线性近似）
把较小尺寸的源（如半分辨率模糊层）放大到宿尺寸：按「源 / 宿尺寸」推送求出源空间浮点坐标，
取 4 个邻域 texel 做双线性插值，减轻最近邻放大产生的块状边缘。

口径：源与宿尺寸可不同（通常源为宿的一半）；越界一律钳到边缘；alpha 恒置 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1 binding 0：上一级输出）
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
	//宿像素 → 源像素的映射比例
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	//源空间的浮点采样位置（像素中心对齐）
	vec2 position = gl_FragCoord.xy * source_scale - vec2(0.5);
	//左上邻域索引与小数部分
	ivec2 base = ivec2(floor(position));
	vec2 fraction = position - vec2(base);
	//四个邻域（钳到 [0, size-1]）
	ivec2 max_index = source_size - ivec2(1);
	vec3 c00 = texelFetch(source_image, clamp(base, ivec2(0), max_index), 0).rgb;
	vec3 c10 = texelFetch(source_image, clamp(base + ivec2(1, 0), ivec2(0), max_index), 0).rgb;
	vec3 c01 = texelFetch(source_image, clamp(base + ivec2(0, 1), ivec2(0), max_index), 0).rgb;
	vec3 c11 = texelFetch(source_image, clamp(base + ivec2(1, 1), ivec2(0), max_index), 0).rgb;
	//双线性插值
	vec3 top = mix(c00, c10, fraction.x);
	vec3 bottom = mix(c01, c11, fraction.x);
	out_color = vec4(mix(top, bottom, fraction.y), 1.0);
}
