#version 450
//场景片元着色器
//来自顶点着色器的纹理坐标
layout(location = 0) in vec2 in_uv;
//来自顶点着色器的世界空间法线（零向量 = 未提供）
layout(location = 1) in vec3 in_normal;
//来自顶点着色器的世界空间位置（第三十一轮）
layout(location = 2) in vec3 in_world;
//逐物体材质（第六十八轮；x = 粗糙度缩放、y = 金属度、z = 覆盖标志、w 保留）
layout(location = 3) flat in vec4 in_material;

/*
逐物体反照率色调（第六十九轮）
推送常量前 64 字节为占位矩阵（模型矩阵已由实例属性承载），64..79 为色调 vec4；
门面在非文字路径**恒推送**该块（默认白色），故未显式设置色调时画面逐字节不变。
*/
layout(push_constant) uniform Push_Tint
{
	layout(offset = 64) vec4 tint;
} push_tint;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//反照率贴图（描述符集 1）：线性过滤 ＋ 重复寻址
layout(set = 1, binding = 0) uniform sampler2D albedo;
//阴影图（第五十二轮；set 1 binding 1，RGBA8 打包 32 位深度，以 texelFetch 读取规避插值）
layout(set = 1, binding = 1) uniform sampler2D shadow_map;

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
	//xyz = 相机世界位置，w = 方向光逐光高光缩放（第五十轮）
	vec4 camera_pos;
	//点光源位置（xyz）＋ 衰减半径（w），最多 8 个（第六十二轮由 4 扩为 8）
	vec4 point_pos_radius[8];
	//点光源颜色（rgb）＋ 强度（w），最多 8 个
	vec4 point_color_intensity[8];
	//聚光灯方向（xyz = 光传播方向）＋ 有效标志（w > 0.5 表示该槽为聚光灯；第三十四轮）
	vec4 point_spot_dir[8];
	//聚光灯内外锥角余弦（x = 内锥、y = 外锥；z = 逐光源衰减模式、w = 逐盏高光缩放；第三十四 / 四十六 / 四十九轮）
	vec4 point_spot_cone[8];
	//雾色（rgb）＋ 密度（w；0 = 关闭；第三十八轮）
	vec4 fog_color_density;
	//雾距离区间（x = 起始、y = 结束；第三十八轮）
	vec4 fog_range;
	//环境光颜色（rgb；第四十轮）
	vec4 ambient_color;
	//半球环境光（rgb = 地面颜色、w = 混合强度 0..1；第四十七轮）
	vec4 ambient_ground;
	//补光方向（xyz = 光传播方向）＋ 强度（w；0 = 关闭；第四十七轮）
	vec4 fill_dir_intensity;
	//补光颜色（rgb；第四十七轮）
	vec4 fill_color;
	//逐盏高光光泽度（std140：每盏占 16 字节；0 = 跟随全局；第五十一轮；第六十二轮扩为 8 盏）
	float point_shininess[8];
	//光源空间视图投影矩阵（第五十二轮；配合 shadow_params 做方向光阴影）
	mat4 light_view_proj;
	//阴影参数（x = 启用 >0.5、y = 深度偏置、z = 阴影图边长（像素）、w = 软度 0..1；第五十二／五十三轮）
	vec4 shadow_params;
	//阴影强度（0 = 阴影无效、1 = 全遮蔽；第五十三轮）
	float shadow_strength;
	//PBR-lite 参数（第六十四轮；x = 启用 > 0.5、y = 粗糙度缩放（默认 1）、z = 金属度（第六十五轮，0..1）、w 保留）
	vec4 pbr_params;
} scene;

//点光源数量上限（与门面 Impl::POINT_LIGHT_MAX 一致；第六十二轮由 4 扩为 8）
const int POINT_LIGHT_MAX = 8;

/*
逐物体 PBR 粗糙度缩放（第六十八轮）
覆盖标志有效时取实例属性（钳到合法域），否则沿用场景 UBO 的全局 pbr_params.y。
*/
float material_roughness_scale()
{
	return in_material.z > 0.5 ? clamp(in_material.x, 0.05, 4.0) : max(scene.pbr_params.y, 0.05);
}

/*
逐物体金属度（第六十八轮）
覆盖标志有效时取实例属性（钳到 0..1），否则沿用场景 UBO 的全局 pbr_params.z。
*/
float material_metallic()
{
	return in_material.z > 0.5 ? clamp(in_material.y, 0.0, 1.0) : clamp(scene.pbr_params.z, 0.0, 1.0);
}

