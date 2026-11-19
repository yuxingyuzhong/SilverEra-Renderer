#version 450
/*
后处理片元着色器 · 分级调色（第一百二十二轮；lift / gamma / gain ＋ 饱和度）
ASC CDL 风格的解析式色彩校正，作为后处理链的普通一级：
1. 增益与黑位：color = color × gain ＋ lift（先把整体亮度缩放，再抬升黑位）；
2. 伽马：color = pow(max(color, 0), 1 / gamma)（gamma > 1 提亮暗部、< 1 压暗）；
3. 饱和度：以 Rec.709 亮度为灰轴插值（0 = 灰度、1 = 原样、> 1 增强）。
参数经推送常量 post_param 下发：x = lift、y = gamma、z = gain、w = saturation；
恒等参数为 (0, 1, 1, 1)；未设置该级参数时沿用全局泛光参数（故使用本预设前应显式设置该级参数）。
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
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色（alpha 透传：第 789 轮，保留场景 alpha 上的逐材质色调编号）
	vec4 source = texelFetch(source_image, coord, 0);
	vec3 color = source.rgb;

	//参数归一（与门面合法域一致）
	float lift = clamp(push.post_param.x, -1.0, 1.0);
	float gamma = clamp(push.post_param.y, 0.01, 10.0);
	float gain = clamp(push.post_param.z, 0.0, 10.0);
	float saturation = clamp(push.post_param.w, 0.0, 4.0);

	//增益与黑位抬升
	color = color * gain + vec3(lift);
	//伽马（按 1 / gamma 幂；负值先钳零避免 pow 未定义）
	color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));
	//饱和度（以 Rec.709 亮度为灰轴）
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	color = mix(vec3(luma), color, saturation);

	out_color = vec4(clamp(color, 0.0, 1.0), source.a);
}
