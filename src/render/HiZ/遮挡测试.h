#pragma once
#include <cstdint>
/*
Hi-Z 遮挡测试（Vulkan-free；第二百五十五轮）
方向一规划 §2.8 的 CPU 参考实现：把世界 AABB 投影到屏幕矩形，按矩形尺寸选择金字塔级，
以「对象最近深度 > 覆盖区域的远面深度（保守取覆盖纹素的最大值）」判定完全遮挡。

口径与 GPU 着色器（256 轮接入）一致：
- 裁剪空间：clip = view_proj × world；任一角点 w ≤ 0（跨近平面 / 相机后方）→ 判可见（保守）；
- 屏幕坐标：ndc = clip.xyz / clip.w；x = (ndc.x × 0.5 + 0.5) × 宽，y = (0.5 − ndc.y × 0.5) × 高（与 Vulkan 视口一致：NDC +1 对应第 0 行）；
- 深度：z = ndc.z（Vulkan 0..1，越小越近）；对象最近深度 = 8 角点的最小 z；
- 金字塔每纹素存该区域的**最大**深度（最远遮挡面）；覆盖纹素取**最大值**（保守）。
*/

//游戏引擎命名空间
namespace engine
{
	//遮挡测试支持的最大级数（与 HiZ 金字塔级数上限同口径）
	constexpr uint32_t HIZ_OCCLUSION_LEVEL_MAX = 12;
	//遮挡判定容差（深度单位；越大越保守，可见侧增加）
	constexpr float HIZ_OCCLUSION_BIAS = 0.002f;

	/*
	金字塔视图（多级紧排）
	data 为第 0 级起逐级紧排的浮点数组；level_offsets 由本模块按尺寸换算（不存于结构内）。
	*/
	struct HiZ_Pyramid_View
	{
		//第 0 级数据指针（逐级紧排）
		const float* data = nullptr;
		//第 0 级宽高
		uint32_t width = 0;
		uint32_t height = 0;
		//级数（1 表示仅全分辨率）
		uint32_t levels = 0;
	};

	/*
	屏幕矩形与深度
	min_x / min_y / max_x / max_y 为像素坐标（可在视图外）；nearest_depth 为对象最近深度；
	cross_near 为真表示存在 w ≤ 0 的角点（跨近平面或相机后方）。
	*/
	struct HiZ_Screen_Rect
	{
		float min_x = 0.0f;
		float min_y = 0.0f;
		float max_x = 0.0f;
		float max_y = 0.0f;
		float nearest_depth = 1.0f;
		bool cross_near = false;
	};

	//遮挡判定结果
	enum class HiZ_Visibility
	{
		//未被完全遮挡（保守口径下判可见）
		Visible,
		//完全位于遮挡面之后（可剔除）
		Occluded,
	};

	/*
	投影世界 AABB 为屏幕矩形
	view_proj：列主序 4×4（视图 × 投影）；width / height 为金字塔第 0 级尺寸（像素）。
	入参非法（空指针 / 非有限值）返回假且不写出结果；跨近平面的情况不视为失败（置 cross_near）。
	*/
	bool hiz_screen_rect_build(HiZ_Screen_Rect& out, const float view_proj[16],
		const float aabb_min[3], const float aabb_max[3], uint32_t width, uint32_t height);

	/*
	按矩形尺寸选择级
	目标：覆盖纹素数不超过 2×2；级 = ceil(log2(max(宽, 高))) 截到 [0, levels-1]；
	矩形小于 1 像素时取第 0 级。
	*/
	uint32_t hiz_level_for_rect(float rect_width, float rect_height, uint32_t levels);

	/*
	取指定级的数据与尺寸（越界或级数非法返回假）
	*/
	bool hiz_pyramid_level_get(const HiZ_Pyramid_View& pyramid, uint32_t level,
		const float*& data_out, uint32_t& width_out, uint32_t& height_out);

	/*
	遮挡判定（保守）
	矩形跨近平面 / 在视图外 / 金字塔无效 → 可见；否则覆盖纹素取最大值，
	对象最近深度 > 该最大值 ＋ bias 才判完全遮挡。
	*/
	HiZ_Visibility hiz_occlusion_test(const HiZ_Pyramid_View& pyramid, const HiZ_Screen_Rect& rect,
		float depth_bias = HIZ_OCCLUSION_BIAS);

	/*
	CPU 降采样（与 `HiZ降采样.comp` 同口径）
	逐目标纹素取 2×2 邻域最大值，邻域越界钳到源图最后一行 / 列；
	目标与源同尺寸时退化为逐纹素拷贝。入参非法返回假。
	*/
	bool hiz_pyramid_downsample(const float* source, uint32_t source_width, uint32_t source_height,
		float* dest, uint32_t dest_width, uint32_t dest_height);
}
