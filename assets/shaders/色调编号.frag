#version 450
/*
逐材质色调编号 · 片元着色器（第 808 轮 · 8 位路径编号预通道；第 819 轮 alpha 裁剪）
把实例属性 z（0..4 = 具体算子、255 = 继承全局）编码为 1..5 写入 R8_UINT 编号附件；
0 表示「无覆盖」，供末级按「id 附件 > alpha > 全局」解码。
alpha 裁剪（第 819 轮）：与主 pass 同式采样反照率并比较裁剪阈值，保证编号与可见性一致；
未声明裁剪（阈值 0）时不做任何采样与丢弃。
本通道在场景 pass 之后以同一深度图绘制（比较 LESS_OR_EQUAL、不写深度）。
*/
layout(location = 0) flat in vec4 in_pbr_features;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out uint out_tone;

//反照率贴图（set 1 binding 0，与场景集同布局）
layout(set = 1, binding = 0) uniform sampler2D albedo_image;

/*
推送常量（与场景管线共享布局）
offset 64 = 逐物体色调（rgb = 色调、a = alpha 乘子）
offset 80 = 材质扩展（z = alpha 裁剪阈值，0 = 关闭；w = UV 旋转）
offset 96 = 材质 UV 变换（xy = 缩放、zw = 偏移）
*/
layout(push_constant) uniform Push_Tint
{
	layout(offset = 64) vec4 tint;
	layout(offset = 80) vec4 material_extras;
	layout(offset = 96) vec4 material_uv;
} push_tint;

void main()
{
	//逐物体算子（0..4 = 具体曲线；其余值 = 继承全局）
	float op = in_pbr_features.z;
	//alpha 裁剪（阈值 > 0 时才采样，避免无谓贴图读取）
	if (push_tint.material_extras.z > 0.0)
	{
		//材质 UV 变换（缩放 → 旋转 → 偏移，与场景片元同序）
		vec2 uv_scaled = in_uv * push_tint.material_uv.xy;
		if (push_tint.material_extras.w != 0.0)
		{
			float rotation_cos = cos(push_tint.material_extras.w);
			float rotation_sin = sin(push_tint.material_extras.w);
			uv_scaled = vec2(uv_scaled.x * rotation_cos - uv_scaled.y * rotation_sin,
				uv_scaled.x * rotation_sin + uv_scaled.y * rotation_cos);
		}
		vec2 uv_material = uv_scaled + push_tint.material_uv.zw;
		//反照率 alpha（含色调 alpha 乘子）低于阈值则丢弃，与主 pass 可见性一致
		float albedo_alpha = texture(albedo_image, uv_material).a * push_tint.tint.a;
		if (albedo_alpha < push_tint.material_extras.z)
			discard;
	}
	//编码（0 = 无覆盖、1..5 = 算子 + 1）
	out_tone = (op >= 0.0 && op <= 4.0) ? (uint(floor(op + 0.5)) + 1u) : 0u;
}
