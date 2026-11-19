#version 450
/*
后处理片元着色器 · 深度描边（第一百七十六轮；二次元风格化阶段 B）
用 5 点 Laplacian 在场景深度上求边缘，按强度把原色压暗，形成粗细均匀的深色轮廓线。
参数经推送常量 post_param 下发：x = 强度（0..1）、z = 深度差阈值（0..10，映射为 0.0005..0.05）、
w = 描边半径（0 或 1 = 单像素，2..4 = 更粗的轮廓；第二百二十轮）。

口径：
1. 邻域采样一律 texelFetch，越界钳到边缘；深度取 set1 binding 2；
2. 阈值为「常数 + 中心视深比例」自适应：远处深度变化平缓，阈值放宽，减少远景破碎线；
3. 强度 0 时压暗权重恒 0，输出与直通逐字节一致；alpha 恒置 1；
4. 半径缺省（0）归一到 1，故默认画面与引入半径前逐字节一致。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（上一级输出）
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（主 pass 后已转为着色器可读，空区域为 1.0）
layout(set = 1, binding = 2) uniform sampler2D depth_image;

//推送常量：源 / 宿尺寸（offset 80）、逐级后处理参数（offset 96）与描边色（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 outline_color;
} push;

//按偏移取深度（越界钳到边缘）
float depth_at(ivec2 coord, ivec2 size, ivec2 offset)
{
	ivec2 target = clamp(coord + offset, ivec2(0), size - ivec2(1));
	return texelFetch(depth_image, target, 0).r;
}

void main()
{
	//源纹理尺寸与宿像素 → 源像素映射比例
	ivec2 source_size = textureSize(source_image, 0);
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//深度图尺寸与坐标（与源同尺寸时 1:1）
	ivec2 depth_size = textureSize(depth_image, 0);
	vec2 depth_scale = push.size_xy.xy / vec2(depth_size);
	ivec2 depth_coord = clamp(ivec2(gl_FragCoord.xy * depth_scale), ivec2(0), depth_size - ivec2(1));
	//中心深度与 4 邻域
	float center = texelFetch(depth_image, depth_coord, 0).r;
	//描边半径（缺省 0 归一到 1，与单像素实现完全一致）
	int radius = int(clamp(round(push.post_param.w), 1.0, 4.0));
	float neighbors = depth_at(depth_coord, depth_size, ivec2(-radius, 0))
		+ depth_at(depth_coord, depth_size, ivec2(radius, 0))
		+ depth_at(depth_coord, depth_size, ivec2(0, -radius))
		+ depth_at(depth_coord, depth_size, ivec2(0, radius));
	//5 点 Laplacian（深度断层的响应）
	float laplacian = abs(center * 4.0 - neighbors);
	//阈值：0..10 映射到 0.0005..0.05，并按中心视深自适应
	float threshold = 0.0005 + clamp(push.post_param.z, 0.0, 10.0) * 0.005;
	threshold *= 1.0 + center * 2.0;
	//边缘权重与强度
	float edge = clamp(laplacian / max(threshold, 0.00001), 0.0, 1.0);
	float strength = clamp(push.post_param.x, 0.0, 1.0);
	//原色按权重混向描边色（alpha 透传源值，保留逐材质色调编号）
	vec4 source = texelFetch(source_image, source_coord, 0);
	out_color = vec4(mix(source.rgb, push.outline_color.rgb, clamp(edge * strength, 0.0, 1.0)), source.a);
}
