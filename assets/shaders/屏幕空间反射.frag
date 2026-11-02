#version 450
/*
后处理片元着色器 · 屏幕空间反射（第一百六十七轮；链式后处理预设）
仅依赖「源画面 + 场景深度」：由深度反投影重建世界坐标，用屏幕空间导数求几何法线，
沿反射方向在世界空间步进并逐点重投影比较深度，命中即取该处颜色做反射混合。
参数经推送常量 post_param 下发：
x = 强度（0 = 关闭，原样输出）、y = 命中厚度（世界单位，0.05..2）、
z = 最大步进距离（世界单位，1..10）、w = 步数缩放（0.25..2，越大越细）。
天空（深度 = 1）与无深度目标的场景跳过本段，输出与源一致。
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

//推送常量：源 / 宿尺寸（offset 80）、后处理参数（offset 96）与边缘淡出宽度（offset 112）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
	//x = 屏幕边缘淡出宽度（第三百七十七轮；≤ 0 回落默认 0.15，0.001..0.5 可配）
	layout(offset = 112) vec4 post_color;
} push;

//最大步进次数上限（固定，保证各硬件行为一致）
const int MAX_STEPS = 48;

//把 [0,1] 的深度值换算为以相机为原点的距离（世界单位）
float linear_depth(float depth)
{
	float near_plane = max(scene.camera_range.x, 0.0001);
	float far_plane = max(scene.camera_range.y, near_plane + 0.0001);
	return (near_plane * far_plane) / max(far_plane - depth * (far_plane - near_plane), 0.0001);
}

//世界坐标 → 屏幕 uv（z 分量 = 投影后的深度 [0,1]；裁剪 w 非正时 z 置 -1 表示不可用）
vec3 world_to_screen(vec3 position)
{
	//世界坐标投影到裁剪空间
	vec4 clip = scene.view_proj * vec4(position, 1.0);
	//裁剪 w 非正（相机背后）不可用
	if (clip.w <= 0.0001)
		return vec3(0.0, 0.0, -1.0);
	//透视除法得 NDC，再映射到 uv（引擎投影已做 Y 翻转，故 NDC Y 与屏幕行同向）
	vec3 ndc = clip.xyz / clip.w;
	vec2 uv = ndc.xy * 0.5 + vec2(0.5);
	return vec3(uv, ndc.z);
}

void main()
{
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 source_size = textureSize(source_image, 0);
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//源颜色
	vec4 color = texelFetch(source_image, coord, 0);

	//深度图尺寸与当前像素深度
	ivec2 depth_size = textureSize(depth_image, 0);
	ivec2 depth_coord = clamp(coord, ivec2(0), depth_size - ivec2(1));
	float center_raw = texelFetch(depth_image, depth_coord, 0).r;
	//当前像素的 uv（以深度图尺寸计）
	vec2 uv = (vec2(depth_coord) + vec2(0.5)) / vec2(depth_size);
	//由深度反投影重建世界坐标（投影为 [0,1] 深度范围，且已做 Y 翻转，故 NDC 各轴直接线性映射）
	vec2 ndc = uv * 2.0 - vec2(1.0);
	vec4 world_h = inverse(scene.view_proj) * vec4(ndc, center_raw, 1.0);
	vec3 world = world_h.xyz / world_h.w;

	/*
	屏幕空间导数求几何法线
	导数必须在统一控制流中求值（此处尚未做任何逐像素提前返回），否则结果未定义；
	同一表面相邻像素的世界坐标差分叉积即法线，符号按「朝向相机」校正。
	*/
	vec3 normal = normalize(cross(dFdx(world), dFdy(world)));
	vec3 to_camera = scene.camera_pos.xyz - world;
	if (dot(normal, to_camera) < 0.0)
		normal = -normal;

	//强度为 0 时关闭（原样输出）
	float strength = clamp(push.post_param.x, 0.0, 1.0);
	if (strength <= 0.0001)
	{
		out_color = color;
		return;
	}
	//天空不参与反射
	if (center_raw >= 0.9999)
	{
		out_color = color;
		return;
	}
	//入射视线方向与反射方向
	vec3 view_dir = normalize(world - scene.camera_pos.xyz);
	vec3 reflect_dir = normalize(reflect(view_dir, normal));
	//反射方向进入表面内部（无有效反射）时跳过
	if (dot(reflect_dir, normal) <= 0.0001)
	{
		out_color = color;
		return;
	}

	//步进参数
	float thickness = clamp(push.post_param.y, 0.05, 2.0);
	float max_distance = clamp(push.post_param.z, 1.0, 10.0);
	int step_count = clamp(int(float(MAX_STEPS) * clamp(push.post_param.w, 0.25, 2.0)), 8, MAX_STEPS);
	float step_size = max_distance / float(step_count);
	//命中颜色与命中标记
	vec3 reflection = color.rgb;
	bool hit = false;
	/*
	分层步进（第四百零五轮；方向三候选 C）
	粗步进沿反射方向推进，首次出现「表面在射线点之前」（穿越）时，
	在上一采样点与本采样点之间做二分细化（6 次），再按厚度口径判定命中；
	粗搜索跨过长距离漏检、细化补足命中精度，等效步数不变。
	*/
	float previous_t = 0.0;
	for (int step = 1; step <= step_count; ++step)
	{
		float current_t = step_size * float(step);
		vec3 probe = world + reflect_dir * current_t;
		vec3 screen = world_to_screen(probe);
		//越出屏幕即终止（屏幕空间无该处信息）
		if (screen.z < 0.0 || screen.x < 0.0 || screen.x > 1.0 || screen.y < 0.0 || screen.y > 1.0)
			break;
		ivec2 probe_coord = clamp(ivec2(screen.xy * vec2(depth_size)), ivec2(0), depth_size - ivec2(1));
		float probe_raw = texelFetch(depth_image, probe_coord, 0).r;
		//探针处为天空则继续步进（并推进粗搜索下界）
		if (probe_raw >= 0.9999)
		{
			previous_t = current_t;
			continue;
		}
		//探针处表面视深 / 射线点视深（同以相机为原点的距离量纲比较）
		float surface_distance = linear_depth(probe_raw);
		float ray_distance = linear_depth(screen.z);
		//尚未穿越表面：继续粗步进
		if (surface_distance > ray_distance)
		{
			previous_t = current_t;
			continue;
		}
		//二分细化：在 [previous_t, current_t] 内逼近穿越点
		float low_t = previous_t;
		float high_t = current_t;
		float refined_surface = surface_distance;
		float refined_ray = ray_distance;
		for (int refine = 0; refine < 6; ++refine)
		{
			float mid_t = 0.5f * (low_t + high_t);
			vec3 mid_probe = world + reflect_dir * mid_t;
			vec3 mid_screen = world_to_screen(mid_probe);
			//细化点越屏：直接以当前高界作为命中点
			if (mid_screen.z < 0.0 || mid_screen.x < 0.0 || mid_screen.x > 1.0
				|| mid_screen.y < 0.0 || mid_screen.y > 1.0)
				break;
			ivec2 mid_coord = clamp(ivec2(mid_screen.xy * vec2(depth_size)), ivec2(0), depth_size - ivec2(1));
			float mid_raw = texelFetch(depth_image, mid_coord, 0).r;
			//细化点落在天空：穿越点在更远处
			if (mid_raw >= 0.9999)
			{
				low_t = mid_t;
				continue;
			}
			float mid_surface = linear_depth(mid_raw);
			float mid_ray = linear_depth(mid_screen.z);
			//仍未穿越：抬高下界；已穿越：压低上界
			if (mid_surface > mid_ray)
				low_t = mid_t;
			else
			{
				high_t = mid_t;
				refined_surface = mid_surface;
				refined_ray = mid_ray;
			}
		}
		//细化后按厚度口径判定（表面在射线点之前且差值不超过厚度）
		if (refined_surface <= refined_ray && refined_ray - refined_surface <= thickness)
		{
			vec3 hit_probe = world + reflect_dir * high_t;
			vec3 hit_screen = world_to_screen(hit_probe);
			ivec2 hit_coord = clamp(ivec2(hit_screen.xy * vec2(depth_size)), ivec2(0), depth_size - ivec2(1));
			reflection = texelFetch(source_image, clamp(hit_coord, ivec2(0), source_size - ivec2(1)), 0).rgb;
			hit = true;
			break;
		}
		//厚度不符：把本点作为新的粗搜索下界继续
		previous_t = current_t;
	}
	//未命中则不改变源画面
	if (!hit)
	{
		out_color = color;
		return;
	}
	//屏幕边缘淡出（避免边缘跳变）与强度混合
	float edge_width = push.post_color.x;
	//未设置（≤ 0）回落第三百六十七轮的默认宽度 0.15，保持旧画面逐字节不变
	if (edge_width <= 0.0)
		edge_width = 0.15;
	edge_width = min(edge_width, 0.5);
	float edge = smoothstep(0.0, edge_width, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
	vec3 blended = mix(color.rgb, reflection, strength * edge);
	out_color = vec4(clamp(blended, 0.0, 1.0), color.a);
}
