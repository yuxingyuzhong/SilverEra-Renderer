#version 450
/*
后处理片元着色器 · 色差（第七十六轮）
径向横向色差：以画面中心为原点，按「到中心归一化距离 × 强度」把红通道沿径向外扩、
蓝通道沿径向内收采样，绿通道保持原位；中心零偏移、越靠近边缘分离越大。
强度取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity，单位 = 半径 1 处的像素偏移；
未专设时沿用全局泛光强度的默认 1.0）。

取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射（1:1 时为恒等）；
越界一律钳边；alpha 按中心像素透传（保留逐材质色调编号）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 色差强度（像素）、w 保留。
*/
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	vec4 post_param;
} push_post;

void main()
{
	//源纹理尺寸（用于越界钳制与半径归一化）
	ivec2 source_size = textureSize(source_image, 0);
	//最大合法坐标
	ivec2 maximum = source_size - ivec2(1);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push_post.size_xy.xy / push_post.size_xy.zw;
	//本像素的源空间连续坐标（保留亚像素位置，使偏移可小于 1 像素）
	vec2 position = gl_FragCoord.xy * source_scale;
	//中心采样坐标（钳到合法范围）
	ivec2 center = clamp(ivec2(position), ivec2(0), maximum);
	/*
	到画面中心的归一化向量
	按「像素中心」折算（＋0.5），使 偶数宽高 时正中心恰为原点；半宽 / 半高归一为 1。
	*/
	vec2 from_center = (position + vec2(0.5)) / vec2(source_size) * 2.0 - vec2(1.0);
	//归一化半径（0 = 中心、1 = 边缘中点）
	float radius = length(from_center);
	//径向单位方向（中心处为零向量 → 不偏移）
	vec2 direction = radius > 0.000001 ? from_center / radius : vec2(0.0);
	//像素偏移量（强度 = 半径 1 处的偏移；中心恒为 0，负强度按 0 处理）
	float shift = max(push_post.post_param.z, 0.0) * radius;
	//红通道沿径向外扩、蓝通道沿径向内收（越界钳边）
	ivec2 red_coord = clamp(ivec2(position + direction * shift), ivec2(0), maximum);
	ivec2 blue_coord = clamp(ivec2(position - direction * shift), ivec2(0), maximum);
	//中心纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 center_texel = texelFetch(source_image, center, 0);
	//三通道各自取自错位后的采样点
	vec3 separated = vec3(
		texelFetch(source_image, red_coord, 0).r,
		center_texel.g,
		texelFetch(source_image, blue_coord, 0).b);
	out_color = vec4(separated, center_texel.a);
}
