#version 450
/*
后处理片元着色器 · 屏幕空间环境光遮蔽（第一百四十七轮；链式后处理预设）
仅依赖深度缓冲：对当前像素取线性深度，与两圈各八方向（共 16 次）邻域深度比较，
邻域更近则计一次遮蔽；遮蔽比例乘强度后压暗源颜色。
参数经推送常量 post_param 下发：
x = 强度（0 = 关闭，原样输出）、y = 邻域半径（宿像素，1..64）、
z = 深度偏置（0..1，按当前线性深度缩放）、w = 幂次（0.5..4，越大越集中）。
天空（深度 = 1）与无深度目标的场景跳过本段，输出与源一致。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源画面（描述符集 1 binding 0）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（描述符集 1 binding 2）：主 pass 写入的单采样深度图
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

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

//把 [0,1] 的深度值换算为以相机为原点的距离（世界单位）
float linear_depth(float depth)
{
	float near_plane = scene.camera_range.x;
	float far_plane = scene.camera_range.y;
	return (near_plane * far_plane) / max(far_plane - depth * (far_plane - near_plane), 0.0001);
}

void main()
{
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_size = textureSize(source_image, 0);
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	vec4 color = texelFetch(source_image, coord, 0);

	//强度为 0 时关闭（原样输出）
	float strength = clamp(push.post_param.x, 0.0, 1.0);
	if (strength <= 0.0001)
	{
		out_color = color;
		return;
	}
	//深度图尺寸与中心深度
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	float center_raw = texelFetch(depth_image, depth_coord, 0).r;
	float center = linear_depth(center_raw);
	//天空（深度 1）不参与遮蔽
	if (center_raw >= 0.9999)
	{
		out_color = color;
		return;
	}
	/*
	两圈八方向采样
	半径以宿像素计；邻域比中心更近（差值超过偏置）即视为遮蔽来源；
	偏置按中心深度缩放，远处自动放宽，避免深度量化误判。
	*/
	float radius = clamp(push.post_param.y, 1.0, 64.0);
	float bias = clamp(push.post_param.z, 0.0, 1.0) * (center * 0.05) + 0.005;
	int occluded = 0;
	int total = 0;
	for (int ring = 1; ring <= 2; ++ring)
	{
		float ring_radius = radius * float(ring) * 0.5;
		for (int direction = 0; direction < 8; ++direction)
		{
			//固定八方向
			float angle = 0.78539816 * float(direction);
			vec2 offset = vec2(cos(angle), sin(angle)) * ring_radius;
			ivec2 neighbor_coord = clamp(depth_coord + ivec2(offset), ivec2(0), depth_size - ivec2(1));
			float neighbor_raw = texelFetch(depth_image, neighbor_coord, 0).r;
			//天空邻域不计入遮蔽
			if (neighbor_raw >= 0.9999)
				continue;
			if (linear_depth(neighbor_raw) < center - bias)
				++occluded;
			++total;
		}
	}
	//无有效邻域
	if (total == 0)
	{
		out_color = color;
		return;
	}
	//遮蔽比例 → 压暗（幂次越大，遮蔽越集中）
	float occlusion = strength * float(occluded) / float(total);
	float factor = pow(clamp(1.0 - occlusion, 0.0, 1.0), clamp(push.post_param.w, 0.5, 4.0));
	color.rgb *= factor;
	out_color = vec4(clamp(color.rgb, 0.0, 1.0), color.a);
}
