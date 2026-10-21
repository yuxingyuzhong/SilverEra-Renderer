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

//场景缓冲（描述符集 0）：视图投影矩阵 ＋ 光照参数 ＋ 相机位置，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	//xyz = 光传播方向（方向光）或光位置（点光源），w = 环境光
	vec4 light_param0;
	//rgb = 光颜色，w = 强度
	vec4 light_param1;
	//x = 光源模式（0 方向光 / 1 点光源）、y = 衰减半径、z = 高光强度、w = 光泽度
	vec4 light_param2;
	//xyz = 相机世界位置，w 保留
	vec4 camera_pos;
} scene;

void main()
{
	vec4 albedo_color = texture(albedo, in_uv);
	/*
	光照（第二十九轮方向光 / 第三十一轮点光源 ＋ 镜面高光）
	未提供法线（长度近零）时按「朝向相机」处理，等价于满照（light = 1），
	保证既有只写位置 / 纹理坐标的网格画面不变。
	*/
	float light = 1.0;
	//高光分量（默认 0）
	float specular = 0.0;
	//若提供了法线
	if (length(in_normal) > 0.000001)
	{
		vec3 normal = normalize(in_normal);
		//指向光源的方向（默认按方向光：传播方向取反）
		vec3 to_light = -scene.light_param0.xyz;
		//衰减（方向光恒 1）
		float attenuation = 1.0;
		//若为点光源（模式 1）
		if (scene.light_param2.x > 0.5)
		{
			vec3 offset = scene.light_param0.xyz - in_world;
			float distance = length(offset);
			//避免零距离除零
			to_light = distance > 0.000001 ? offset / distance : vec3(0.0, 0.0, 1.0);
			//线性衰减：距离达到半径时降为 0（半径为 0 时视为无衰减）
			attenuation = scene.light_param2.y > 0.000001
				? clamp(1.0 - distance / scene.light_param2.y, 0.0, 1.0) : 1.0;
		}
		float ndotl = max(dot(normal, to_light), 0.0);
		light = clamp(scene.light_param0.w + scene.light_param1.w * ndotl * attenuation, 0.0, 1.0);
		//Blinn-Phong 高光：视线方向取「相机 → 片元」的反向
		vec3 to_view = normalize(scene.camera_pos.xyz - in_world);
		vec3 halfway = normalize(to_light + to_view);
		float spec_base = max(dot(normal, halfway), 0.0);
		specular = scene.light_param2.z * pow(spec_base, max(scene.light_param2.w, 1.0)) * attenuation;
	}
	//最终颜色：反照率 × 光色 × 漫反射照度 ＋ 高光（钳制到 [0,1]）
	vec3 lit = albedo_color.rgb * scene.light_param1.rgb * light + vec3(specular);
	out_color = vec4(clamp(lit, 0.0, 1.0), albedo_color.a);
}
