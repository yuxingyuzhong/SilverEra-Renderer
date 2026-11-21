#version 450
//场景蒙皮顶点着色器（方向十 · 收敛轮 859）
//顶点属性：位置（vec3）＋ 纹理坐标（vec2）＋ 法线（vec3）＋ 关节（uvec4）＋ 权重（vec4），步长 48，与 Skinned_Vertex 一致
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
//关节下标（四个；指向调色板条目）
layout(location = 11) in uvec4 in_joint;
//关节权重（四个；u16 UNORM 归一化到 0..1）
layout(location = 12) in vec4 in_weight;
//实例属性与静态场景着色器一致（location 3..10）
layout(location = 3) in vec4 in_model_0;
layout(location = 4) in vec4 in_model_1;
layout(location = 5) in vec4 in_model_2;
layout(location = 6) in vec4 in_model_3;
layout(location = 7) in vec4 in_material;
layout(location = 8) in vec4 in_toon;
layout(location = 9) in vec4 in_toon_tint;
layout(location = 10) in vec4 in_pbr_features;

//传递给片元着色器的纹理坐标
layout(location = 0) out vec2 out_uv;
//传递给片元着色器的世界空间法线
layout(location = 1) out vec3 out_normal;
//传递给片元着色器的世界空间位置
layout(location = 2) out vec3 out_world;
//逐物体材质 / 卡通 / 暗部 / PBR 特性（整实例恒定，flat）
layout(location = 3) flat out vec4 out_material;
layout(location = 4) flat out vec4 out_toon;
layout(location = 5) flat out vec4 out_toon_tint;
layout(location = 6) flat out vec4 out_pbr_features;

//场景缓冲（描述符集 0）：与静态场景着色器同布局
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	vec4 light_common;
	vec4 light_dir;
	vec4 light_color;
	vec4 camera_pos;
	vec4 point_pos_radius[8];
	vec4 point_color_intensity[8];
} scene;

/*
蒙皮调色板（set 0 binding 5；顶点阶段只读）
每条为「世界 × 逆绑定」的 4×4 列主序矩阵；空动画时全部为单位矩阵，蒙皮结果与静态顶点一致。
*/
layout(set = 0, binding = 5) readonly buffer Skin_Palette
{
	mat4 palette[];
} skin;

void main()
{
	//实例模型矩阵（列主序：4 个列向量拼装）
	mat4 model = mat4(in_model_0, in_model_1, in_model_2, in_model_3);
	out_uv = in_uv;
	/*
	蒙皮：局部位置与法线先经调色板变换，再乘实例模型矩阵
	权重和为零（静态网格误走蒙皮管线）时按原样输出，保证与静态路径一致。
	*/
	float weight_sum = in_weight.x + in_weight.y + in_weight.z + in_weight.w;
	vec4 local_position = vec4(in_position, 1.0);
	vec3 local_normal = in_normal;
	if (weight_sum > 0.0)
	{
		//逐关节累加位置与法线
		vec4 skinned_position = vec4(0.0);
		vec3 skinned_normal = vec3(0.0);
		for (int index = 0; index < 4; ++index)
		{
			float weight = in_weight[index];
			if (weight <= 0.0)
				continue;
			mat4 joint_matrix = skin.palette[int(in_joint[index])];
			skinned_position += weight * (joint_matrix * local_position);
			skinned_normal += weight * (mat3(joint_matrix) * in_normal);
		}
		//按权重和归一（权重未必严格和为 1）
		local_position = skinned_position / weight_sum;
		local_normal = skinned_normal / weight_sum;
	}
	//世界空间位置与法线
	vec4 world = model * local_position;
	out_world = world.xyz;
	out_normal = mat3(model) * local_normal;
	//逐物体属性原样下传
	out_material = in_material;
	out_toon = in_toon;
	out_toon_tint = in_toon_tint;
	out_pbr_features = in_pbr_features;
	gl_Position = scene.view_proj * world;
}