/*
PBR-lite 粗糙度换算（第六十四轮；第六十八轮增加逐物体缩放入参）
由 Blinn-Phong 光泽度换算 GGX 粗糙度：roughness = sqrt(2 / (shininess + 2))，
再乘缩放入参并钳到 [0.03, 1]。
*/
float pbr_roughness(float shininess, float scale)
{
	float roughness = sqrt(2.0 / (max(shininess, 1.0) + 2.0));
	return clamp(roughness * max(scale, 0.05), 0.03, 1.0);
}

/*
PBR-lite 菲涅尔基色（第六十五轮；第六十八轮金属度改为入参）
金属度（0..1）把 F0 从介电 0.04 插值到反照率色；金属表面镜面带基色。
*/
vec3 pbr_f0(vec3 albedo, float metallic)
{
	return mix(vec3(0.04), albedo, clamp(metallic, 0.0, 1.0));
}

/*
GGX 镜面反射（第六十四轮；第六十五轮改为 RGB 菲涅尔）
分布项 GGX（α = roughness²）、几何项 Smith-Schlick、菲涅尔项 Schlick（F0 由金属度给出）；
返回已含 1 / (4·N·L·N·V) 的镜面值（可大于 1，由上层强度钳制）。
*/
vec3 ggx_specular(vec3 normal, vec3 to_view, vec3 to_light, float roughness, vec3 f0)
{
	float ndotl = max(dot(normal, to_light), 0.0);
	float ndotv = max(dot(normal, to_view), 0.0);
	//任一侧背光则无镜面
	if (ndotl <= 0.0 || ndotv <= 0.0)
		return vec3(0.0);
	vec3 halfway = normalize(to_light + to_view);
	float ndoth = max(dot(normal, halfway), 0.0);
	float vdoth = max(dot(to_view, halfway), 0.0);
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denominator = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
	//法线分布（GGX）
	float distribution = alpha2 / (3.14159265 * denominator * denominator);
	//几何遮蔽（Smith-Schlick 近似）
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	float geometry = (ndotl / (ndotl * (1.0 - k) + k)) * (ndotv / (ndotv * (1.0 - k) + k));
	//菲涅尔（Schlick；F0 由金属度插值）
	vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - vdoth, 5.0);
	return fresnel * (distribution * geometry / (4.0 * ndotl * ndotv));
}

