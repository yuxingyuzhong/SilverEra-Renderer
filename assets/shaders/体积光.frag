#version 450
/*
后处理片元着色器 · 体积光（第三百八十二轮；屏幕空间光束 / God ray）
从每个像素沿「像素 → 光源屏幕位置」方向等距采样场景深度：只有天空（深度 ≥ 0.9999）的采样点才计入光路，
故天空方向的漏光形成光束、被几何遮挡处自然变暗；结果以加法叠加到源画面。
参数经推送常量下发：
post_param.x = 强度（0 = 关闭，原样输出；0..1）
post_param.y = 每步权重衰减（0.5..1.0；1.0 = 不衰减）
post_param.z = 采样缩放（0.25..2 → 8..48 步）
post_param.w = 光源屏幕位置的 y（0..1，默认取 0.9；x 固定 0.5）
post_color.rgb = 光束颜色（offset 112）
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

//推送常量：源 / 宿尺寸（offset 80）、后处理参数（offset 96）与光色（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 post_color;
} push;

//最大采样次数上限（固定，保证各硬件行为一致）
const int MAX_SAMPLES = 48;

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

	//深度图尺寸与当前像素 uv
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	vec2 uv = (vec2(depth_coord) + vec2(0.5)) / vec2(depth_size);
	//光源屏幕位置（x 固定中心、y 可配）与采样参数
	vec2 light_uv = vec2(0.5, clamp(push.post_param.w, 0.0, 1.0));
	int samples = clamp(int(float(MAX_SAMPLES) * clamp(push.post_param.z, 0.25, 2.0)), 8, MAX_SAMPLES);
	float decay = clamp(push.post_param.y, 0.5, 1.0);

	//沿「像素 → 光源」方向等距采样，仅天空采样点计入光路（越远权重越低）
	float illumination = 0.0;
	float weight = 1.0;
	for (int sample_index = 1; sample_index <= samples; ++sample_index)
	{
		vec2 sample_uv = uv + (light_uv - uv) * (float(sample_index) / float(samples));
		ivec2 sample_coord = clamp(ivec2(sample_uv * vec2(depth_size)), ivec2(0), depth_size - ivec2(1));
		float sample_depth = texelFetch(depth_image, sample_coord, 0).r;
		//天空（无几何遮挡）才允许光通过
		if (sample_depth >= 0.9999)
			illumination += weight;
		weight *= decay;
	}
	//归一化并乘强度
	float shaft = clamp(illumination / float(samples), 0.0, 1.0) * strength;
	//加法叠加光束颜色（不压暗原画面）
	out_color = vec4(clamp(color.rgb + push.post_color.rgb * shaft, 0.0, 1.0), color.a);
}
