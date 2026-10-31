#include "src/render/HiZ/局部命名空间使用.h"
//遮挡测试公开面（Vulkan-free）
#include "src/render/HiZ/遮挡测试.h"
//std::ceil / std::log2 / std::isfinite / std::floor
#include <cmath>
//std::min / std::max
#include <algorithm>

namespace engine
{
	namespace
	{
		//单个角点经矩阵变换后的裁剪坐标
		struct Clip_Point
		{
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			float w = 0.0f;
		};

		//变换一个世界点（列主序矩阵）
		Clip_Point transform_point(const float matrix[16], float x, float y, float z)
		{
			Clip_Point point;
			point.x = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
			point.y = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
			point.z = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
			point.w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
			return point;
		}
	}

	/*
	投影世界 AABB 为屏幕矩形
	逐 8 角点变换后取屏幕坐标包围盒与最小深度；任一角点 w ≤ 0 即置跨近平面标志（保守判可见）。
	*/
	bool hiz_screen_rect_build(HiZ_Screen_Rect& out, const float view_proj[16],
		const float aabb_min[3], const float aabb_max[3], uint32_t width, uint32_t height)
	{
		//入参校验
		if (view_proj == nullptr || aabb_min == nullptr || aabb_max == nullptr || width == 0 || height == 0)
			return false;
		//矩阵与范围须为有限值
		for (uint32_t index = 0; index < 16; ++index)
			if (!std::isfinite(view_proj[index]))
				return false;
		for (uint32_t axis = 0; axis < 3; ++axis)
			if (!std::isfinite(aabb_min[axis]) || !std::isfinite(aabb_max[axis])
				|| aabb_min[axis] > aabb_max[axis])
				return false;
		out = HiZ_Screen_Rect{};
		out.cross_near = false;
		//初始化包围盒（首角点覆盖）
		bool first_valid = true;
		float nearest_depth = 1.0f;
		//逐角点变换
		for (uint32_t corner = 0; corner < 8; ++corner)
		{
			//角点世界坐标
			const float x = (corner & 1u) != 0 ? aabb_max[0] : aabb_min[0];
			const float y = (corner & 2u) != 0 ? aabb_max[1] : aabb_min[1];
			const float z = (corner & 4u) != 0 ? aabb_max[2] : aabb_min[2];
			Clip_Point point = transform_point(view_proj, x, y, z);
			//跨近平面 / 相机后方：保守判可见，仍继续计算包围盒
			if (point.w <= 0.0f)
			{
				out.cross_near = true;
				continue;
			}
			//透视除法
			const float ndc_x = point.x / point.w;
			const float ndc_y = point.y / point.w;
			const float ndc_z = point.z / point.w;
			//屏幕坐标（与 Vulkan 视口一致：NDC +1 对应第 0 行）
			const float screen_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
			const float screen_y = (0.5f - ndc_y * 0.5f) * static_cast<float>(height);
			//首角点初始化
			if (first_valid)
			{
				out.min_x = screen_x;
				out.max_x = screen_x;
				out.min_y = screen_y;
				out.max_y = screen_y;
				nearest_depth = ndc_z;
				first_valid = false;
			}
			else
			{
				//扩展包围盒与最近深度
				out.min_x = (std::min)(out.min_x, screen_x);
				out.max_x = (std::max)(out.max_x, screen_x);
				out.min_y = (std::min)(out.min_y, screen_y);
				out.max_y = (std::max)(out.max_y, screen_y);
				nearest_depth = (std::min)(nearest_depth, ndc_z);
			}
		}
		//无有效角点：视为跨近平面（可见）
		if (first_valid)
		{
			out.cross_near = true;
			return true;
		}
		out.nearest_depth = nearest_depth;
		return true;
	}

	//按矩形尺寸选择级（覆盖纹素 ≤ 2×2）
	uint32_t hiz_level_for_rect(float rect_width, float rect_height, uint32_t levels)
	{
		//无级可用
		if (levels == 0)
			return 0;
		//矩形小于 1 像素
		const float extent = (std::max)(1.0f, (std::max)(rect_width, rect_height));
		//向上取整到 2 的幂
		uint32_t level = static_cast<uint32_t>(std::ceil(std::log2(extent)));
		//钳到有效级
		if (level >= levels)
			level = levels - 1;
		return level;
	}

