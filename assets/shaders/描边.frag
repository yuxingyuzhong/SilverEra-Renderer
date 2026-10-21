#version 450
/*
后处理片元着色器 · 描边（链式后处理预设）
用 3×3 Sobel 算子在源纹理的亮度上求梯度幅值，按幅值把原色压暗，形成深色描边。

口径：
1. 邻域采样一律 texelFetch ＋ 偏移（不依赖采样器过滤状态），越界钳到边缘；
2. 梯度按前后两点亮度差取「最大分量」的近似（|gx| + |gy|），乘增益后钳到 [0,1]
   作为「压暗权重」：edge = 0 保留原色，edge = 1 变纯黑；
3. alpha 恒置 1（与其余后处理预设一致）。
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

//按偏移取亮度（越界钳到边缘）
float luma_at(ivec2 coord, ivec2 size, ivec2 offset)
{
	ivec2 target = clamp(coord + offset, ivec2(0), size - ivec2(1));
	vec3 color = texelFetch(source_image, target, 0).rgb;
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	//源纹理尺寸与中心坐标
	ivec2 source_size = textureSize(source_image, 0);
		//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时为点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//中心与 3×3 邻域亮度
	float l00 = luma_at(coord, source_size, ivec2(-1, -1));
	float l10 = luma_at(coord, source_size, ivec2(0, -1));
	float l20 = luma_at(coord, source_size, ivec2(1, -1));
	float l01 = luma_at(coord, source_size, ivec2(-1, 0));
	float l21 = luma_at(coord, source_size, ivec2(1, 0));
	float l02 = luma_at(coord, source_size, ivec2(-1, 1));
	float l12 = luma_at(coord, source_size, ivec2(0, 1));
	float l22 = luma_at(coord, source_size, ivec2(1, 1));
	//Sobel 梯度（x / y 方向）
	float gx = (l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02);
	float gy = (l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20);
	//梯度幅值近似
	float magnitude = abs(gx) + abs(gy);
	//压暗权重（增益 2.0，钳到 [0,1]）
	float weight = clamp(magnitude * 2.0, 0.0, 1.0);
	//原色
	vec4 source = texelFetch(source_image, coord, 0);
	//按权重压暗
	out_color = vec4(source.rgb * (1.0 - weight), 1.0);
}
