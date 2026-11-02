#version 450
/*
后处理片元着色器 · 体积光（第三百八十二轮；屏幕空间光束 / God ray）
从每个像素沿「像素 → 光源屏幕位置」方向等距采样场景深度：只有天空（深度 ≥ 0.9999）的采样点才计入光路，
故天空方向的漏光形成光束、被几何遮挡处自然变暗；结果以加法叠加到源画面。
参数经推送常量下发：
post_param.x = 强度（0 = 关闭，原样输出；0..1）
post_param.y = 每步权重衰减（0.5..1.0；1.0 = 不衰减）
post_param.z = 采样缩放（0.25..2 → 8..48 步）
post_param.w = 光源屏幕位置的 y（0..1，默认取 0.9）
post_color.rgb = 光束颜色（offset 112）
post_color.a = 光源屏幕位置 x（≤ 0 回落 0.5）
post_extra.x = 阴影开关（第四百零二轮；> 0.5 且场景阴影开启时按方向光阴影图判定空气遮挡）
post_extra.y = 额外深度偏置（NDC 量纲，0..0.05；默认 0）
注意：MSAA 开启时主 pass 的多重采样深度由场景 pass 结束时按样本 0 解析到单采样深度图（第一百九十轮），故本段可直接使用。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源画面（描述符集 1 binding 0）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（描述符集 1 binding 2）：主 pass 写入的单采样深度图
layout(set = 1, binding = 2) uniform sampler2D depth_image;
//方向光阴影图（描述符集 1 binding 4；第三百九十九轮接通，第四百零二轮启用）
layout(set = 1, binding = 4) uniform sampler2D shadow_image;

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
	//天空盒参数（只为对齐 std140 偏移；本段不读取）
	vec4 sky_params;
	//第三级级联矩阵与参数（第四百零二轮；与 场景.frag 同布局）
	mat4 light_view_proj_far2;
	vec4 cascade_params2;
} scene;

//推送常量：源 / 宿尺寸（offset 80）、后处理参数（96）、光色（112）、逐级额外四参（128）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 post_color;
	layout(offset = 128) vec4 post_extra;
} push;

//最大采样次数上限（固定，保证各硬件行为一致）
const int MAX_SAMPLES = 48;

//按级序取光源空间矩阵（与 场景.frag 同口径）
mat4 cascade_matrix_of(int index)
{
	if (index == 1)
		return scene.light_view_proj_far;
	if (index >= 2)
		return scene.light_view_proj_far2;
	return scene.light_view_proj;
}

//按级序取本片深度跨度（与 场景.frag 同口径）
float cascade_span_of(int index)
{
	if (index == 1)
		return scene.cascade_params.w;
	if (index >= 2)
		return scene.cascade_params2.y;
	return scene.cascade_params.z;
}

/*
单点阴影判定（第四百零二轮）
返回 1 = 受光、0 = 遮蔽；阴影未启用、位于光源背后或阴影图外一律视为受光。
与 场景.frag 的 shadow_sample 同口径（级联选择 / 切片映射 / 打包深度解码），但不加法线偏移与软边邻域：
空气点没有法线，且体积光每步一次采样已足够形成条纹。
*/
float shadow_at(vec3 world_pos, float extra_bias)
{
	//场景阴影未启用
	if (scene.shadow_params.x < 0.5)
		return 1.0;
	int count = int(scene.cascade_params.x + 0.5);
	float view_distance = distance(world_pos, scene.camera_pos.xyz);
	int index = 0;
	if (count >= 2 && view_distance > scene.cascade_params.y)
		index = 1;
	if (count >= 3 && view_distance > scene.cascade_params2.x)
		index = 2;
	vec4 light_clip = cascade_matrix_of(index) * vec4(world_pos, 1.0);
	//位于光源背后
	if (light_clip.w <= 0.000001)
		return 1.0;
	vec3 ndc = light_clip.xyz / light_clip.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
	//阴影图外一律受光
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
		return 1.0;
	//多级阴影图沿 X 切片（Y 收缩到顶格）
	if (count == 2)
	{
		uv.x = uv.x * 0.5 + (index == 1 ? 0.5 : 0.0);
		uv.y = uv.y * 0.5;
	}
	else if (count >= 3)
	{
		float slice = 1.0 / 3.0;
		uv.x = uv.x * slice + float(index) * slice;
		uv.y = uv.y * slice;
	}
	int size = int(scene.shadow_params.z + 0.5);
	ivec2 texel = clamp(ivec2(uv * float(size)), ivec2(0), ivec2(size - 1));
	//解包 32 位深度（与 场景.frag 同格式）
	vec4 packed = texelFetch(shadow_image, texel, 0);
	float stored = dot(packed, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
	//探针在光源空间的深度（含场景偏置与本次额外偏置）
	float current = ndc.z - scene.shadow_params.y - extra_bias;
	return current <= stored ? 1.0 : 0.0;
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

	//深度图尺寸与当前像素 uv
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	vec2 uv = (vec2(depth_coord) + vec2(0.5)) / vec2(depth_size);
	//光源屏幕位置（第三百九十轮：x 由颜色第四分量给出，≤ 0 回落 0.5；y 取通用四参 w）
	float light_x = push.post_color.a > 0.0 ? clamp(push.post_color.a, 0.0, 1.0) : 0.5;
	vec2 light_uv = vec2(light_x, clamp(push.post_param.w, 0.0, 1.0));
	int samples = clamp(int(float(MAX_SAMPLES) * clamp(push.post_param.z, 0.25, 2.0)), 8, MAX_SAMPLES);
	float decay = clamp(push.post_param.y, 0.5, 1.0);

	//沿「像素 → 光源」方向等距采样，仅天空采样点计入光路（越远权重越低）
	float illumination = 0.0;
	float weight = 1.0;
	/*
	阴影开关（第四百零二轮）
	post_extra.x > 0.5 且场景阴影开启时才做空气遮挡判定；关闭时循环与原实现逐语句一致（画面逐字节不变）。
	额外偏置取 post_extra.y（NDC 量纲，钳到 0..0.05）。
	*/
	bool shadow_on = push.post_extra.x > 0.5 && scene.shadow_params.x > 0.5;
	float extra_bias = clamp(push.post_extra.y, 0.0, 0.05);
	mat4 inv_view_proj = mat4(1.0);
	if (shadow_on)
		inv_view_proj = inverse(scene.view_proj);
	for (int sample_index = 1; sample_index <= samples; ++sample_index)
	{
		vec2 sample_uv = uv + (light_uv - uv) * (float(sample_index) / float(samples));
		ivec2 sample_coord = clamp(ivec2(sample_uv * vec2(depth_size)), ivec2(0), depth_size - ivec2(1));
		float sample_depth = texelFetch(depth_image, sample_coord, 0).r;
		//天空（无几何遮挡）才允许光通过
		if (sample_depth >= 0.9999)
		{
			//默认受光（阴影关闭时即此值）
			float lit = 1.0;
			if (shadow_on)
			{
				//把该采样点反投影回世界坐标，再做方向光阴影判定
				vec2 sample_ndc = sample_uv * 2.0 - 1.0;
				vec4 world_h = inv_view_proj * vec4(sample_ndc, sample_depth, 1.0);
				vec3 probe = world_h.xyz / world_h.w;
				lit = shadow_at(probe, extra_bias);
			}
			illumination += weight * lit;
		}
		else if (shadow_on)
		{
			/*
			几何采样点（第四百零二轮）
			光路在此终结：该点受光则说明光路依达此处（计入本步权重），
			该点处于阴影中则光路被遮挡、本像素后续采样不再贡献（截断）。
			阴影强度 scene.shadow_strength 在 1 与判定值之间插值（0 = 阴影无效）。
			*/
			vec2 sample_ndc = sample_uv * 2.0 - 1.0;
			vec4 world_h = inv_view_proj * vec4(sample_ndc, sample_depth, 1.0);
			vec3 probe = world_h.xyz / world_h.w;
			float lit = shadow_at(probe, extra_bias);
			lit = mix(1.0, lit, clamp(scene.shadow_strength, 0.0, 1.0));
			illumination += weight * lit;
			//被遮蔽：光路不再延伸
			if (lit <= 0.0001)
				break;
		}
		weight *= decay;
	}
	//归一化并乘强度
	float shaft = clamp(illumination / float(samples), 0.0, 1.0) * strength;
	//加法叠加光束颜色（不压暗原画面）
	out_color = vec4(clamp(color.rgb + push.post_color.rgb * shaft, 0.0, 1.0), color.a);
}
