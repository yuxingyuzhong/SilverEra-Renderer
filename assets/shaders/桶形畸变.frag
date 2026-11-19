#version 450
/*
后处理片元着色器 · 桶形畸变（第一百一十八轮；链式后处理预设）
按像素到屏幕中心的距离平方施加径向缩放，形成桶形（枕形取负）畸变。
参数经 post_param.x 下发（阈值参数），映射为 0..0.3 的畸变强度。
*/
//颜色附件输出
layout(location = 0) out vec4 out_color;

//源纹理（描述符集 1）：上一级的输出
layout(set = 1, binding = 0) uniform sampler2D source_image;

//推送常量：源 / 宿尺寸（offset 80）与后处理参数（offset 96）
layout(push_constant) uniform Push_Post
{
	layout(offset = 80) vec4 size_xy;
	layout(offset = 96) vec4 post_param;
} push;

void main()
{
	//宿 UV（0..1）与源 / 宿比例
	vec2 uv = gl_FragCoord.xy / push.size_xy.zw;
	vec2 source_scale = push.size_xy.xy / push.size_xy.zw;
	//畸变强度（限制在 0..0.3）
	float strength = clamp(push.post_param.x, 0.0, 1.0) * 0.3;
	//以中心为原点的归一化坐标与半径平方
	vec2 centered = (uv - vec2(0.5)) * 2.0;
	float radius_squared = dot(centered, centered);
	//径向缩放后回到 0..1 UV
	vec2 distorted = centered * (1.0 + strength * radius_squared);
	vec2 source_uv = clamp(distorted * 0.5 + 0.5, vec2(0.0), vec2(1.0));
	//采样源纹理（连同 alpha：透传逐材质色调编号；双线性会在畸变边缘混编号，末级按最近编号解码）
	vec4 source = texture(source_image, source_uv * source_scale);
	vec3 color = source.rgb;

	out_color = vec4(color, source.a);
}
