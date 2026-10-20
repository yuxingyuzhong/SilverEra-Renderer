#version 450
/*
后处理片元着色器 · 灰度（链式后处理预设）
把源纹理逐像素 1:1 取回后按 Rec.709 亮度加权去色（保持 alpha 为 1）。

用途：作为后处理链的一级（可单独使用，也可与描边 / 色调映射串联）。
取像素用 texelFetch ＋ 帧缓冲坐标，与顶点朝向 / 纹理坐标无关；要求源与宿范围一致（1:1）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//按帧缓冲坐标取源像素（1:1；钳到 [0, size-1]）
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), source_size - ivec2(1));
	//取源颜色
	vec4 source = texelFetch(source_image, coord, 0);
	//Rec.709 亮度
	float luma = dot(source.rgb, vec3(0.2126, 0.7152, 0.0722));
	//灰度输出
	out_color = vec4(vec3(luma), 1.0);
}
