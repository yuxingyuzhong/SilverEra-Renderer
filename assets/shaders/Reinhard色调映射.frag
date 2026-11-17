#version 450
/*
后处理片元着色器 · Reinhard 色调映射（链式后处理预设；第一百八十五轮）
经典 Reinhard 曲线逐通道压缩高光：x / (1 + x)。

口径：
1. 输入只钳下界（第七百四十八轮）：8 位源本就 ≤ 1、结果逐字节不变；16F 源可保留 >1 高光；
2. 输出仍落在 [0,1]：x=1 → 0.5、x=0.18 → ≈0.153（暗区压暗强于 ACES）；
3. alpha 恒置 1；只做逐像素映射，不读任何参数（与既有 ACES 预设互补）。
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

//Reinhard 曲线（单通道）
float reinhard(float x)
{
	//只钳下界后按 x / (1 + x) 映射
	float v = max(x, 0.0);
	return v / (1.0 + v);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时为点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色并逐通道映射
	vec4 source = texelFetch(source_image, coord, 0);
	out_color = vec4(vec3(reinhard(source.r), reinhard(source.g), reinhard(source.b)), 1.0);
}