/*
阴影深度比较（第五十三轮）
在阴影图纹素坐标处取一次打包深度并解包，返回 current <= stored ? 1 : 0。
*/
float shadow_compare(ivec2 texel, float current, int size)
{
	//越界钳边
	ivec2 clamped = clamp(texel, ivec2(0), ivec2(size - 1));
	vec4 packed = texelFetch(shadow_map, clamped, 0);
	//解包 32 位深度
	float stored = dot(packed, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
	//片元比阴影图记录更近则受光，否则遮蔽
	return current <= stored ? 1.0 : 0.0;
}

/*
方向光阴影系数（第五十二轮；第五十三轮增软度与强度；第五十九轮加法线偏移）
shadow_params：x = 启用（> 0.5 生效）、y = 深度偏置、z = 阴影图边长（像素）、w = 软度（0 = 单点硬边、≥0.5 = 3×3 平均）。
返回 0 = 完全遮蔽、1 = 完全受光；阴影图范围外或 w ≤ 0 时视为受光；
最终按 scene.shadow_strength 在 1 与遮蔽系数之间插值（0 = 阴影无效、1 = 全遮蔽）。
*/
float shadow_factor(vec3 world_pos, vec3 normal)
{
	//未启用阴影
	if (scene.shadow_params.x < 0.5)
		return 1.0;
	/*
	法线偏移（第五十九轮）
	沿片元法线推出一小段后再取阴影深度，抵消深度量化与掠射角造成的自阴影（shadow acne）；
	偏移量 = 深度偏置 × 光源正交深度跨度（近 1、远 60 → 59），使世界量级与 NDC 偏置量一致；
	法线为零向量（未提供法线）时不偏移，保持既有网格行为。
	*/
	vec3 sample_pos = world_pos;
	if (length(normal) > 0.000001)
		sample_pos += normalize(normal) * (scene.shadow_params.y * 59.0);
	//光源空间裁剪坐标
	vec4 light_clip = scene.light_view_proj * vec4(sample_pos, 1.0);
	//位于光源背后
	if (light_clip.w <= 0.000001)
		return 1.0;
	vec3 ndc = light_clip.xyz / light_clip.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
	//阴影图外一律受光（避免边界拉伸）
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
		return 1.0;
	int size = int(scene.shadow_params.z + 0.5);
	//纹素坐标（最近点，避免插值得到无意义的打包字节）
	ivec2 texel = ivec2(clamp(uv * float(size), vec2(0.0), vec2(float(size - 1))));
	//当前片元在光源空间的深度（Vulkan 深度范围 [0,1]）减去偏置
	float current = ndc.z - scene.shadow_params.y;
	//遮蔽系数（软度 0 = 单点；>0 按软度映射邻域半径 1..3 → 3×3 / 5×5 / 7×7）
	float factor;
	if (scene.shadow_params.w <= 0.0)
		factor = shadow_compare(texel, current, size);
	else
	{
		//邻域半径（0.25 以下 → 1、0.75 以下 → 2、其余 → 3）
		float softness = clamp(scene.shadow_params.w, 0.0, 1.0);
		int radius = softness <= 0.25 ? 1 : (softness <= 0.75 ? 2 : 3);
		float sum = 0.0;
		int samples = 0;
		for (int offset_y = -radius; offset_y <= radius; ++offset_y)
			for (int offset_x = -radius; offset_x <= radius; ++offset_x)
			{
				sum += shadow_compare(texel + ivec2(offset_x, offset_y), current, size);
				++samples;
			}
		factor = sum / float(samples);
	}
	//按阴影强度在「无阴影」与「全遮蔽」之间插值
	return mix(1.0, factor, clamp(scene.shadow_strength, 0.0, 1.0));
}

void main()
{
	//采样反照率并乘逐物体色调（默认白色 → 不变）
	vec4 albedo_color = texture(albedo, in_uv) * push_tint.tint;
	/*
	多光源累加（第二十九轮方向光 / 第三十一轮点光源与高光 / 第三十二轮多光源）
	未提供法线（长度近零）时按「朝向相机」处理，等价于满照（辐射度 = 1），
	保证既有只写位置 / 纹理坐标的网格画面不变。
	*/
	vec3 radiance = vec3(1.0);
	vec3 specular = vec3(0.0);
	//若提供了法线
	if (length(in_normal) > 0.000001)
	{
		vec3 normal = normalize(in_normal);
		//视线方向（相机 → 片元 的反向，即片元指向相机）
		vec3 to_view = normalize(scene.camera_pos.xyz - in_world);
		/*
		环境光作为基底辐射度（颜色 × 强度；第四十轮引入颜色；第四十七轮支持半球混合）
		半球混合：法线朝上取 ambient_color、朝下取 ambient_ground.rgb，
		按「法线 y 分量映射到 0..1」线性插值后再按 ambient_ground.w 与单一颜色混合；
		w = 0 时退化为单一 ambient_color（既有画面逐字节不变）。
		*/
		vec3 ambient_rgb = scene.ambient_color.rgb;
		//若启用半球环境光
		if (scene.ambient_ground.w > 0.0)
		{
			//法线 y ∈ [-1, 1] 映射到 [0, 1]
			float up_mix = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
			//地面 → 天顶插值，再与单一颜色按强度混合
			vec3 hemisphere_rgb = mix(scene.ambient_ground.rgb, scene.ambient_color.rgb, up_mix);
			ambient_rgb = mix(scene.ambient_color.rgb, hemisphere_rgb, scene.ambient_ground.w);
		}
		radiance = ambient_rgb * scene.light_common.x;
		/*
		方向光（强度为 0 视为关闭）
		高光按 Blinn-Phong：H = normalize(L + V)，取各光源贡献的最大值（不做叠加，避免过曝）
		*/
		if (scene.light_dir.w > 0.0)
		{
			vec3 to_light = -scene.light_dir.xyz;
			float ndotl = max(dot(normal, to_light), 0.0);
			//方向光阴影系数（第五十二轮；未启用时恒 1；第五十九轮传入法线做偏移）
			float dir_shadow = shadow_factor(in_world, normal);
			radiance += scene.light_color.rgb * scene.light_dir.w * ndotl * dir_shadow;
			//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
			if (scene.pbr_params.x > 0.5)
				specular = max(specular, ggx_specular(normal, to_view, to_light,
					pbr_roughness(max(scene.light_common.z, 1.0), material_roughness_scale()),
					pbr_f0(albedo_color.rgb, material_metallic()))
					* scene.camera_pos.w * dir_shadow * scene.light_dir.w);
			else
			{
				vec3 halfway = normalize(to_light + to_view);
				float spec_base = max(dot(normal, halfway), 0.0);
				specular = max(specular, vec3(pow(spec_base, max(scene.light_common.z, 1.0)) * scene.camera_pos.w * dir_shadow));
			}
		}
		/*
		补光（第四十七轮；第二盏方向光，强度为 0 视为关闭）
		只与方向光做漫反射累加；高光同样取最大值（不与主光叠加，避免过曝）。
		*/
		if (scene.fill_dir_intensity.w > 0.0)
		{
			vec3 fill_to_light = -scene.fill_dir_intensity.xyz;
			float fill_ndotl = max(dot(normal, fill_to_light), 0.0);
			radiance += scene.fill_color.rgb * scene.fill_dir_intensity.w * fill_ndotl;
			//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
			if (scene.pbr_params.x > 0.5)
				specular = max(specular, ggx_specular(normal, to_view, fill_to_light,
					pbr_roughness(max(scene.light_common.z, 1.0), material_roughness_scale()),
					pbr_f0(albedo_color.rgb, material_metallic()))
					* scene.fill_color.w * scene.fill_dir_intensity.w);
			else
			{
				vec3 fill_halfway = normalize(fill_to_light + to_view);
				float fill_spec_base = max(dot(normal, fill_halfway), 0.0);
				specular = max(specular, vec3(pow(fill_spec_base, max(scene.light_common.z, 1.0)) * scene.fill_color.w));
			}
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
			距离衰减（第三十六轮可切换模式；第四十六轮支持逐光源覆盖）
			逐光源模式存于 point_spot_cone.z：0 = 跟随全局（light_color.w）、1 = 线性、2 = 平方反比；
			线性模式在距离达到半径时降为 0，平方反比为 1/(1+(d/r)²) 且不设硬截断；
			半径为 0 时两种模式均视为无衰减。
			*/
			float radius = scene.point_pos_radius[index].w;
			float attenuation = 1.0;
			if (radius > 0.000001)
			{
				float normalized = distance / radius;
				//逐光源模式（0 = 跟随全局；全局 0 = 线性、1 = 平方反比，映射到 1 / 2）
				int mode = int(scene.point_spot_cone[index].z + 0.5);
				if (mode == 0)
					mode = scene.light_color.w > 0.5 ? 2 : 1;
				attenuation = mode == 2
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
			//逐盏光泽度（第五十一轮：0 = 跟随全局光泽度）
			float shininess = scene.point_shininess[index] > 0.0
				? scene.point_shininess[index]
				: max(scene.light_common.z, 1.0);
			//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
			if (scene.pbr_params.x > 0.5)
				specular = max(specular, ggx_specular(normal, to_view, to_light,
					pbr_roughness(shininess, material_roughness_scale()),
					pbr_f0(albedo_color.rgb, material_metallic())) * attenuation * cone * scene.point_spot_cone[index].w
					* scene.point_color_intensity[index].w);
			else
			{
				vec3 halfway = normalize(to_light + to_view);
				float spec_base = max(dot(normal, halfway), 0.0);
				specular = max(specular, vec3(pow(spec_base, shininess) * attenuation * cone
					* scene.point_spot_cone[index].w));
			}
		}
	}
	/*
	最终颜色：反照率 × 累加辐射度 × 漫反射权重 ＋ 镜面（强度由 light_common.y 统一缩放），钳制到 [0,1]
	漫反射权重（第六十五轮）：PBR 开启时按金属度做能量守恒（1 − metallic），关闭时恒 1；
	金属度来源由 material_metallic() 决定（第六十八轮：逐物体覆盖或全局）。
	*/
	float diffuse_weight = scene.pbr_params.x > 0.5
		? clamp(1.0 - material_metallic(), 0.0, 1.0) : 1.0;
	vec3 lit = albedo_color.rgb * radiance * diffuse_weight + specular * scene.light_common.y;
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
