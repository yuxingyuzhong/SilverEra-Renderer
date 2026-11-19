#version 450
/*
后处理片元着色器 · 景深（第一百二十三轮）
单级后处理：以场景深度图（描述符集 1 binding 2）判定每像素的弥散圆（CoC），
再对源画面做固定 12 抽样的圆形散景模糊。参数经推送常量 post_param 下发：
	x = 对焦距离（世界单位，以相机为原点）
	y = 光圈（0 = 全清晰、1 = 最强）
	z = 最大模糊半径（像素；< 0.5 视为无模糊）
	w = 保留
深度换算：透视投影下 depth ∈ [0,1]，距离 = near × far / (far − depth × (far − near))；
	双级级联阴影与景深互不影响（后者只读深度图）。
注意：MSAA 开启时主 pass 的深度写入多重采样附件，由场景 pass 结束时按样本 0 解析到单采样深度图
	（第一百九十轮；Renderer_Config::msaa_depth_resolve 默认开启），故本阶段可直接使用。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

/*
场景缓冲（描述符集 0）
仅取相机近远平面（offset 1056 字节起），但 GLSL 无逐成员偏移，故按与 场景.frag 完全一致的顺序声明。
*/
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	vec4 light_common;
	vec4 light_dir;
	vec4 light_color;
	vec4 camera_pos;
	vec4 point_pos_radius[8];
	vec4 point_color_intensity[8];
	vec4 point_spot_dir[8];
	vec4 point_spot_cone[8];
	vec4 fog_color_density;
	vec4 fog_range;
	vec4 ambient_color;
	vec4 ambient_ground;
	vec4 fill_dir_intensity;
	vec4 fill_color;
	float point_shininess[8];
	mat4 light_view_proj;
	vec4 shadow_params;
	float shadow_strength;
	vec4 pbr_params;
	mat4 light_view_proj_far;
	vec4 cascade_params;
	vec4 camera_range;
} scene;

//源画面（描述符集 1 binding 0）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;
//场景深度（描述符集 1 binding 2）：主 pass 写入的单采样深度图
layout(set = 1, binding = 2) uniform sampler2D depth_image;

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

//圆形散景抽样数（固定 12，保证各硬件行为一致）
const int TAPS = 12;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 source_texel = texelFetch(source_image, coord, 0);
	vec3 color = source_texel.rgb;
	//取该像素的深度并换算成世界距离
	float depth = texelFetch(depth_image, coord, 0).r;
	float near_plane = max(scene.camera_range.x, 0.0001);
	float far_plane = max(scene.camera_range.y, near_plane + 0.0001);
	float distance_to_camera = (near_plane * far_plane)
		/ max(far_plane - depth * (far_plane - near_plane), 0.0001);
	//对焦距离与模糊半径
	float focus = max(push.post_param.x, 0.0001);
	float aperture = clamp(push.post_param.y, 0.0, 1.0);
	float max_radius = max(push.post_param.z, 0.0);
	//弥散圆（0 = 在对焦面、1 = 最大离焦）
	float coc = clamp(abs(distance_to_camera - focus) / focus * aperture, 0.0, 1.0);
	//本像素模糊半径（像素）
	float radius = coc * max_radius;

	//半径过小视为清晰，直接写回（与直通逐字节一致）
	if (radius < 0.5)
	{
		out_color = vec4(color, source_texel.a);
		return;
	}

	//圆形散景累加（中心 ＋ 12 抽样，等权）
	vec3 accum = color;
	for (int index = 0; index < TAPS; ++index)
	{
		//抽样角度（黄金角分布，避免规则环的环状伪影）
		float angle = 2.39996323 * float(index);
		vec2 direction = vec2(cos(angle), sin(angle));
		//抽样半径在 [0.5, 1] × 本像素半径内均匀分布
		float sample_radius = radius * (0.5 + 0.5 * float(index + 1) / float(TAPS));
		ivec2 sample_coord = clamp(coord + ivec2(direction * sample_radius), ivec2(0), source_size - ivec2(1));
		accum += texelFetch(source_image, sample_coord, 0).rgb;
	}
	//均值写回（alpha 不参与平均：逐材质色调编号按中心像素透传）
	out_color = vec4(accum / float(TAPS + 1), source_texel.a);
}
