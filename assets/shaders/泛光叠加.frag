#version 450
/*
后处理片元着色器 · 泛光叠加（双源后处理 / 真泛光闭环）
把「上一级输出」（描述符集 1 binding 0：模糊后的亮部层）与「场景目标」
（描述符集 1 binding 1：本帧场景 pass 的输出）逐像素相加并钳到 [0,1]，得到「原画面 ＋ 泛光」的合成结果。

口径：本片元为**双源**，须放在后处理链**末级**（此前的各级只读 binding 0）；
binding 1 由门面恒绑定目标组首项（场景目标），alpha 恒置 1。
取像素用 texelFetch ＋ 帧缓冲坐标，与顶点朝向 / 纹理坐标无关；要求源与宿范围一致（1:1）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//上一级输出（描述符集 1 binding 0：模糊后的亮部层）
layout(set = 1, binding = 0) uniform sampler2D bloom_image;

//场景目标（描述符集 1 binding 1：本帧场景 pass 的输出）
layout(set = 1, binding = 1) uniform sampler2D scene_image;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(bloom_image, 0);
	//按帧缓冲坐标取源像素（1:1；钳到 [0, size-1]）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), source_size - ivec2(1));
	//亮部层与场景原色
	vec3 bloom = texelFetch(bloom_image, coord, 0).rgb;
	vec3 scene = texelFetch(scene_image, coord, 0).rgb;
	//相加并钳到色域
	out_color = vec4(clamp(scene + bloom, vec3(0.0), vec3(1.0)), 1.0);
}
