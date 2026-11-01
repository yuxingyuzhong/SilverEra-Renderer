#version 450
/*
后处理片元着色器 · 体积雾（第三百七十三轮；链式后处理预设）
仅依赖「源画面 + 场景深度」：由深度反投影求本像素到相机的视线距离，沿视线等距步进累积光学厚度，
按「1 − exp(−厚度)」的透明度把雾色叠到源画面；可选高度衰减（世界 Y 越高密度越低）。
参数经推送常量下发：
post_param.x = 密度（0 = 关闭，原样输出；0.001..1）
post_param.y = 高度衰减系数（0..4；> 0 时按 exp(−系数 × max(世界 Y, 0)) 逐点衰减）
post_param.z = 最大步进距离（世界单位，1..50）
post_param.w = 步数缩放（0.25..2，对应 8..48 步）
post_color.rgb = 雾颜色（后处理第三组推送常量，offset 112）
天空（深度 = 1）按远平面距离计步，故远景整体被雾覆盖，近景薄。
注意：MSAA 开启时主 pass 的多重采样深度由场景 pass 结束时按样本 0 解析到单采样深度图（第一百九十轮），故本段可直接使用。
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

//推送常量：源 / 宿尺寸（offset 80）、后处理参数（offset 96）与雾颜色（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 post_color;
} push;

//最大步进次数上限（固定，保证各硬件行为一致）
const int MAX_STEPS = 48;

void main()
{
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_size = textureSize(source_image, 0);
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	vec4 color = texelFetch(source_image, coord, 0);

	//密度为 0 时关闭（原样输出）
	float density = clamp(push.post_param.x, 0.0, 1.0);
	if (density <= 0.0001)
	{
		out_color = color;
		return;
	}

	//深度图尺寸与当前像素深度
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	float raw = texelFetch(depth_image, depth_coord, 0).r;
	//当前像素 uv（以深度图尺寸计）
	vec2 uv = (vec2(depth_coord) + vec2(0.5)) / vec2(depth_size);
	//由深度反投影重建世界坐标（投影为 [0,1] 深度范围，且已做 Y 翻转，故 NDC 各轴直接线性映射）
	vec2 ndc = uv * 2.0 - vec2(1.0);
	mat4 inverse_view_proj = inverse(scene.view_proj);
	vec3 camera = scene.camera_pos.xyz;
	//远平面距离（天空按它计步）
	float far_plane = max(scene.camera_range.y, 0.0001);
	//本像素到相机的视线距离
	float scene_distance = far_plane;
	//非天空：由世界坐标求距离
	if (raw < 0.9999)
	{
		vec4 world_h = inverse_view_proj * vec4(ndc, raw, 1.0);
		scene_distance = length(world_h.xyz / world_h.w - camera);
	}
	//视线方向：远平面上的同屏像素指向（与近处几何同向，不受单像素深度影响）
	vec4 far_h = inverse_view_proj * vec4(ndc, 1.0, 1.0);
	vec3 ray_dir = normalize(far_h.xyz / far_h.w - camera);

	//步进参数
	float max_distance = clamp(push.post_param.z, 1.0, 50.0);
	float travel = min(scene_distance, max_distance);
	int step_count = clamp(int(float(MAX_STEPS) * clamp(push.post_param.w, 0.25, 2.0)), 8, MAX_STEPS);
	float step_length = travel / float(step_count);
	float falloff = clamp(push.post_param.y, 0.0, 4.0);

	//沿视线累积光学厚度（中点采样）
	float optical = 0.0;
	for (int step = 0; step < step_count; ++step)
	{
		vec3 position = camera + ray_dir * (step_length * (float(step) + 0.5));
		//高度衰减：世界 Y 越高密度越低（0 以下不再衰减）
		float height_density = falloff > 0.0 ? exp(-falloff * max(position.y, 0.0)) : 1.0;
		optical += density * height_density * step_length;
	}
	//光学厚度 → 透明度（Beer–Lambert 近似）
	float alpha = 1.0 - exp(-optical);
	out_color = vec4(mix(color.rgb, push.post_color.rgb, alpha), color.a);
}
