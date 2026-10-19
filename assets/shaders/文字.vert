#version 450
/*
文字顶点着色器
顶点属性：位置（vec3，**像素坐标**）＋ 纹理坐标（vec2，图集 uv），步长 20，与渲染层 Vertex 一致；
实例属性：模型矩阵（4 列 vec4，按实例步进，步长 64）—— 对文字而言它是「像素坐标 → NDC」的仿射矩阵
（含文本位置偏移与 Y 轴翻转）。
与场景管线的区别：文字**不经**场景 UBO（set0 不参与），故本着色器不引用 set0；
其余（顶点输入布局、管线布局、描述符集布局）与场景管线完全一致。
*/
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//传递给片元着色器的图集坐标
layout(location = 0) out vec2 out_uv;

void main()
{
	//像素坐标 → NDC 的仿射矩阵（列主序：4 个列向量拼装）
	mat4 transform = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	out_uv = in_uv;
	//直接输出（不再乘场景视图投影矩阵）
	gl_Position = transform * vec4(in_position, 1.0);
}
