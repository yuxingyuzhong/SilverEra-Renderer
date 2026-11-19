#version 450
/*
后处理片元着色器 · 色调分离（第七十八轮）
逐通道按「色阶数 N」量化：out = floor(c × N + 0.5) / N，形成明显色带（posterize）。
N 取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity），四舍五入后钳到 [2, 64]；
未专设时沿用全局泛光强度默认 1.0 → 钳为 2（二值化风格）。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；越界钳边；alpha 恒 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 色阶数、w 保留。
*/
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	vec4 post_param;
} push_post;

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
	//色阶数（四舍五入后钳到 [2, 64]）
	float levels = clamp(floor(push_post.post_param.z + 0.5), 2.0, 64.0);
	//逐通道量化（结果仍落在 [0, 1]）
	vec3 posterized = floor(source * levels + 0.5) / levels;
	out_color = vec4(posterized, source_texel.a);
}
