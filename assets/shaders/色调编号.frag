#version 450
/*
逐材质色调编号 · 片元着色器（第 808 轮 · 8 位路径编号预通道）
把实例属性 z（0..4 = 具体算子、255 = 继承全局）编码为 1..5 写入 R8_UINT 编号附件；
0 表示「无覆盖」，供末级按「id 附件 > alpha > 全局」解码。
本通道在场景 pass 之后以同一深度图绘制（比较 LESS_OR_EQUAL、不写深度），
故只有最终可见片元留下编号；天空盒 / 粒子 / 透明项的算子为 255，自然写 0。
*/
layout(location = 0) flat in vec4 in_pbr_features;
layout(location = 0) out uint out_tone;

void main()
{
	//逐物体算子（0..4 = 具体曲线；其余值 = 继承全局）
	float op = in_pbr_features.z;
	//编码（0 = 无覆盖、1..5 = 算子 + 1）
	out_tone = (op >= 0.0 && op <= 4.0) ? (uint(floor(op + 0.5)) + 1u) : 0u;
}
