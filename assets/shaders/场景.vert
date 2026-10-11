#version 450
//场景顶点着色器
//顶点属性：位置（vec3）＋ 纹理坐标（vec2），步长 20，与渲染层 Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

//传递给片元着色器的纹理坐标
layout(location = 0) out vec2 out_uv;

//场景缓冲（描述符集 0）：视图投影矩阵，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
} scene;

//对象推送常量：模型矩阵，逐次绘制更新
layout(push_constant) uniform Push_Object
{
	mat4 model;
} object;

void main()
{
	out_uv = in_uv;
	gl_Position = scene.view_proj * object.model * vec4(in_position, 1.0);
}
