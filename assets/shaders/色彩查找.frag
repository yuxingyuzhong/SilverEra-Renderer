#version 450
/*
后处理片元着色器 · 3D LUT 色彩查找（第一百四十四轮；链式后处理预设）
查找表以 2D 条带存储：宽 = edge × edge、高 = edge；
条带内 X = 层号 × edge ＋ 红索引、Y = 绿索引，蓝决定层号。
采样一律用 texelFetch ＋ 手工三线性：共享采样器为线性重复寻址，
直接采样会在层与层边界串色，故不依赖硬件过滤。
参数经推送常量 post_param 下发：x = 混合强度（0 = 关闭该级、1 = 全量；未设该级参数时取全局泛光阈值）。
查找表尺寸非法（高 < 2、宽 ≠ 高²）时原样输出。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1 binding 0）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;
//3D LUT 条带（描述符集 1 binding 3）
layout(set = 1, binding = 3) uniform sampler2D lut_image;

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

//按条带布局手工三线性采样
vec3 lut_sample(vec3 color, int edge)
{
	//各通道映射到 [0, edge − 1] 的连续索引
	float r = clamp(color.r, 0.0, 1.0) * float(edge - 1);
	float g = clamp(color.g, 0.0, 1.0) * float(edge - 1);
	float b = clamp(color.b, 0.0, 1.0) * float(edge - 1);
	//低 / 高索引与层内插值权重
	int r0 = int(floor(r));
	int g0 = int(floor(g));
	int b0 = int(floor(b));
	int r1 = min(r0 + 1, edge - 1);
	int g1 = min(g0 + 1, edge - 1);
	int b1 = min(b0 + 1, edge - 1);
	float rf = r - float(r0);
	float gf = g - float(g0);
	float bf = b - float(b0);
	//蓝层 b0 的四个角（红 × 绿）
	vec3 c000 = texelFetch(lut_image, ivec2(b0 * edge + r0, g0), 0).rgb;
	vec3 c100 = texelFetch(lut_image, ivec2(b0 * edge + r1, g0), 0).rgb;
	vec3 c010 = texelFetch(lut_image, ivec2(b0 * edge + r0, g1), 0).rgb;
	vec3 c110 = texelFetch(lut_image, ivec2(b0 * edge + r1, g1), 0).rgb;
	//蓝层 b1 的四个角
	vec3 c001 = texelFetch(lut_image, ivec2(b1 * edge + r0, g0), 0).rgb;
	vec3 c101 = texelFetch(lut_image, ivec2(b1 * edge + r1, g0), 0).rgb;
	vec3 c011 = texelFetch(lut_image, ivec2(b1 * edge + r0, g1), 0).rgb;
	vec3 c111 = texelFetch(lut_image, ivec2(b1 * edge + r1, g1), 0).rgb;
	//先在红方向、再在绿方向插值，最后在蓝方向插值
	vec3 low_slice = mix(mix(c000, c100, rf), mix(c010, c110, rf), gf);
	vec3 high_slice = mix(mix(c001, c101, rf), mix(c011, c111, rf), gf);
	return mix(low_slice, high_slice, bf);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	//取源颜色
	vec3 color = clamp(source_texel.rgb, 0.0, 1.0);
	//查找表尺寸
	ivec2 lut_size = textureSize(lut_image, 0);
	int edge = lut_size.y;
	//尺寸合法才做查找（高 ≥ 2、宽 = 高²）
	if (edge >= 2 && lut_size.x == edge * edge)
	{
		vec3 graded = lut_sample(color, edge);
		//混合强度：0 = 关闭该级、1 = 全量；未设该级参数时沿用全局泛光参数（x 默认 0.5）
		float strength = clamp(push.post_param.x, 0.0, 1.0);
		color = mix(color, graded, strength);
	}
	out_color = vec4(clamp(color, 0.0, 1.0), source_texel.a);
}
