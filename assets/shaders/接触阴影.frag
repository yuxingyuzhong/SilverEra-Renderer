#version 450
/*
后处理片元着色器 · 屏幕空间接触阴影（第六百七十三轮；方向八 D3）
由场景深度重建世界坐标，沿主光方向（-light_dir）等距步进；把每个采样点投影回屏幕并读取深度，
若该像素的深度图表面比采样点更靠近相机，则视为被遮挡，累积接触阴影并压暗源画面。
参数经推送常量下发：
post_param.x = 强度（0 = 关闭，原样输出；0..1）
post_param.y = 最大步进距离（世界单位，0.05..20）
post_param.z = 步数缩放（0.25..2，对应 4..32 步）
post_param.w = 深度偏置（NDC，0.0005..0.05；抑制自阴影）
post_color.rgb = 阴影色调（默认黑，即直接压暗）
天空像素、方向光关闭与强度 0 时一律原样输出（与引入前逐字节一致）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源画面（描述符集 1 binding 0）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（描述符集 1 binding 2）：主 pass 写入的单采样深度图
layout(set = 1, binding = 2) uniform sampler2D depth_image;

//场景 UBO（set 0 binding 0；前缀与体积雾一致，仅读取视图投影 / 光照 / 相机位置与近远平面）
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

//推送常量：源 / 宿尺寸（offset 80）、后处理参数（offset 96）与阴影色调（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	layout(offset = 112) vec4 post_color;
} push;

//步数上限（固定，保证各硬件行为一致）
const int MAX_STEPS = 32;

/*
接触遮挡采样（第六百七十五轮；相位可调；第六百八十一轮增厚度剔除）
phase 为步内相对偏移：0.5 = 固定中点（与 673 逐字节一致）；
thickness 为允许的深度差上限（NDC）：超过即视为远处前景而非接触遮挡，不计入。
返回遮挡样本数（0..step_count）。
*/
float contact_occlusion(vec3 world_pos, vec3 to_light, int step_count, float step_length,
	float bias, float thickness, ivec2 depth_size, float phase, mat4 view_proj)
{
	float occluded = 0.0;
	for (int step = 1; step <= step_count; ++step)
	{
		vec3 sample_pos = world_pos + to_light * (step_length * (float(step) - 0.5 + phase));
		vec4 clip = view_proj * vec4(sample_pos, 1.0);
		//样本位于相机背后则跳过
		if (clip.w <= 0.000001)
			continue;
		vec3 sample_ndc = clip.xyz / clip.w;
		//屏幕外样本跳过
		if (abs(sample_ndc.x) > 1.0 || abs(sample_ndc.y) > 1.0
			|| sample_ndc.z < 0.0 || sample_ndc.z > 1.0)
			continue;
		//投影回深度图像素
		ivec2 sample_coord = clamp(ivec2((sample_ndc.xy * 0.5 + 0.5) * vec2(depth_size)),
			ivec2(0), depth_size - ivec2(1));
		float stored = texelFetch(depth_image, sample_coord, 0).r;
		//深度图为天空则不算遮挡
		if (stored >= 0.9999)
			continue;
		//深度差（正 = 深度图表面比采样点更近）
		float difference = sample_ndc.z - stored;
		/*
		深度图表面比采样点更近 → 该样本被遮挡；
		超过厚度上限则视为远处前景（非接触遮挡），不计入（第六百八十一轮）。
		*/
		if (difference > bias && difference <= thickness)
			occluded += 1.0;
	}
	return occluded;
}

/*
逐像素抖动相位（第六百七十五轮；交错梯度噪声）
由像素坐标与种子共同决定，取值 (0, 1)；同帧内空间均匀铺开、跨帧随种子变化。
*/
float jitter_phase(vec2 frag_coord, float seed)
{
	float ign = fract(52.9829189 * fract(dot(frag_coord + vec2(5.588238) * seed,
		vec2(0.06711056, 0.00583715))));
	return ign;
}

void main()
{
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_size = textureSize(source_image, 0);
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	vec4 color = texelFetch(source_image, coord, 0);
	//强度 0 或方向光关闭：原样输出
	float strength = clamp(push.post_param.x, 0.0, 1.0);
	if (strength <= 0.0001 || scene.light_dir.w <= 0.0)
	{
		out_color = color;
		return;
	}

	//深度图尺寸与当前像素深度
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	float raw = texelFetch(depth_image, depth_coord, 0).r;
	//天空不参与接触阴影
	if (raw >= 0.9999)
	{
		out_color = color;
		return;
	}
	//由深度反投影重建本像素的世界坐标（与体积雾同口径）
	vec2 uv = (vec2(depth_coord) + vec2(0.5)) / vec2(depth_size);
	vec2 ndc = uv * 2.0 - vec2(1.0);
	mat4 inverse_view_proj = inverse(scene.view_proj);
	vec4 world_h = inverse_view_proj * vec4(ndc, raw, 1.0);
	vec3 world_pos = world_h.xyz / world_h.w;
	//沿主光方向步进（light_dir 为传播方向，取反得到指向光源）
	vec3 to_light = normalize(-scene.light_dir.xyz);
	float max_distance = clamp(push.post_param.y, 0.05, 20.0);
	int step_count = clamp(int(16.0 * clamp(push.post_param.z, 0.25, 2.0)), 4, MAX_STEPS);
	float bias = clamp(push.post_param.w, 0.0005, 0.05);
	float step_length = max_distance / float(step_count);
	/*
	厚度阈值（第六百八十一轮）
	以「最大步进距离 / 相机近远跨度」估算 NDC 厚度，仅统计厚度内的遮挡样本，
	避免把远处前景当作接触阴影；钳到 [0.002, 0.2]。
	*/
	float depth_span = max(scene.camera_range.y - scene.camera_range.x, 0.001);
	float thickness = clamp(max_distance / depth_span, 0.002, 0.2);
	/*
	抖动相位（第六百七十五轮）
	post_color.a 为种子：0（默认）= 固定中点采样，与 673 逐字节一致；
	> 0 时按像素与种子抖动，并以互补相位二次采样平均，摊平层状伪影。
	*/
	float jitter_seed = max(push.post_color.a, 0.0);
	float phase = 0.5;
	if (jitter_seed > 0.0)
		phase = jitter_phase(gl_FragCoord.xy, jitter_seed);
	//遮挡计数
	float occluded = contact_occlusion(world_pos, to_light, step_count, step_length,
		bias, thickness, depth_size, phase, scene.view_proj);
	//互补相位平均（仅抖动开启时）
	if (jitter_seed > 0.0)
	{
		float paired = contact_occlusion(world_pos, to_light, step_count, step_length,
			bias, thickness, depth_size, 1.0 - phase, scene.view_proj);
		occluded = 0.5 * (occluded + paired);
	}
	//遮挡比例 → 阴影强度
	float occlusion = occluded / float(step_count);
	vec3 shadowed = mix(color.rgb, push.post_color.rgb, strength * occlusion);
	out_color = vec4(shadowed, color.a);
}
