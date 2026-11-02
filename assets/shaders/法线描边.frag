#version 450
/*
后处理片元着色器 · 法线描边（第四百零七轮；方向三候选 D 的「深度重建法线」实现）
不新增 MRT：法线不来自 G-buffer，而是由场景深度反投影重建世界坐标，
再用中心像素与四邻域的世界坐标差分求法线；任一象限与中心法线偏差大即判为折边。
相比「深度描边」：折边两侧深度可能连续（同一物体上的棱线），深度响应弱而法线响应强，
故两者互补；本段的边缘权重取「法线边缘」与「深度边缘」的较大者，轮廓与折边一并覆盖。

参数经推送常量下发：
  post_param.x = 强度（0 = 关闭，原样输出；0..1）
  post_param.y = 法线阈值（0..1；缺省 0 或 ≤ 0 回落 0.10）
  post_param.z = 深度阈值（0..10，映射为 0.0005..0.05，与深度描边同口径）
  post_param.w = 描边半径（0 或 1 = 单像素，2..4 更粗）
  outline_color.rgb = 描边色

口径：
1. 天空像素（深度 1）不描边（原样输出）；几何与天空的交界处由几何一侧描边；
2. 逆投影矩阵每像素只算一次，四邻域复用之；
3. 强度 0 时混入权重恒 0，输出与直通逐字节一致；
4. 半径缺省（0）归一到 1，故默认画面与引入半径参数前一致。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（上一级输出）
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（主 pass 后已转为着色器可读，空区域为 1.0）
layout(set = 1, binding = 2) uniform sampler2D depth_image;

//场景 UBO（set 0 binding 0；只声明到相机近远平面，与主场景同一缓冲）
layout(set = 0, binding = 0) uniform Scene_Block
{
	mat4 view_proj;
	vec4 light_common;
	vec4 light_dir;
	vec4 light_color;
	vec4 camera_pos;
	vec4 point_pos_radius[8];
	vec4 point_color_intensity[8];
	vec4 point_spot_dir[8];
	vec4 point_spot_cone[8];
	vec4 fog_color_density;
	vec4 fog_range;
	vec4 ambient_color;
	vec4 ambient_ground;
	vec4 fill_dir_intensity;
	vec4 fill_color;
	float point_shininess[8];
	mat4 light_view_proj;
	vec4 shadow_params;
	float shadow_strength;
	vec4 pbr_params;
	mat4 light_view_proj_far;
	vec4 cascade_params;
	vec4 camera_range;
} scene;

//推送常量：源 / 宿尺寸（offset 80）、逐级后处理参数（offset 96）与描边色（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 outline_color;
} push;

//按偏移取深度（越界钳到边缘）
float depth_offset(ivec2 coord, ivec2 size, ivec2 offset)
{
	ivec2 target = clamp(coord + offset, ivec2(0), size - ivec2(1));
	return texelFetch(depth_image, target, 0).r;
}

//按偏移取世界坐标（深度反投影；逆矩阵由调用方传入，避免每点重算）
vec3 world_offset(ivec2 coord, ivec2 size, ivec2 offset, mat4 inverse_view_proj)
{
	ivec2 target = clamp(coord + offset, ivec2(0), size - ivec2(1));
	vec2 uv = (vec2(target) + vec2(0.5)) / vec2(size);
	float raw = texelFetch(depth_image, target, 0).r;
	vec4 world = inverse_view_proj * vec4(uv * 2.0 - vec2(1.0), raw, 1.0);
	return world.xyz / world.w;
}

//统一控制流内的安全归一化（长度过小时回退到给定法线，避免 NaN）
vec3 safe_normalize(vec3 value, vec3 fallback)
{
	float length = length(value);
	//长度过小：无法定义方向，回退
	if (length < 0.000001)
		return fallback;
	return value / length;
}

void main()
{
	//源纹理尺寸与宿像素 → 源像素映射比例
	ivec2 source_size = textureSize(source_image, 0);
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	vec4 source = texelFetch(source_image, source_coord, 0);

	//深度图尺寸与中心像素深度
	ivec2 depth_size = textureSize(depth_image, 0);
	vec2 depth_scale = push.size_xy.xy / vec2(depth_size);
	ivec2 center_coord = clamp(ivec2(gl_FragCoord.xy * depth_scale), ivec2(0), depth_size - ivec2(1));
	float center_raw = texelFetch(depth_image, center_coord, 0).r;

	//强度与关闭口径
	float strength = clamp(push.post_param.x, 0.0, 1.0);
	//强度 0 或天空像素：原样输出
	if (strength <= 0.0001 || center_raw >= 0.9999)
	{
		out_color = source;
		return;
	}

	//描边半径（缺省 0 归一到 1）
	int radius = int(clamp(round(push.post_param.w), 1.0, 4.0));
	//逆投影矩阵（同一帧同一相机，四邻域复用）
	mat4 inverse_view_proj = inverse(scene.view_proj);
	//中心与四邻域世界坐标
	vec3 center = world_offset(center_coord, depth_size, ivec2(0), inverse_view_proj);
	vec3 left = world_offset(center_coord, depth_size, ivec2(-radius, 0), inverse_view_proj);
	vec3 right = world_offset(center_coord, depth_size, ivec2(radius, 0), inverse_view_proj);
	vec3 up = world_offset(center_coord, depth_size, ivec2(0, radius), inverse_view_proj);
	vec3 down = world_offset(center_coord, depth_size, ivec2(0, -radius), inverse_view_proj);

	//中心法线（左右 / 上下差分叉积）
	vec3 center_normal = safe_normalize(cross(right - left, up - down), vec3(0.0, 1.0, 0.0));
	vec3 to_camera = scene.camera_pos.xyz - center;
	//朝向相机校正
	if (dot(center_normal, to_camera) < 0.0)
		center_normal = -center_normal;

	//四象限小三角形法线（中心与相邻两个邻域构成）
	vec3 corner_normals[4] = {
		safe_normalize(cross(right - center, up - center), center_normal),
		safe_normalize(cross(up - center, left - center), center_normal),
		safe_normalize(cross(left - center, down - center), center_normal),
		safe_normalize(cross(down - center, right - center), center_normal),
	};
	//取与中心法线最不一致的象限（任一折边都会体现在某一象限）
	float minimum_dot = 1.0;
	for (int index = 0; index < 4; ++index)
	{
		//象限法线朝向相机校正
		if (dot(corner_normals[index], to_camera) < 0.0)
			corner_normals[index] = -corner_normals[index];
		minimum_dot = min(minimum_dot, dot(corner_normals[index], center_normal));
	}
	//法线阈值（≤ 0 回落 0.10）
	float normal_threshold = push.post_param.y;
	//未设置：回落默认阈值
	if (normal_threshold <= 0.0)
		normal_threshold = 0.10;
	normal_threshold = clamp(normal_threshold, 0.005, 1.0);
	//法线边缘权重（1 − 最小点积 达到阈值即饱和）
	float normal_edge = clamp((1.0 - minimum_dot) / normal_threshold, 0.0, 1.0);

	//深度边缘权重（5 点 Laplacian，与深度描边同口径）
	float neighbors_raw = depth_offset(center_coord, depth_size, ivec2(-radius, 0))
		+ depth_offset(center_coord, depth_size, ivec2(radius, 0))
		+ depth_offset(center_coord, depth_size, ivec2(0, -radius))
		+ depth_offset(center_coord, depth_size, ivec2(0, radius));
	float laplacian = abs(center_raw * 4.0 - neighbors_raw);
	float depth_threshold = 0.0005 + clamp(push.post_param.z, 0.0, 10.0) * 0.005;
	depth_threshold *= 1.0 + center_raw * 2.0;
	float depth_edge = clamp(laplacian / max(depth_threshold, 0.00001), 0.0, 1.0);

	//两类边缘取较大者（轮廓与折边一并覆盖）
	float edge = max(normal_edge, depth_edge) * strength;
	out_color = vec4(mix(source.rgb, push.outline_color.rgb, clamp(edge, 0.0, 1.0)), source.a);
}
