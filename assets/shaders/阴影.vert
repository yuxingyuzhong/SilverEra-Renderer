#version 450
//阴影顶点着色器（第五十二轮）：以光源空间视图投影矩阵变换，输出归一化深度供片元打包
//顶点属性与场景一致：位置（vec3）、纹理坐标（vec2，未用）、法线（vec3，未用）
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
//实例属性：模型矩阵（4 列 vec4，按实例步进）
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//输出光源空间深度（0..1；Vulkan 深度范围）
layout(location = 0) out float out_depth;

/*
推送常量：光源空间视图投影矩阵
与场景管线共享同一管线布局（推送范围 80 字节），本管线只用前 64 字节。
*/
layout(push_constant) uniform Shadow_Push
{
	mat4 light_view_proj;
} shadow_push;

void main()
{
	//实例模型矩阵（列主序：4 个列向量拼装）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	//光源空间裁剪坐标
	vec4 clip = shadow_push.light_view_proj * model * vec4(in_position, 1.0);
	gl_Position = clip;
	//归一化深度（w 为 0 时按远平面处理，避免除零）
	out_depth = clip.w > 0.000001 ? clip.z / clip.w : 1.0;
}
