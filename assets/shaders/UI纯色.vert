#version 450
/*
UI 纯色顶点着色器
顶点属性：位置（vec3，**窗口像素坐标**，z = 0），步长 32，与渲染层 Vertex 布局一致；
推送常量 offset 0 起为「像素坐标 → NDC」矩阵（列主序 mat4）；
UI 绘制在无深度附件的 UI 阶段进行，故不做任何深度 / 场景 UBO 参与。
*/
layout(location = 0) in vec3 in_position;

//像素坐标 → NDC 的仿射矩阵（列主序）
layout(push_constant) uniform Push_UI
{
	mat4 transform;
} push_ui;

void main()
{
	//只使用位置与矩阵，纹理坐标对纯色四边形无意义
	gl_Position = push_ui.transform * vec4(in_position, 1.0);
}
