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
	//rgb = 方向光颜色，w 保留
	vec4 light_color;
	//xyz = 相机世界位置，w 保留
	vec4 camera_pos;
	//点光源位置（xyz）＋ 衰减半径（w），最多 4 个
	vec4 point_pos_radius[4];
	//点光源颜色（rgb）＋ 强度（w），最多 4 个
	vec4 point_color_intensity[4];
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
		//环境光作为基底辐射度
		radiance = vec3(scene.light_common.x);
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
			//线性衰减：距离达到半径时降为 0（半径为 0 时视为无衰减）
			float radius = scene.point_pos_radius[index].w;
			float attenuation = radius > 0.000001 ? clamp(1.0 - distance / radius, 0.0, 1.0) : 1.0;
			float ndotl = max(dot(normal, to_light), 0.0);
			radiance += scene.point_color_intensity[index].rgb * scene.point_color_intensity[index].w
				* ndotl * attenuation;
			//高光同样取最大值；衰减为 0 的光源不产生高光
			vec3 halfway = normalize(to_light + to_view);
			float spec_base = max(dot(normal, halfway), 0.0);
			specular = max(specular, pow(spec_base, max(scene.light_common.z, 1.0)) * attenuation);
		}
	}
	//最终颜色：反照率 × 累加辐射度 ＋ 高光（强度由 light_common.y 统一缩放），钳制到 [0,1]
	vec3 lit = albedo_color.rgb * radiance + vec3(specular * scene.light_common.y);
	out_color = vec4(clamp(lit, 0.0, 1.0), albedo_color.a);
}
