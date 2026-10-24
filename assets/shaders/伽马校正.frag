#version 450
/*
后处理片元着色器 · 伽马校正（第八十九轮）
逐通道幂变换：out = pow(clamp(source, 0, 1), 1 / γ)。γ > 1 提亮中间调、γ < 1 压暗中间调；
端点 0 / 1 不变。γ 取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity），
钳到 [0.2, 5]；未专设时沿用全局强度默认 1.0（等价于原样输出）。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；越界钳边；alpha 恒 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 伽马值、w 保留。
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
	//源颜色（钳到 [0,1] 保证幂运算合法）
	vec3 source = clamp(texelFetch(source_image, coord, 0).rgb, 0.0, 1.0);
	//伽马值（钳到 [0.2, 5]；非数按 1 处理）
	float gamma = push_post.post_param.z;
	if (!(gamma == gamma))
		gamma = 1.0;
	gamma = clamp(gamma, 0.2, 5.0);
	//逐通道幂变换
	out_color = vec4(pow(source, vec3(1.0 / gamma)), 1.0);
}
