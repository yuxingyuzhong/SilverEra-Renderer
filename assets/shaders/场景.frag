#version 450
//场景片元着色器
//来自顶点着色器的纹理坐标
layout(location = 0) in vec2 in_uv;
//来自顶点着色器的世界空间法线（零向量 = 未提供）
layout(location = 1) in vec3 in_normal;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//反照率贴图（描述符集 1）：线性过滤 ＋ 重复寻址
layout(set = 1, binding = 0) uniform sampler2D albedo;

//场景缓冲（描述符集 0）：视图投影矩阵 ＋ 光照参数，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	//光照：xyz = 光传播方向（单位向量），w = 环境光
	vec4 light_dir;
	//光照：rgb = 光颜色，w = 强度
	vec4 light_color;
} scene;

void main()
{
	vec4 albedo_color = texture(albedo, in_uv);
	/*
	方向光（Lambert）＋ 环境光（第二十九轮新增）
	未提供法线（长度近零）时按「朝向相机」处理，等价于满照（light = 1），
	保证既有只写位置 / 纹理坐标的网格画面不变。
	*/
	float light = 1.0;
	//若提供了法线
	if (length(in_normal) > 0.000001)
	{
		vec3 normal = normalize(in_normal);
		//光传播方向取反即「指向光源」的方向
		float ndotl = max(dot(normal, -scene.light_dir.xyz), 0.0);
		light = clamp(scene.light_dir.w + scene.light_color.w * ndotl, 0.0, 1.0);
	}
	//反照率按光照系数缩放（仅 RGB；alpha 保持贴图原值，混合语义不变）
	out_color = vec4(albedo_color.rgb * scene.light_color.rgb * light, albedo_color.a);
}
