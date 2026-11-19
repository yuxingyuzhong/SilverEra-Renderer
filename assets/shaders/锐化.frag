#version 450
/*
后处理片元着色器 · 锐化（第七十四轮）
非锐化掩模（unsharp mask）：中心像素 ＋ 0.8 ×（中心 − 4 邻域均值），结果钳到 [0,1]。
取像素用 texelFetch ＋ 整数偏移（越界钳边），与顶点朝向 / 纹理坐标无关；alpha 按中心像素透传（保留逐材质色调编号）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//最大合法坐标
	ivec2 maximum = source_size - ivec2(1);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	//中心像素坐标（钳到合法范围）
	ivec2 center = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), maximum);
	//中心纹素（保留 alpha 以便透传逐材质色调编号）
	vec4 middle_texel = texelFetch(source_image, center, 0);
	//中心像素
	vec3 middle = middle_texel.rgb;
	//四邻域（越界钳边）
	vec3 neighbors = texelFetch(source_image, clamp(center + ivec2(1, 0), ivec2(0), maximum), 0).rgb
		+ texelFetch(source_image, clamp(center + ivec2(-1, 0), ivec2(0), maximum), 0).rgb
		+ texelFetch(source_image, clamp(center + ivec2(0, 1), ivec2(0), maximum), 0).rgb
		+ texelFetch(source_image, clamp(center + ivec2(0, -1), ivec2(0), maximum), 0).rgb;
	//邻域均值
	vec3 average = neighbors * 0.25;
	//非锐化掩模（强度 0.8）
	vec3 sharpened = middle + (middle - average) * 0.8;
	out_color = vec4(clamp(sharpened, 0.0, 1.0), middle_texel.a);
}