	//取指定级的数据与尺寸
	bool hiz_pyramid_level_get(const HiZ_Pyramid_View& pyramid, uint32_t level,
		const float*& data_out, uint32_t& width_out, uint32_t& height_out)
	{
		//出参先归零
		data_out = nullptr;
		width_out = 0;
		height_out = 0;
		//视图非法
		if (pyramid.data == nullptr || pyramid.width == 0 || pyramid.height == 0
			|| pyramid.levels == 0 || level >= pyramid.levels || pyramid.levels > HIZ_OCCLUSION_LEVEL_MAX)
			return false;
		//逐级紧排偏移
		uint32_t offset = 0;
		for (uint32_t current = 0; current < level; ++current)
		{
			const uint32_t level_width = (std::max)(1u, pyramid.width >> current);
			const uint32_t level_height = (std::max)(1u, pyramid.height >> current);
			offset += level_width * level_height;
		}
		data_out = pyramid.data + offset;
		width_out = (std::max)(1u, pyramid.width >> level);
		height_out = (std::max)(1u, pyramid.height >> level);
		return true;
	}

	/*
	遮挡判定（保守）
	覆盖区域取覆盖纹素的最大值（最远遮挡面），对象最近深度严格大于该值 ＋ bias 才剔除。
	*/
	HiZ_Visibility hiz_occlusion_test(const HiZ_Pyramid_View& pyramid, const HiZ_Screen_Rect& rect,
		float depth_bias)
	{
		//跨近平面 / 无有效投影判可见
		if (rect.cross_near)
			return HiZ_Visibility::Visible;
		//在视图外判可见（视锥剔除负责，不在此多剔）
		if (rect.max_x < 0.0f || rect.max_y < 0.0f
			|| rect.min_x >= static_cast<float>(pyramid.width) || rect.min_y >= static_cast<float>(pyramid.height))
			return HiZ_Visibility::Visible;
		//选择级
		const float rect_width = rect.max_x - rect.min_x;
		const float rect_height = rect.max_y - rect.min_y;
		const uint32_t level = hiz_level_for_rect(rect_width, rect_height, pyramid.levels);
		//取该级
		const float* level_data = nullptr;
		uint32_t level_width = 0;
		uint32_t level_height = 0;
		if (!hiz_pyramid_level_get(pyramid, level, level_data, level_width, level_height))
			return HiZ_Visibility::Visible;
		//矩形到该级纹素范围（缩放 = 2^level）
		const uint32_t scale = 1u << level;
		const float inverse_scale = 1.0f / static_cast<float>(scale);
		int min_x = static_cast<int>(std::floor(rect.min_x * inverse_scale));
		int min_y = static_cast<int>(std::floor(rect.min_y * inverse_scale));
		int max_x = static_cast<int>(std::floor(rect.max_x * inverse_scale));
		int max_y = static_cast<int>(std::floor(rect.max_y * inverse_scale));
		//钳到该级范围
		min_x = (std::max)(0, (std::min)(min_x, static_cast<int>(level_width) - 1));
		max_x = (std::max)(0, (std::min)(max_x, static_cast<int>(level_width) - 1));
		min_y = (std::max)(0, (std::min)(min_y, static_cast<int>(level_height) - 1));
		max_y = (std::max)(0, (std::min)(max_y, static_cast<int>(level_height) - 1));
		//覆盖纹素的最大远面深度（保守）
		float farthest = 0.0f;
		for (int ty = min_y; ty <= max_y; ++ty)
		{
			for (int tx = min_x; tx <= max_x; ++tx)
			{
				const float value = level_data[static_cast<uint32_t>(ty) * level_width + static_cast<uint32_t>(tx)];
				farthest = (std::max)(farthest, value);
			}
		}
		//对象最近深度须严格大于远面深度 ＋ bias 才完全遮挡
		if (rect.nearest_depth > farthest + depth_bias)
			return HiZ_Visibility::Occluded;
		return HiZ_Visibility::Visible;
	}

	/*
	CPU 降采样（与着色器同口径）
	2×2 邻域取最大值，邻域越界钳到最后一行 / 列。
	*/
	bool hiz_pyramid_downsample(const float* source, uint32_t source_width, uint32_t source_height,
		float* dest, uint32_t dest_width, uint32_t dest_height)
	{
		//入参校验
		if (source == nullptr || dest == nullptr || source_width == 0 || source_height == 0
			|| dest_width == 0 || dest_height == 0)
			return false;
		//源最后一行 / 列（钳制边界）
		const uint32_t source_max_x = source_width - 1;
		const uint32_t source_max_y = source_height - 1;
		for (uint32_t y = 0; y < dest_height; ++y)
		{
			for (uint32_t x = 0; x < dest_width; ++x)
			{
				//2×2 邻域最大值
				float farthest = 0.0f;
				for (uint32_t offset_y = 0; offset_y < 2; ++offset_y)
				{
					for (uint32_t offset_x = 0; offset_x < 2; ++offset_x)
					{
						//钳到源图范围
						const uint32_t sample_x = (std::min)(x * 2 + offset_x, source_max_x);
						const uint32_t sample_y = (std::min)(y * 2 + offset_y, source_max_y);
						const float value = source[sample_y * source_width + sample_x];
						farthest = (std::max)(farthest, value);
					}
				}
				dest[y * dest_width + x] = farthest;
			}
		}
		return true;
	}
}
