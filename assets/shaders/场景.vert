#version 450
//场景顶点着色器
//顶点属性：位置（vec3）＋ 纹理坐标（vec2）＋ 法线（vec3），步长 32，与渲染层 Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
//法线（第二十九轮新增；零向量表示未提供，由片元按朝向相机处理）
layout(location = 2) in vec3 in_normal;
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
//传递给片元着色器的世界空间法线（未提供时为零向量）
layout(location = 1) out vec3 out_normal;

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
	//实例模型矩阵（列主序：4 个列向量拼装）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	out_uv = in_uv;
	//法线经模型矩阵线性部分变换（不做逆转置；非等比缩放下为近似）
	out_normal = mat3(model) * in_normal;
	gl_Position = scene.view_proj * model * vec4(in_position, 1.0);
}
