#version 450
/*
后处理片元着色器 · 高斯模糊（泛光链的二级；第八十一轮扩展半径）
对源纹理做 (2N+1)×(2N+1) 三角权重近似高斯卷积：weight = (N+1−|dx|)(N+1−|dy|) / (N+1)⁴。
半径为 N 时 N = 1 与既有 3×3 核（1/16、2/16、1/16）逐权重等价，故默认画面不变。
半径取后处理推送常量的 z 分量（`post_stage_param_set` 的 intensity），四舍五入后钳到 [1, 4]；
未专设时沿用全局强度默认 1.0 → 半径 1（现行 3×3 行为）。

取像素用 texelFetch ＋ 整数偏移（与顶点朝向 / 纹理坐标无关），越界钳到边缘；alpha 恒置 1。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

/*
推送常量（后处理块，offset 80 起）
size_xy：x/y = 源宽高、z/w = 宿宽高；post_param：x = 阈值（本片元不用）、y = 软膝（不用）、
z = 模糊半径、w 保留。
*/
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	vec4 post_param;
} push_post;

void main()
{
	//源纹理尺寸（用于越界钳制）
	ivec2 source_size = textureSize(source_image, 0);
	//宿像素 → 源像素映射比例（1:1 时为 1）
	vec2 source_scale = push_post.size_xy.xy / push_post.size_xy.zw;
	//中心像素坐标
	ivec2 center = ivec2(gl_FragCoord.xy * source_scale);
	//模糊半径（四舍五入后钳到 [1, 4]）
	int radius = int(clamp(floor(push_post.post_param.z + 0.5), 1.0, 4.0));
	//三角核权重归一化分母
	float norm = float((radius + 1) * (radius + 1));
	norm = norm * norm;
	//累加颜色
	vec3 sum = vec3(0.0);
	//(2N+1)×(2N+1) 三角权重卷积
	for (int dy = -radius; dy <= radius; ++dy)
	{
		for (int dx = -radius; dx <= radius; ++dx)
		{
			//邻域权重（中心最大，向外线性递减）
			float weight = float(radius + 1 - abs(dx)) * float(radius + 1 - abs(dy)) / norm;
			//邻域坐标（钳到 [0, size-1]）
			ivec2 coord = clamp(center + ivec2(dx, dy), ivec2(0), source_size - ivec2(1));
			//加权累加
			sum += texelFetch(source_image, coord, 0).rgb * weight;
		}
	}
	out_color = vec4(sum, 1.0);
}
