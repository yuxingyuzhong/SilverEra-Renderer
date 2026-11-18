#version 450
/*
后处理片元着色器 · AgX 色调映射（AgX 近似，方向九第 760 轮）

口径
1. 输入为上一级输出（HDR 默认路径下是 16F 场景目标，可 > 1）；
2. 先经 AgX 输入矩阵去饱和到宽色域工作空间，再做「白点归一的高光压缩」，最后经输出矩阵回 Rec.709；
3. 与逐通道算子（ACES / 胶片 / 指数）的关键差别：**彩色高光会去饱和**，不会在压缩后仍保持过纯的通道；
4. 输出钳到 [0,1]，alpha 恒 1；
5. 本文件为工程近似实现（非官方 LUT 逐位一致），曲线取 x / (x + 0.9)（中灰 0.18 → 0.167，白 4.0 → 0.816）。
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

//AgX 输入矩阵（Rec.709 线性 → AgX 工作空间；列主序）
const mat3 AGX_INSET = mat3(
	0.842479062253094, 0.0423282422610123, 0.0423756549057051,
	0.0784336, 0.878468636469772, 0.0784336,
	0.0792237451477643, 0.0791661274605434, 0.879142973793104);

//AgX 输出矩阵（AgX 工作空间 → Rec.709；列主序）
const mat3 AGX_OUTSET = mat3(
	1.19687900512017, -0.0528968517574562, -0.0529716355144438,
	-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
	-0.0990297440797205, -0.0989611768448433, 1.15107367264116);

//AgX 光段压缩（逐通道；白点归一的高光收敛）
vec3 agx_compress(vec3 work)
{
	return work / (work + vec3(0.9));
}

//AgX 近似映射
vec3 agx(vec3 color)
{
	//去饱和到 AgX 工作空间（彩色高光的通道差在此被压缩）
	vec3 work = AGX_INSET * max(color, vec3(0.0));
	//高光压缩
	vec3 mapped = agx_compress(work);
	//回到 Rec.709 显示域
	return clamp(AGX_OUTSET * mapped, 0.0, 1.0);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色并映射
	vec4 source = texelFetch(source_image, coord, 0);
	out_color = vec4(agx(source.rgb), 1.0);
}
