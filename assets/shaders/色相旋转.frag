#version 450
/*
后处理片元着色器 · 色相旋转（第八十七轮）
把源颜色转入 HSV，色相角整体旋转 offset 度后转回 RGB；饱和度与明度保持不变。
偏移角取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity，单位：度），
钳到 [-360, 360]；未专设时沿用全局强度默认 1.0（约 1°，人眼几乎不可察，等价于不变）。
注意：门面 `post_stage_param_set` 要求 intensity ∈ (0, 100)，故本预设单次可设的有效角为 (0°, 100°)。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；越界钳边；alpha 透传源值（保留逐材质色调编号）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 色相偏移角（度）、w 保留。
*/
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	vec4 post_param;
} push_post;

//RGB → HSV（h ∈ [0,1)）
vec3 rgb_to_hsv(vec3 color)
{
	float maximum = max(color.r, max(color.g, color.b));
	float minimum = min(color.r, min(color.g, color.b));
	float delta = maximum - minimum;
	//明度
	float value = maximum;
	//饱和度（黑为 0）
	float saturation = maximum > 0.000001 ? delta / maximum : 0.0;
	//色相
	float hue = 0.0;
	if (delta > 0.000001)
	{
		if (maximum == color.r)
			hue = (color.g - color.b) / delta;
		else if (maximum == color.g)
			hue = 2.0 + (color.b - color.r) / delta;
		else
			hue = 4.0 + (color.r - color.g) / delta;
		hue = fract(hue / 6.0);
	}
	return vec3(hue, saturation, value);
}

//HSV → RGB（h ∈ [0,1)）
vec3 hsv_to_rgb(vec3 hsv)
{
	float hue = fract(hsv.x) * 6.0;
	float saturation = clamp(hsv.y, 0.0, 1.0);
	float value = clamp(hsv.z, 0.0, 1.0);
	//所在色扇与扇内小数
	float sector = floor(hue);
	float fraction = hue - sector;
	float p = value * (1.0 - saturation);
	float q = value * (1.0 - saturation * fraction);
	float t = value * (1.0 - saturation * (1.0 - fraction));
	//按扇区取分量
	if (sector < 1.0)
		return vec3(value, t, p);
	if (sector < 2.0)
		return vec3(q, value, p);
	if (sector < 3.0)
		return vec3(p, value, t);
	if (sector < 4.0)
		return vec3(p, q, value);
	if (sector < 5.0)
		return vec3(t, p, value);
	return vec3(value, p, q);
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
	//色相偏移（度 → 转数，钳到 [-360, 360] 度）
	float offset_degrees = clamp(push_post.post_param.z, -360.0, 360.0);
	//转 HSV → 旋转色相 → 转回 RGB
	vec3 hsv = rgb_to_hsv(source);
	hsv.x = fract(hsv.x + offset_degrees / 360.0);
	out_color = vec4(hsv_to_rgb(hsv), source_texel.a);
}
