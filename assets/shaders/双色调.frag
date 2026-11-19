#version 450
/*
后处理片元着色器 · 双色调（第一百一十九轮；链式后处理预设）
按亮度把画面映射到「暗部深蓝 → 亮部暖橙」的双色渐变，并按强度与原画面混合：
1. x = 对比度（钳制 0.1..4，绕中灰缩放亮度）；
2. z = 混入强度（0 = 原样、1 = 完全双色调）。
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
	//源纹理尺寸与像素映射
	ivec2 source_size = textureSize(source_image, 0);
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色与亮度
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 color_texel = texelFetch(source_image, coord, 0);
	vec3 color = color_texel.rgb;
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));

	//对比度与混入强度
	float contrast = clamp(push.post_param.x, 0.1, 4.0);
	float strength = clamp(push.post_param.z, 0.0, 1.0);
	//亮度绕中灰缩放后映射到双色渐变
	float level = clamp((luma - 0.5) * contrast + 0.5, 0.0, 1.0);
	vec3 dark = vec3(0.05, 0.08, 0.20);
	vec3 light = vec3(1.0, 0.85, 0.55);
	vec3 mapped = mix(dark, light, level);
	vec3 result = mix(color, mapped, strength);

	out_color = vec4(clamp(result, 0.0, 1.0), color_texel.a);
}
