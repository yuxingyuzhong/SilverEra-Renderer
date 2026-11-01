#version 450
/*
粒子 GPU 顶点着色器（第三百四十一轮；深研方向二 阶段 D）
不读实例属性：以 gl_InstanceIndex 取「紧凑索引」→「粒子状态」，在着色器内构造面向相机的 billboard。
相机位置与尺寸经推送常量下发（偏移 0：vec4 = 相机 xyz ＋ 尺寸）。
输出与 粒子.frag 兼容（out_uv ＋ flat out_age）。
*/
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

//传递给片元着色器的四边形局部 UV（用于程序化圆盘遮罩）
layout(location = 0) out vec2 out_uv;
//归一化年龄（0 = 刚发射、1 = 寿命尽头；整实例恒定）
layout(location = 1) flat out float out_age;

//粒子状态（32 字节；与 Host 侧 Particle_State 逐字段一致）
struct Particle_State
{
	float position[3];
	float velocity[3];
	float age;
	float lifetime;
};

//set 0：binding 0 = 场景 UBO（仅视图投影）、1 = 粒子状态（只读）、2 = 紧凑索引（只读）
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
} scene;
layout(std430, set = 0, binding = 1) readonly buffer States { Particle_State states[]; };
layout(std430, set = 0, binding = 2) readonly buffer Indices { uint indices[]; };

//推送常量：xyz = 相机世界位置、w = 四边形世界边长
layout(push_constant) uniform Push_Particle
{
	vec4 camera_size;
} push;

void main()
{
	//紧凑索引 → 粒子状态
	uint particle_index = indices[gl_InstanceIndex];
	Particle_State state = states[particle_index];
	//归一化年龄与尺寸收缩（与 CPU 路径同口径：最多收缩 35%）
	float age_ratio = state.lifetime > 0.0 ? state.age / state.lifetime : 0.0;
	age_ratio = clamp(age_ratio, 0.0, 1.0);
	float half_size = push.camera_size.w * 0.5 * (1.0 - 0.35 * age_ratio);
	//视线方向（相机 → 粒子；退化时回落 -Z；用分支而非三元以避开除零求值）
	vec3 forward = vec3(state.position[0], state.position[1], state.position[2]) - push.camera_size.xyz;
	float forward_length = length(forward);
	if (forward_length > 1e-6)
		//归一化视线
		forward = forward / forward_length;
	else
		//退化：看向 -Z
		forward = vec3(0.0, 0.0, -1.0);
	//右向量 = forward × 世界上方向（0,1,0）；退化时回落 +X
	vec3 right = vec3(-forward.z, 0.0, forward.x);
	float right_length = length(right);
	if (right_length > 1e-6)
		//归一化右向量
		right = right / right_length;
	else
		//视线与世界上方向平行：回落 +X
		right = vec3(1.0, 0.0, 0.0);
	//上向量 = right × forward
	vec3 up = cross(right, forward);
	/*
	billboard：中心 ＋ 右 / 上 × 半尺寸（第三百四十五轮修正）
	与 CPU 路径逐式一致：四边形顶点为 ±0.5、缩放为 half_size，世界尺寸 = 2 × 0.5 × half_size = half_size；
	此前多乘了一个 2.0，导致 GPU 路径的粒子外观是 CPU 路径的 2 倍。
	*/
	vec3 world = vec3(state.position[0], state.position[1], state.position[2])
		+ right * (in_position.x * half_size) + up * (in_position.y * half_size);
	out_uv = in_uv;
	out_age = age_ratio;
	gl_Position = scene.view_proj * vec4(world, 1.0);
}
