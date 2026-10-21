#version 450
/*
后处理片元着色器 · 高斯模糊（泛光链的二级）
对源纹理做 3×3 高斯近似卷积（权重 1/16、2/16、1/16；行列可分离核的 3×3 组合），
用于把上一级「亮部提取」得到的亮部向外扩散，形成泛光雏形。

取像素用 texelFetch ＋ 整数偏移（与顶点朝向 / 纹理坐标无关），越界钳到边缘；
要求源与宿范围一致（1:1）。alpha 恒置 1。
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
	//中心像素坐标
		//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时为点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 center = ivec2(gl_FragCoord.xy * source_scale);
	//累加颜色
	vec3 sum = vec3(0.0);
	//3×3 高斯核（可分离核 [1 2 1] ⊗ [1 2 1] / 16）
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			//邻域权重（边缘 1、边上 2、中心 4，除以 16）
			float weight = (2.0 - abs(float(dx))) * (2.0 - abs(float(dy))) / 16.0;
			//邻域坐标（钳到 [0, size-1]）
			ivec2 coord = clamp(center + ivec2(dx, dy), ivec2(0), source_size - ivec2(1));
			//加权累加
			sum += texelFetch(source_image, coord, 0).rgb * weight;
		}
	}
	out_color = vec4(sum, 1.0);
}
