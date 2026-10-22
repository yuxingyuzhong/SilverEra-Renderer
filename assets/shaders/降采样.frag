#version 450
/*
后处理片元着色器 · 降采样（SSAA 盒式平均）
把 N 倍尺寸的源按 N×N 个 texel 取平均写入宿像素，N = 源宽 / 宿宽（整数，1..4）。
用于超采样抗锯齿：场景先渲染到 N 倍目标，再经本 pass 回到输出分辨率；
关闭 SSAA 时本 pass 不参与录制，既有画面逐字节不变。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1 binding 0：N 倍场景目标）
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：后处理「源 / 宿尺寸」（offset 80；x/y = 源宽高，z/w = 宿宽高）
layout(push_constant) uniform Push_Size
{
	layout(offset = 80) vec4 size_xy;
} push_size;

void main()
{
	//源 / 宿尺寸四分量
	vec4 sizes = push_size.size_xy;
	//源纹理尺寸（越界钳制用）
	ivec2 source_size = textureSize(source_image, 0);
	//采样倍率（源宽 / 宿宽；按最近整数取，至少 1）
	float ratio = max(sizes.x / max(sizes.z, 1.0), 1.0);
	int factor = int(floor(ratio + 0.5));
	//宿像素对应源区域的左上角
	vec2 base = gl_FragCoord.xy * ratio;
	//盒式平均累加
	vec3 sum = vec3(0.0);
	int count = 0;
	for (int j = 0; j < 4; ++j)
	{
		if (j >= factor)
			break;
		for (int i = 0; i < 4; ++i)
		{
			if (i >= factor)
				break;
			//源像素坐标（钳到有效范围）
			ivec2 coord = clamp(ivec2(base) + ivec2(i, j), ivec2(0), source_size - ivec2(1));
			sum += texelFetch(source_image, coord, 0).rgb;
			++count;
		}
	}
	out_color = vec4(sum / float(max(count, 1)), 1.0);
}
