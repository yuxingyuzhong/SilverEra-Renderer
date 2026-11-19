#version 450
/*
后处理片元着色器 · 色彩校正（第一百零八轮；链式后处理预设）
按「对比度 / 饱和度 / 亮度」三项逐像素校正：
1. 对比度以 0.5 为轴缩放（> 1 增强、< 1 压缩）；
2. 饱和度以 Rec.709 亮度为灰轴插值（0 = 灰度、1 = 原样）；
3. 亮度为线性增益。
参数经推送常量 post_param 下发：x = 对比度、y = 饱和度、z = 亮度、w 忽略；
未设置该级参数时使用全局泛光参数（故使用本预设前应显式设置该级参数）。
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
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 color_texel = texelFetch(source_image, coord, 0);
	vec3 color = color_texel.rgb;

	//参数归一
	float contrast = clamp(push.post_param.x, 0.1, 4.0);
	float saturation = clamp(push.post_param.y, 0.0, 4.0);
	float brightness = clamp(push.post_param.z, 0.0, 4.0);

	//对比度（以中灰为轴）
	color = (color - vec3(0.5)) * contrast + vec3(0.5);
	//饱和度（以 Rec.709 亮度为灰轴）
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	color = mix(vec3(luma), color, saturation);
	//亮度增益
	color = color * brightness;

	out_color = vec4(clamp(color, 0.0, 1.0), color_texel.a);
}
