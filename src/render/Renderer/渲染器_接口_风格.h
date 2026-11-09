#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：风格
	*/
	class Renderer_Style : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		PBR-lite 镜面开关（第六十四轮新增）
		开启后四类光源的高光改用 GGX（粗糙度由逐盏光泽度换算），关闭时沿用 Blinn-Phong；
		默认关闭，故既有画面逐字节不变。
		*/
		void pbr_enable_set(bool enabled);
		//PBR-lite 是否启用（未初始化时为默认 false）
		bool pbr_enabled(void) const;
		/*
		PBR-lite 粗糙度缩放（第六十四轮新增；合法 0.05..4，越界整次忽略）
		最终粗糙度 = clamp(sqrt(2 / (shininess + 2)) × 缩放, 0.03, 1)；默认 1。
		*/
		void pbr_roughness_set(float scale);
		//当前 PBR-lite 粗糙度缩放（未初始化时为默认 1）
		float pbr_roughness(void) const;
		/*
		PBR 金属度（第六十五轮新增；合法 0..1，越界整次忽略；默认 0）
		PBR 开启时 F0 由 0.04 向反照率插值，漫反射按 (1 − metallic) 做能量守恒；关闭时不生效。
		*/
		void pbr_metallic_set(float metallic);
		//当前 PBR 金属度（未初始化时为默认 0）
		float pbr_metallic(void) const;
		/*
		PBR 掠射高光能量修正（第六百七十二轮；D1 第一步）
		开启且 PBR 开启时镜面项乘入射余弦 NoL，修正掠射角过亮；默认关闭（逐字节不变）。
		*/
		void pbr_energy_set(bool enabled);
		//当前 PBR 掠射高光能量修正是否开启（未初始化时为默认假）
		bool pbr_energy(void) const;
		/*
		PBR 粗糙度绝对口径（第七百轮；D1 收尾）
		开启且 PBR 开启时，材质 / 全局粗糙度值直接作为 GGX 粗糙度，不再由镜面光泽度换算；
		默认关闭（逐字节不变）。
		*/
		void pbr_roughness_absolute_set(bool enabled);
		//当前 PBR 粗糙度绝对口径是否开启（未初始化时为默认假）
		bool pbr_roughness_absolute(void) const;
		/*
		二次元风格化 · 光照色阶量化与边缘光（第一百七十四轮新增）
		steps：色阶数（0 = 关闭，原样光照；1..8 = 把各光源的 N·L 量化到 N 档，形成硬边明暗分界）；
		rim：菲涅尔边缘光强度（0 = 关闭；0..1，轮廓处叠加反照率亮边）。
		默认 (0, 0) 使画面与引入前逐字节不变；数据侧对应 `Light_Data::toon_steps` / `toon_rim`。
		*/
		void toon_set(uint32_t steps, float rim);
		//当前色阶数（0 = 关闭；未初始化时为 0）
		uint32_t toon_steps(void) const;
		//当前边缘光强度（0 = 关闭；未初始化时为 0）
		float toon_rim(void) const;
		/*
		设置二次元暗部色调（第一百七十八轮新增）
		rgb 为暗部（主方向光量化受光系数低于 0.5 的区域）乘用的色调，各分量 0..1（非有限值按 1）；
		(1,1,1) 为关闭（乘性恒等），默认值即 (1,1,1)，故不调用时画面逐字节不变。
		数据侧对应 `Light_Data::toon_shadow_tint` / `Scene_Data::toon_shadow_tint`。
		*/
		void toon_shadow_tint_set(float r, float g, float b);
		//读取暗部色调（第一百七十八轮新增；未初始化写默认 (1,1,1)）
		void toon_shadow_tint(float out[3]) const;
		/*
		设置卡通高光阈值（第一百七十九轮新增）
		threshold 为高光亮度二值化阈值（0 = 关闭；0..1）：高于阈值的高光整块保留、低于阈值整块归零，
		形成硬边高光块；默认 0 使画面与引入前逐字节不变。数据侧对应 `Light_Data::toon_specular`。
		*/
		void toon_specular_set(float threshold);
		//读取卡通高光阈值（第一百七十九轮新增；未初始化返回 0）
		float toon_specular(void) const;
		/*
		设置反向外扩描边（第二百二十八轮）
		width 为参考距离（3 世界单位）处沿顶点法线的外扩宽度（0 = 关闭；0..1，越界钳制，非有限值按 0）；
		更远处的顶点按「视距 / 3」等比放大外扩量（第二百三十轮），使轮廓的屏幕像素宽度基本恒定。
		color 为描边色（RGB 0..1，非有限分量按 0）。开启后每个场景绘制项额外登记一条描边项
		（剔除正面、只写外轮廓），故 `Frame_Stats::draw_items` 相应增加。
		法线为零向量的顶点（如平面）不外扩；文字绘制不参与描边；描边管线登记失败时本设置无效。
		数据侧对应 `Light_Data::outline_width` / `outline_color`。
		*/
		void outline_set(float width, const float color[3]);
		//关闭反向外扩描边（等价于 outline_set(0, 任意色)；第二百二十八轮）
		void outline_clear(void);
		//当前外扩宽度（未初始化返回 0；第二百二十八轮）
		float outline_width(void) const;
		//读取当前描边色（未初始化写 (0,0,0)；第二百二十八轮）
		void outline_color(float out[3]) const;
	};
}
