#version 450
/*
后处理片元着色器 · 泛光叠加（双源后处理 / 真泛光闭环）
把「上一级输出」（描述符集 1 binding 0：模糊后的亮部层）与「场景目标」
（描述符集 1 binding 1：本帧场景 pass 的输出）逐像素相加并钳到 [0,1]，得到「原画面 ＋ 泛光」的合成结果。

口径：本片元为**双源**，须放在后处理链**末级**（此前的各级只读 binding 0）；
binding 1 由门面恒绑定目标组首项（场景目标），alpha 恒置 1。
取像素用 texelFetch ＋ 帧缓冲坐标，经推送的「源 / 宿尺寸」映射为源像素（1:1 时为恒等）；
两个源的尺寸可各自不同（如半分辨率泛光层 ＋ 全尺寸场景），故各按自身 `textureSize` 计算映射。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//上一级输出（描述符集 1 binding 0：模糊后的亮部层）
layout(set = 1, binding = 0) uniform sampler2D bloom_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

//场景目标（描述符集 1 binding 1：本帧场景 pass 的输出）
layout(set = 1, binding = 1) uniform sampler2D scene_image;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(bloom_image, 0);
	//场景纹理尺寸（两源可不同尺寸）
	ivec2 scene_size = textureSize(scene_image, 0);
	//宿像素 → 亮部层像素（推送比例）
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时为点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//宿像素 → 场景像素（按场景纹理自身尺寸计算）
	vec2 scene_scale = vec2(scene_size) / push_size.size_xy.zw;
	ivec2 scene_coord = clamp(ivec2(gl_FragCoord.xy * scene_scale), ivec2(0), scene_size - ivec2(1));
	//亮部层与场景原色
	vec3 bloom = texelFetch(bloom_image, coord, 0).rgb;
	vec3 scene = texelFetch(scene_image, scene_coord, 0).rgb;
	//相加并钳到色域
	out_color = vec4(clamp(scene + bloom, vec3(0.0), vec3(1.0)), 1.0);
}
