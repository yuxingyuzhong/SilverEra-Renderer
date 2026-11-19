#version 450
/*
后处理片元着色器 · 胶片颗粒（第七十九轮）
以屏幕空间哈希噪声叠加轻微颗粒：out = clamp(source + (hash − 0.5) × strength, 0, 1)。
哈希只依赖像素坐标（同一帧内稳定、逐帧不闪烁），噪声零均值，故整幅平均亮度基本不变。
强度取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity），钳到 [0, 0.5]；
未专设时沿用全局强度默认 1.0 → 钳为 0.5（明显颗粒）。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；越界钳边；alpha 恒 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 颗粒强度、w 保留。
*/
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	vec4 post_param;
} push_post;

/*
屏幕空间哈希（输入像素坐标，输出 [0, 1) 的伪随机值）
Dave Hoskins 风格：先按分数部分打散，再以点积混合，最后取分数部分。
*/
float hash12(vec2 position)
{
	vec3 p3 = fract(vec3(position.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push_post.size_xy.xy / push_post.size_xy.zw;
	//中心采样坐标（钳到合法范围）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	vec3 source = source_texel.rgb;
	//颗粒强度（钳到 [0, 0.5]；负值按 0）
	float strength = clamp(push_post.post_param.z, 0.0, 0.5);
	//零均值噪声（同一像素坐标恒定）
	float noise = hash12(vec2(coord)) - 0.5;
	//叠加颗粒并钳到色域
	out_color = vec4(clamp(source + noise * strength, 0.0, 1.0), source_texel.a);
}
