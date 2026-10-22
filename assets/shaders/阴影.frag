#version 450
//阴影片元着色器（第五十二轮）：把深度打包进 RGBA8（32 位精度），供场景片元解包比较
layout(location = 0) in float in_depth;
//颜色附件输出（目标为 RGBA8，故用 4 通道承载 32 位深度）
layout(location = 0) out vec4 out_color;

/*
深度打包（经典 RGBA8 打包）
把 0..1 深度按 1/255 递进拆到 4 个通道：r + g/255 + b/65025 + a/16581375。
*/
vec4 pack_depth(float depth)
{
	vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * depth;
	enc = fract(enc);
	enc -= enc.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
	return enc;
}

void main()
{
	out_color = pack_depth(clamp(in_depth, 0.0, 1.0));
}
