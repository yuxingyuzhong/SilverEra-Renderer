#version 450
//场景顶点着色器
//顶点属性：位置（vec3）＋ 纹理坐标（vec2），步长 20，与渲染层 Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
/*
实例属性：模型矩阵（4 列 vec4，按实例步进，步长 64）
模型矩阵统一由「实例缓冲」承载（不再经推送常量）；实例数为 1 时即逐次绘制的等价路径，
故本渲染层只有一条绘制路径，不存在「非实例 / 实例」两套语义。
*/
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//传递给片元着色器的纹理坐标
layout(location = 0) out vec2 out_uv;

//场景缓冲（描述符集 0）：视图投影矩阵，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
} scene;

void main()
{
	//实例模型矩阵（列主序：4 个列向量拼装）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	out_uv = in_uv;
	gl_Position = scene.view_proj * model * vec4(in_position, 1.0);
}
