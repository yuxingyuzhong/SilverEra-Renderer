#version 450
/*
后处理片元着色器 · 径向模糊（第一百零九轮；链式后处理预设）
以屏幕中心为原点，沿「像素 → 中心」方向做 8 次均匀采样并平均，形成径向拖影。
参数经推送常量 post_param.x 下发（阈值参数），映射为 0..0.1 的径向位移比例；
未设置该级参数时使用全局泛光阈值（默认 0.6 → 位移 0.06）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

void main()
{
	//宿 UV（0..1）与源 / 宿比例
	vec2 base_uv = gl_FragCoord.xy / push.size_xy.zw;
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	//径向位移比例（限制在 0..0.1，避免过度采样）
	float strength = clamp(push.post_param.x * 0.1, 0.0, 0.1);
	//本像素相对中心的方向向量
	vec2 direction = (base_uv - vec2(0.5)) * strength;

	//8 次均匀采样（i = 0 为原像素，i = 7 为最靠中心）
	vec3 sum = vec3(0.0);
	const int samples = 8;
	for (int index = 0; index < samples; ++index)
	{
		vec2 uv = base_uv - direction * (float(index) / float(samples - 1));
		uv = clamp(uv * source_scale, vec2(0.0), vec2(1.0));
		sum += texture(source_image, uv).rgb;
	}
	out_color = vec4(sum / float(samples), 1.0);
}
