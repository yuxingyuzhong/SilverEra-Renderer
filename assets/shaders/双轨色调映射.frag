#version 450
/*
后处理片元着色器 · 双轨色调映射（方向九第 773 轮；hdr_tone_operator = 5）

用途
在同一帧内按屏幕横向区域应用两条色调曲线，用于「写实场景 ＋ 二次元角色」分屏验证：
左半（uv.x < split − band）走 AgX（写实侧：高光收敛、彩色高光去饱和）；
右半（uv.x > split + band）走逐通道 ACES（二次元侧：保持纯色）。
中间 band 宽度内线性过渡，避免硬边。

口径
1. 输入为上一级输出（HDR 默认路径下是 16F 场景目标，可 > 1），两条曲线都只钳下界；
2. split = 0.5、band = 0.05 为本轮固定值（后续如需可改为推送常量）；
3. 输出钳到 [0,1]，alpha 恒 1；
4. 仅作为 internal 算子供验证，默认不启用（`hdr_tone_operator` 默认仍为 0 = ACES）。
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

//AgX 近似映射（去饱和 ＋ 白点归一压缩）
vec3 agx(vec3 color)
{
	//去饱和到 AgX 工作空间
	vec3 work = AGX_INSET * max(color, vec3(0.0));
	//高光压缩
	vec3 mapped = work / (work + vec3(0.9));
	//回到 Rec.709 显示域
	return clamp(AGX_OUTSET * mapped, 0.0, 1.0);
}

//简化 ACES 曲线（单通道，只钳下界）
float aces(float x)
{
	float v = max(x, 0.0);
	return (v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14);
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色
	vec3 source = texelFetch(source_image, coord, 0).rgb;
	//本像素的屏幕横向归一坐标（0 = 左缘、1 = 右缘）
	float uv_x = gl_FragCoord.x / push_size.size_xy.z;
	//过渡权重：0 = 左半（AgX）、1 = 右半（逐通道 ACES）
	float weight = smoothstep(0.45, 0.55, uv_x);
	//两条曲线结果
	vec3 left = agx(source);
	vec3 right = vec3(aces(source.r), aces(source.g), aces(source.b));
	//按区域混合
	out_color = vec4(mix(left, right, weight), 1.0);
}
