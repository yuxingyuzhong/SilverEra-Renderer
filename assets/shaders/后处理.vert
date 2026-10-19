#version 450
/*
后处理顶点着色器（同帧多 pass 的第二个 pass）
输入为「全屏四边形」的顶点（位置为 NDC 坐标，范围 -1..1），经实例模型矩阵变换后直接作为裁剪空间坐标输出：
不施加场景视图投影矩阵（故本 pass 与相机无关）；实例模型矩阵在本 pass 恒为单位矩阵，
故结果即 NDC 位置本身，全屏覆盖。
本 pass 与场景 pass 共享同一管线布局与顶点输入声明（含实例矩阵属性），
故此处照常声明并应用实例矩阵，既与渲染层「模型矩阵统一由实例缓冲承载」的唯一绘制路径一致，
也避免「管线声明了顶点属性而着色器未使用」的告警。
*/
//顶点属性：位置（vec3）＋ 纹理坐标（vec2），步长 20，与渲染层 Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
/*
实例属性：模型矩阵（4 列 vec4，按实例步进，步长 64）
本 pass 恒传入单位矩阵，故对 NDC 坐标的变换为恒等变换。
*/
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

void main()
{
	//实例模型矩阵（列主序：4 个列向量拼装；本 pass 恒为单位矩阵）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	//应用模型矩阵后直接作为裁剪空间坐标输出（单位矩阵时即 in_position 本身）
	gl_Position = model * vec4(in_position, 1.0);
}
