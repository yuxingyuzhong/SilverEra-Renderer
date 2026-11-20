#version 450
/*
逐材质色调编号 · 顶点着色器（第 808 轮 · 8 位路径编号预通道；第 819 轮补 UV）
复用与场景管线完全相同的顶点 / 实例布局：位置 vec3（binding 0）＋ 纹理坐标 vec2 ＋
模型矩阵四列 ＋ 逐物体 PBR 特性（binding 1，location 10），故无需任何管线变体；
只做一次视图投影变换，并把实例属性与 UV 交给片元（特性为 flat）。
场景缓冲只声明首成员视图投影矩阵，与场景 UBO（set 0 binding 0）布局兼容。
*/
layout(location = 0) in vec3 in_position;
//纹理坐标（第 819 轮：alpha 裁剪需要与主 pass 同样的反照率采样）
layout(location = 1) in vec2 in_uv;
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;
layout(location = 10) in vec4 in_pbr_features;

//场景缓冲（set 0 binding 0）：仅取视图投影矩阵
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
} scene_buffer;

//逐物体 PBR 特性（整实例恒定，flat）
layout(location = 0) flat out vec4 out_pbr_features;
//纹理坐标（插值）
layout(location = 1) out vec2 out_uv;

void main()
{
	//实例模型矩阵（列主序四列）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	//裁剪空间位置
	gl_Position = scene_buffer.view_proj * model * vec4(in_position, 1.0);
	//逐物体特性原样交给片元
	out_pbr_features = in_pbr_features;
	//UV 原样交给片元（材质 UV 变换在片元内完成）
	out_uv = in_uv;
}
