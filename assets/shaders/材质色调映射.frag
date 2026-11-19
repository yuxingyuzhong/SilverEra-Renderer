#version 450
/*
后处理片元着色器 · 逐材质色调映射（第 779 轮 · 通用引擎化整改）

用途
HDR 默认路径的末级算子：同一帧内按像素选择色调曲线，故写实物体与二次元物体可各自选算子，
不再被「全帧唯一算子」强制妥协（762 实测：AgX 会把二次元纯色压灰）。

口径
1. 场景 pass 把逐材质算子编号写进 16F 场景目标的 alpha：alpha = (id + 1) × 0.0005，id ∈ [0, 4]；
   未请求覆盖的像素 alpha ≥ 0.5（通常为 1），此时用推送常量 w 的全局默认算子；
2. 编号经最近邻取整（floor(x * 2000 + 0.5) − 1），不做插值，避免两种曲线在中途混合；
   MSAA 解析会平均 alpha，边缘像素可能回落默认算子（可接受；
3. 各曲线与既有单算子片元逐式一致（ACES / 胶片 / 指数 / 扩展 Reinhard / AgX）；算子 5（双轨）
   仍按屏幕横向区域平滑过渡（x = 分轨位置、y = 过渡半宽）；
4. 输出 alpha 恒 1；只读推送常量 offset 80..111（不依赖 144 字节推送能力）。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
逐材质色调编号附件（第 812 轮 · 8 位路径段 4）
与场景目标同尺寸的 R8_UINT 附件：0 = 无覆盖、1..5 = 算子 + 1；
未绑定真实编号附件时由引擎写入 1×1 零图（恒 0），解码自动回落到 alpha / 全局。
*/
layout(set = 1, binding = 10) uniform usampler2D tone_id_image;

//推送常量：源 / 宿尺寸（offset 80）与色调参数（offset 96）
layout(push_constant) uniform Push_Size
{
	//x/y = 源宽高、z/w = 宿宽高
	layout(offset = 80) vec4 size_xy;
	//x = 分轨位置、y = 过渡半宽（仅回落算子 5 使用）、z 保留、w = 回落算子编号（0..5）
	layout(offset = 96) vec4 tone_param;
} push_size;

//Rec.709 亮度权重（扩展 Reinhard 用）
const vec3 LUMA_WEIGHT = vec3(0.2126, 0.7152, 0.0722);

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

//胶片曲线默认白点与曝光偏置
const float DEFAULT_WHITE_POINT = 11.2;
const float DEFAULT_EXPOSURE_BIAS = 2.0;

//简化 ACES 曲线（单通道，只钳下界）
float aces(float x)
{
	float v = max(x, 0.0);
	return (v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14);
}

//Hable filmic 基础曲线（单通道）
float hable(float x)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

//胶片曲线（白点归一化，默认参数口径）
vec3 filmic(vec3 color)
{
	vec3 biased = max(color, vec3(0.0)) * DEFAULT_EXPOSURE_BIAS;
	vec3 current = vec3(hable(biased.r), hable(biased.g), hable(biased.b));
	float white_scale = hable(DEFAULT_WHITE_POINT);
	return clamp(current / vec3(max(white_scale, 1e-5)), 0.0, 1.0);
}

//指数曲线（单通道）
float exponential(float x)
{
	return 1.0 - exp(-max(x, 0.0));
}

//扩展 Reinhard（按亮度缩放，保持色相）
vec3 reinhard_extended(vec3 color)
{
	const float WHITE_POINT = 2.0;
	float luminance = dot(color, LUMA_WEIGHT);
	float mapped = (luminance * (1.0 + luminance / (WHITE_POINT * WHITE_POINT))) / (1.0 + luminance);
	float scale = luminance > 1e-5 ? mapped / luminance : 1.0;
	return clamp(color * scale, 0.0, 1.0);
}

//AgX 近似映射（去饱和 ＋ 白点归一压缩）
vec3 agx(vec3 color)
{
	vec3 work = AGX_INSET * max(color, vec3(0.0));
	vec3 mapped = work / (work + vec3(0.9));
	return clamp(AGX_OUTSET * mapped, 0.0, 1.0);
}

//按编号取曲线（0 ACES / 1 胶片 / 2 指数 / 3 扩展 Reinhard / 4 AgX）
vec3 tone_by(int operator_id, vec3 color)
{
	if (operator_id == 1)
		return filmic(color);
	if (operator_id == 2)
		return vec3(exponential(color.r), exponential(color.g), exponential(color.b));
	if (operator_id == 3)
		return reinhard_extended(color);
	if (operator_id == 4)
		return agx(color);
	return vec3(aces(color.r), aces(color.g), aces(color.b));
}

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1；非 1:1 时点采样）
	vec2 source_scale = push_size.size_xy.xy / push_size.size_xy.zw;
	ivec2 coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), source_size - ivec2(1));
	//取源颜色
	vec4 source = texelFetch(source_image, coord, 0);
	//全局默认算子（推送常量 w；非法回落 0）
	int operator_id = int(clamp(push_size.tone_param.w + 0.5, 0.0, 5.0));
	/*
	编号附件优先（第 812 轮 · 8 位路径段 4）
	通道开启且目标带编号附件时，每个可见片元携带 1..5（算子 + 1）；0 表示未覆盖。
	编号按同一「宿 → 源」比例映射到编号附件坐标（编号附件与场景目标同尺寸）。
	*/
	ivec2 tone_size = textureSize(tone_id_image, 0);
	ivec2 tone_coord = clamp(ivec2(gl_FragCoord.xy * source_scale), ivec2(0), tone_size - ivec2(1));
	uint tone_id = texelFetch(tone_id_image, tone_coord, 0).r;
	if (tone_id >= 1u && tone_id <= 5u)
		operator_id = clamp(int(tone_id) - 1, 0, 4);
	/*
	逐材质覆盖（第 779 轮；第 784 轮收窄编码带宽）
	场景 pass 把 (id + 1) × 0.0005 写进 alpha；编码落在 0.0005..0.0025，故按 < 0.003 判定，
	与粒子等透明绘制的 alpha 碰撞概率被压到「几乎不可见」的量级；
	编号取最近邻，避免两种曲线在过渡处混合出第三种结果。
	*/
	if (source.a > 0.0 && source.a < 0.003)
		operator_id = clamp(int(floor(source.a * 2000.0 + 0.5)) - 1, 0, 4);
	//算子 5：双轨（仅作为全局回落；逐材质覆盖优先）
	if (operator_id == 5)
	{
		float uv_x = gl_FragCoord.x / push_size.size_xy.z;
		float split = (push_size.tone_param.x > 0.0 && push_size.tone_param.x < 1.0)
			? push_size.tone_param.x : 0.5;
		float band = (push_size.tone_param.y > 0.0 && push_size.tone_param.y < 0.5)
			? push_size.tone_param.y : 0.1;
		float weight = smoothstep(split - band, split + band, uv_x);
		vec3 left = agx(source.rgb);
		vec3 right = vec3(aces(source.r), aces(source.g), aces(source.b));
		out_color = vec4(mix(left, right, weight), 1.0);
		return;
	}
	out_color = vec4(tone_by(operator_id, source.rgb), 1.0);
}
