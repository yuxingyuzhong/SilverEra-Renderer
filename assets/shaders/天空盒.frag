#version 450
/*
天空盒片元着色器（第一百二十五轮）
以「逆视图投影矩阵」把片元 NDC 反推为世界空间视线方向（取 w = 0 的方向向量），
再用该方向采样立方体贴图，作为场景背景。
推送常量：offset 0 为逆视图投影矩阵（16 浮点）；门面每帧写入。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//来自顶点着色器的 NDC 坐标
layout(location = 0) in vec2 in_ndc;

//天空盒立方体贴图（描述符集 1 binding 5）
layout(set = 1, binding = 5) uniform samplerCube sky_map;

//推送常量：逆视图投影矩阵（offset 0）
layout(push_constant) uniform Push_Sky
{
	layout(offset = 0) mat4 inverse_view_proj;
} push;

void main()
{
	//方向向量（w = 0 → 只取矩阵的旋转部分，得到该像素对应的世界方向）
	vec4 direction = push.inverse_view_proj * vec4(in_ndc, 1.0, 0.0);
	//方向退化时回落到 +Z（避免归一化零向量）
	vec3 dir = length(direction.xyz) > 0.000001 ? normalize(direction.xyz) : vec3(0.0, 0.0, 1.0);
	//采样立方体贴图（不透明背景）
	out_color = vec4(texture(sky_map, dir).rgb, 1.0);
}
