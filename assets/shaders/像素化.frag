#version 450
/*
后处理片元着色器 · 像素化（第八十轮）
把画面按 N×N 个「宿像素块」采样同一源像素，形成马赛克效果。
块尺寸取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity），四舍五入后钳到 [1, 64]；
未专设时沿用全局强度默认 1.0 → 块尺寸 1（等价于原样输出）。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；
块内取块中心对应的源像素；越界钳边；alpha 透传块中心源值（保留逐材质色调编号）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 块尺寸（宿像素）、w 保留。
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
	//块尺寸（四舍五入后钳到 [1, 64]）
	float block = clamp(floor(push_post.post_param.z + 0.5), 1.0, 64.0);
	//本像素所属块的中心（宿像素坐标）
	vec2 block_center = (floor(gl_FragCoord.xy / block) + vec2(0.5)) * block;
	//映射到源像素并钳边
	ivec2 coord = clamp(ivec2(block_center * source_scale), ivec2(0), source_size - ivec2(1));
	//取块中心对应的源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	out_color = vec4(source_texel.rgb, source_texel.a);
}
