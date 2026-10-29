#version 450
//反向外扩描边 · 顶点着色器（第二百二十八轮）
//沿世界空间法线把顶点外扩 width，配合「剔除正面」只保留背面，从而形成物体外轮廓。
//顶点属性：位置（vec3）＋ 法线（vec3），步长 32；实例属性：模型矩阵（4 列 vec4，步长 80）。
layout(location = 0) in vec3 in_position;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;

//场景缓冲（描述符集 0，binding 0）：本着色器只消费视图投影矩阵与相机位置，其余成员由场景着色器使用
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	layout(offset = 112) vec4 camera_pos;
} scene;

/*
描边参数（推送常量 offset 112 的 vec4；与后处理描边色共用同一槽位）
x = 外扩宽度（世界单位；0 = 不外扩），yzw = 描边色（片元着色器使用）。
*/
layout(push_constant) uniform Outline_Push
{
	layout(offset = 112) vec4 outline_params;
} outline;

void main()
{
	//实例模型矩阵（列主序：4 个列向量拼装）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	//世界空间位置
	vec4 world = model * vec4(in_position, 1.0);
	//世界空间法线（零向量表示未提供，此时无法外扩，按原位置输出）
	vec3 normal_world = mat3(model) * in_normal;
	float normal_length = length(normal_world);
	if (normal_length > 1e-6)
	{
		/*
		宽度按视距补偿（第二百三十轮）
		参考距离 3 世界单位处的宽度即 outline_params.x；更远处按距离等比放大，
		使轮廓在屏幕上的像素宽度基本恒定（近处不会粗得糊住细节、远处不会细到看不见）。
		*/
		float camera_distance = length(scene.camera_pos.xyz - world.xyz);
		float distance_scale = camera_distance / 3.0;
		world.xyz += (normal_world / normal_length) * outline.outline_params.x * distance_scale;
	}
	gl_Position = scene.view_proj * world;
}
