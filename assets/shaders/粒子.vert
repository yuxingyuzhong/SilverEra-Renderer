#version 450
/*
粒子顶点着色器（第三百一十四轮；深研方向二 阶段 B）
顶点为 XY 平面单位四边形（±0.5）；实例缓冲承载「面向相机的 billboard 基向量 × 半尺寸 ＋ 中心」模型矩阵，
故顶点阶段只需常规世界变换（billboard 基在 CPU 侧按相机方向构造）。
*/
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
//实例模型矩阵（列主序 4 列；步长 80 的实例缓冲，材质槽不使用）
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//传递给片元着色器的四边形局部 UV（用于程序化圆盘遮罩）
layout(location = 0) out vec2 out_uv;
//归一化年龄（0 = 刚发射、1 = 寿命尽头；整实例恒定，用 flat 避免边缘插值）
layout(location = 1) flat out float out_age;

//场景缓冲（描述符集 0，binding 0）：仅使用视图投影矩阵
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
} scene;

void main()
{
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	out_uv = in_uv;
	//实例矩阵第 3 列（index 2）的 w 分量承载归一化年龄（第三百一十五轮）
	out_age = in_model_2.w;
	gl_Position = scene.view_proj * model * vec4(in_position, 1.0);
}
