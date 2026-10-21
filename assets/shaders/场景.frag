#version 450
//场景片元着色器
//来自顶点着色器的纹理坐标
layout(location = 0) in vec2 in_uv;
//来自顶点着色器的世界空间法线（零向量 = 未提供）
layout(location = 1) in vec3 in_normal;
//来自顶点着色器的世界空间位置（第三十一轮）
layout(location = 2) in vec3 in_world;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//反照率贴图（描述符集 1）：线性过滤 ＋ 重复寻址
layout(set = 1, binding = 0) uniform sampler2D albedo;

//场景缓冲（描述符集 0）：视图投影矩阵 ＋ 光照参数 ＋ 相机位置 ＋ 点光源数组，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	//x = 环境光、y = 高光强度、z = 光泽度、w = 有效点光源数量（第三十二轮）
	vec4 light_common;
	//xyz = 方向光传播方向，w = 方向光强度（0 = 关闭）
	vec4 light_dir;
	//rgb = 方向光颜色，w = 距离衰减模式（0 = 线性、1 = 平方反比；第三十六轮）
	vec4 light_color;
	//xyz = 相机世界位置，w 保留
	vec4 camera_pos;
	//点光源位置（xyz）＋ 衰减半径（w），最多 4 个
	vec4 point_pos_radius[4];
	//点光源颜色（rgb）＋ 强度（w），最多 4 个
	vec4 point_color_intensity[4];
	//聚光灯方向（xyz = 光传播方向）＋ 有效标志（w > 0.5 表示该槽为聚光灯；第三十四轮）
	vec4 point_spot_dir[4];
	//聚光灯内外锥角余弦（x = 内锥、y = 外锥；第三十四轮）
	vec4 point_spot_cone[4];
	//雾色（rgb）＋ 密度（w；0 = 关闭；第三十八轮）
	vec4 fog_color_density;
	//雾距离区间（x = 起始、y = 结束；第三十八轮）
	vec4 fog_range;
	//环境光颜色（rgb；第四十轮）
	vec4 ambient_color;
} scene;

//点光源数量上限（与门面 Impl::POINT_LIGHT_MAX 一致）
const int POINT_LIGHT_MAX = 4;

void main()
{
	vec4 albedo_color = texture(albedo, in_uv);
	/*
	多光源累加（第二十九轮方向光 / 第三十一轮点光源与高光 / 第三十二轮多光源）
	未提供法线（长度近零）时按「朝向相机」处理，等价于满照（辐射度 = 1），
	保证既有只写位置 / 纹理坐标的网格画面不变。
	*/
	vec3 radiance = vec3(1.0);
	float specular = 0.0;
	//若提供了法线
	if (length(in_normal) > 0.000001)
	{
		vec3 normal = normalize(in_normal);
		//视线方向（相机 → 片元 的反向，即片元指向相机）
		vec3 to_view = normalize(scene.camera_pos.xyz - in_world);
		//环境光作为基底辐射度（颜色 × 强度；第四十轮引入颜色）
			radiance = scene.ambient_color.rgb * scene.light_common.x;
		/*
		方向光（强度为 0 视为关闭）
		高光按 Blinn-Phong：H = normalize(L + V)，取各光源贡献的最大值（不做叠加，避免过曝）
		*/
		if (scene.light_dir.w > 0.0)
		{
			vec3 to_light = -scene.light_dir.xyz;
			float ndotl = max(dot(normal, to_light), 0.0);
			radiance += scene.light_color.rgb * scene.light_dir.w * ndotl;
			vec3 halfway = normalize(to_light + to_view);
			float spec_base = max(dot(normal, halfway), 0.0);
			specular = max(specular, pow(spec_base, max(scene.light_common.z, 1.0)));
		}
		/*
		点光源（数量由 light_common.w 给出，线性衰减；超过上限的登记在门面侧即被拒绝）
		*/
		int point_count = int(scene.light_common.w);
		for (int index = 0; index < POINT_LIGHT_MAX; ++index)
		{
			//超出有效数量的槽位不参与
			if (index >= point_count)
				break;
			vec3 offset = scene.point_pos_radius[index].xyz - in_world;
			float distance = length(offset);
			//避免零距离除零
			vec3 to_light = distance > 0.000001 ? offset / distance : vec3(0.0, 0.0, 1.0);
			/*
			距离衰减（第三十六轮可切换模式）
			mode 0（默认）：线性，距离达到半径时降为 0；mode 1：平方反比 1/(1+(d/r)²)，不设硬截断；
			半径为 0 时两种模式均视为无衰减。模式随 light_color.w 逐帧下发，点光源与聚光灯共用。
			*/
			float radius = scene.point_pos_radius[index].w;
			float attenuation = 1.0;
			if (radius > 0.000001)
			{
				float normalized = distance / radius;
				attenuation = scene.light_color.w > 0.5
					? 1.0 / (1.0 + normalized * normalized)
					: clamp(1.0 - normalized, 0.0, 1.0);
			}
			/*
			聚光灯锥形衰减（第三十四轮）
			普通点光源的有效标志为 0 → cone 恒 1；聚光灯按「光轴与光源→片元方向」夹角取平滑锥形：
			夹角小于内角 → 1，大于外角 → 0，其间 smoothstep 过渡。
			*/
			float cone = 1.0;
			if (scene.point_spot_dir[index].w > 0.5)
			{
				//光源 → 片元 的方向（to_light 为片元 → 光源，取反即得）
				vec3 light_to_fragment = -to_light;
				float cos_angle = dot(light_to_fragment, scene.point_spot_dir[index].xyz);
				cone = smoothstep(scene.point_spot_cone[index].y, scene.point_spot_cone[index].x, cos_angle);
			}
			float ndotl = max(dot(normal, to_light), 0.0);
			radiance += scene.point_color_intensity[index].rgb * scene.point_color_intensity[index].w
				* ndotl * attenuation * cone;
			//高光同样取最大值；衰减或锥形为 0 的光源不产生高光
			vec3 halfway = normalize(to_light + to_view);
			float spec_base = max(dot(normal, halfway), 0.0);
			specular = max(specular, pow(spec_base, max(scene.light_common.z, 1.0)) * attenuation * cone);
		}
	}
	//最终颜色：反照率 × 累加辐射度 ＋ 高光（强度由 light_common.y 统一缩放），钳制到 [0,1]
	vec3 lit = albedo_color.rgb * radiance + vec3(specular * scene.light_common.y);
	/*
	距离雾（第三十八轮；密度 0 = 关闭）
	按「相机 → 片元」距离在 [起始, 结束] 间归一化，取 t² × 密度 混合雾色；
	雾前的照度与雾后的混合都在线性空间完成，最后统一钳制。
	*/
	vec3 final_rgb = lit;
	if (scene.fog_color_density.w > 0.0)
	{
		float view_distance = length(scene.camera_pos.xyz - in_world);
		//雾区间跨度（避免除零）
		float fog_span = max(scene.fog_range.y - scene.fog_range.x, 0.0001);
		float fog_t = clamp((view_distance - scene.fog_range.x) / fog_span, 0.0, 1.0);
		float fog_factor = clamp(fog_t * fog_t * scene.fog_color_density.w, 0.0, 1.0);
		final_rgb = mix(lit, scene.fog_color_density.rgb, fog_factor);
	}
	out_color = vec4(clamp(final_rgb, 0.0, 1.0), albedo_color.a);
}
