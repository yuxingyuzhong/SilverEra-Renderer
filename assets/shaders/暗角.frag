#version 450
/*
后处理片元着色器 · 暗角（预设）
按「到画面中心归一化距离」做亮度衰减：中心保持原色、边缘压暗，衰减曲线用 smoothstep(inner, outer, r)。
半径以源纹理尺寸归一化（宽高各半为 1），故与目标尺寸无关。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；alpha 恒置 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

void main()
{
	//源纹理尺寸（用于越界钳制与半径归一化）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色
	//源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	vec3 source = source_texel.rgb;
	//到中心的归一化距离（0 = 中心、1 = 边缘中点）
	vec2 normalized = abs(vec2(coord) / vec2(source_size) * 2.0 - vec2(1.0));
	float radius = length(normalized);
	//衰减：inner 内不压暗、outer 外压到 0.25
	const float inner = 0.55;
	const float outer = 1.0;
	float weight = 1.0 - 0.75 * smoothstep(inner, outer, radius);
	out_color = vec4(source * weight, source_texel.a);
}
