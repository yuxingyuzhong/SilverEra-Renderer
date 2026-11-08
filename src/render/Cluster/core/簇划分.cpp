#include "src/render/Cluster/簇划分.h"


//游戏引擎命名空间
namespace engine
{
	namespace
	{
		//三维向量长度
		float length3(const float v[3])
		{
			return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		}

		//列主序矩阵 × 三维点（写出齐次坐标 x / y / z / w）
		void transform_point(const float m[16], const float p[3], float out[4])
		{
			out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
			out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
			out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
			out[3] = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
		}

		//浮点钳到 [low, high]（NaN 归 low）
		float clamp_float(float value, float low, float high)
		{
			//NaN 或下界外
			if (!(value >= low))
				return low;
			return value > high ? high : value;
		}

		//浮点 → 网格下标（floor 后钳到 [0, axis-1]）
		int32_t index_floor(float value, uint32_t axis)
		{
			int32_t index = static_cast<int32_t>(std::floor(value));
			if (index < 0)
				return 0;
			return index >= static_cast<int32_t>(axis) ? static_cast<int32_t>(axis) - 1 : index;
		}
	}

	uint32_t cluster_counts_reference(uint32_t* counts_out, uint32_t capacity, Cluster_Grid grid,
		const Light_Record* lights, uint32_t light_count, const Cluster_Input& input)
	{
		//网格总簇数
		uint32_t total = cluster_grid_clusters(grid);
		//容量不足或入参非法
		if (counts_out == nullptr || total == 0 || capacity < total || lights == nullptr || light_count == 0)
			return 0;
		//计数先清零
		std::memset(counts_out, 0, static_cast<std::size_t>(total) * sizeof(uint32_t));
		//垂直半视场正切（非法时退化为 1，保证不除零）
		float tan_half = std::tan(input.fov * 0.5f);
		if (!(tan_half > 1e-6f))
			tan_half = 1.0f;
		//近远平面深度跨度
		float depth_span = input.far_plane - input.near_plane;
		if (!(depth_span > 1e-6f))
			depth_span = 1e-6f;
		//轴数（循环内复用）
		uint32_t axis_x = grid.x;
		uint32_t axis_y = grid.y;
		uint32_t axis_z = grid.z;
		for (uint32_t light_index = 0; light_index < light_count; ++light_index)
		{
			const Light_Record& light = lights[light_index];
			//跳过未启用的记录
			if ((light.flags & 1u) == 0)
				continue;
			//方向光照亮全部簇
			if (light.type == static_cast<uint32_t>(Light_Type::Directional))
			{
				//逐簇累加
				for (uint32_t index = 0; index < total; ++index)
					counts_out[index] += 1;
				continue;
			}
			//包围球参数
			const float center[3] = { light.pos_radius[0], light.pos_radius[1], light.pos_radius[2] };
			float radius = light.pos_radius[3];
			//半径为负按 0 处理
			if (!(radius > 0.0f))
				radius = 0.0f;
			//球心到相机的距离（视图深度近似）
			const float offset[3] = {
				center[0] - input.camera_position[0],
				center[1] - input.camera_position[1],
				center[2] - input.camera_position[2],
			};
			float distance = length3(offset);
			//球心投影到裁剪空间
			float clip[4] = {};
			transform_point(input.view_proj, center, clip);
			/*
			屏幕矩形与视图深度区间
			默认覆盖全屏与全部深度；球在相机前方时按透视缩放收紧。
			*/
			float u0 = 0.0f;
			float u1 = 1.0f;
			float v0 = 0.0f;
			float v1 = 1.0f;
			float z_low = input.near_plane;
			float z_high = input.far_plane;
			//球心在相机前方（w 为正）时按半径估算屏幕矩形的保守外扩
			if (clip[3] > 1e-6f)
			{
				float inv_w = 1.0f / clip[3];
				//球心屏幕坐标（v 自上而下，与引擎投影 Y 翻转一致）
				float center_u = clip[0] * inv_w * 0.5f + 0.5f;
				float center_v = 0.5f - clip[1] * inv_w * 0.5f;
				/*
				半高世界尺寸 → 半径占屏幕比例（第三百零四轮修正）
				NDC 半高 1.0 对应 distance × tan_half，球投影半径即 radius / (distance × tan_half)；
				原式多除以 2，小半径光源的覆盖矩形偏小（片元在球内却不在簇列表里）。
				*/
				float denom = distance * tan_half;
				float dv = denom > 1e-6f ? radius / denom : 1.0f;
				float du = input.aspect > 1e-6f ? dv / input.aspect : 1.0f;
				u0 = center_u - du;
				u1 = center_u + du;
				v0 = center_v - dv;
				v1 = center_v + dv;
				z_low = distance - radius;
				z_high = distance + radius;
			}
			//跨近平面或位于相机背后：整屏且全部深度（保守）
			if (clip[3] <= 1e-6f || z_low <= input.near_plane)
			{
				u0 = 0.0f;
				u1 = 1.0f;
				v0 = 0.0f;
				v1 = 1.0f;
				z_low = input.near_plane;
				z_high = input.far_plane;
			}
			//屏幕矩形完全在视口外
			if (u1 < 0.0f || u0 > 1.0f || v1 < 0.0f || v0 > 1.0f)
				continue;
			//深度区间完全在近远平面外
			if (z_high < input.near_plane || z_low > input.far_plane)
				continue;
			//钳到视口
			u0 = clamp_float(u0, 0.0f, 1.0f);
			u1 = clamp_float(u1, 0.0f, 1.0f);
			v0 = clamp_float(v0, 0.0f, 1.0f);
			v1 = clamp_float(v1, 0.0f, 1.0f);
			/*
			索引范围（保守上界）
			下界取 floor，上界取 ceil - 1，保证包含边界簇。
			*/
			int32_t ix0 = index_floor(u0 * static_cast<float>(axis_x), axis_x);
			int32_t ix1 = index_floor(std::ceil(u1 * static_cast<float>(axis_x)) - 1.0f, axis_x);
			int32_t iy0 = index_floor(v0 * static_cast<float>(axis_y), axis_y);
			int32_t iy1 = index_floor(std::ceil(v1 * static_cast<float>(axis_y)) - 1.0f, axis_y);
			int32_t iz0 = index_floor((z_low - input.near_plane) / depth_span * static_cast<float>(axis_z), axis_z);
			int32_t iz1 = index_floor(std::ceil((z_high - input.near_plane) / depth_span * static_cast<float>(axis_z)) - 1.0f, axis_z);
			//逐簇累加
			for (int32_t iz = iz0; iz <= iz1; ++iz)
				for (int32_t iy = iy0; iy <= iy1; ++iy)
					for (int32_t ix = ix0; ix <= ix1; ++ix)
						counts_out[(static_cast<uint32_t>(iz) * axis_y + static_cast<uint32_t>(iy)) * axis_x
							+ static_cast<uint32_t>(ix)] += 1;
		}
		return total;
	}

	uint32_t cluster_prefix_block_scan(uint32_t* prefix_out, uint32_t* block_totals_out, uint32_t block_capacity,
		const uint32_t* counts, uint32_t count)
	{
		//入参非法
		if (prefix_out == nullptr || block_totals_out == nullptr || counts == nullptr || count == 0)
			return 0;
		//块数与容量
		uint32_t block_count = (count + CLUSTER_SCAN_BLOCK - 1) / CLUSTER_SCAN_BLOCK;
		if (block_capacity < block_count)
			return 0;
		//逐块包含式前缀和
		for (uint32_t block = 0; block < block_count; ++block)
		{
			uint32_t base = block * CLUSTER_SCAN_BLOCK;
			uint32_t limit = base + CLUSTER_SCAN_BLOCK;
			if (limit > count)
				limit = count;
			uint32_t running = 0;
			for (uint32_t index = base; index < limit; ++index)
			{
				running += counts[index];
				prefix_out[index] = running;
			}
			block_totals_out[block] = running;
		}
		return block_count;
	}

	uint32_t cluster_prefix_scan(uint32_t* prefix_out, const uint32_t* counts, uint32_t count)
	{
		//入参非法
		if (prefix_out == nullptr || counts == nullptr || count == 0)
			return 0;
		//全局包含式前缀和
		uint32_t running = 0;
		for (uint32_t index = 0; index < count; ++index)
		{
			running += counts[index];
			prefix_out[index] = running;
		}
		return count;
	}

	uint32_t cluster_list_offsets(const uint32_t* counts, uint32_t cluster_total, uint32_t* offsets_out)
	{
		//入参非法
		if (counts == nullptr || offsets_out == nullptr || cluster_total == 0)
			return 0;
		//逐簇累加排他偏移
		uint32_t running = 0;
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
		{
			offsets_out[cluster] = running;
			running += counts[cluster];
		}
		return running;
	}

	uint32_t cluster_list_reference(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_io,
		const uint32_t* counts, uint32_t cluster_total, Cluster_Grid grid,
		const Light_Record* lights, uint32_t light_count, const Cluster_Input& input)
	{
		//入参非法
		if (list_out == nullptr || offsets_io == nullptr || counts == nullptr || lights == nullptr
			|| light_count == 0 || cluster_total == 0)
			return 0;
		//总条目数与容量
		uint32_t entries = 0;
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
			entries += counts[cluster];
		if (capacity < entries)
			return 0;
		//垂直半视场正切（非法时退化为 1，保证不除零）
		float tan_half = std::tan(input.fov * 0.5f);
		if (!(tan_half > 1e-6f))
			tan_half = 1.0f;
		//近远平面深度跨度
		float depth_span = input.far_plane - input.near_plane;
		if (!(depth_span > 1e-6f))
			depth_span = 1e-6f;
		//轴数（循环内复用）
		uint32_t axis_x = grid.x;
		uint32_t axis_y = grid.y;
		uint32_t axis_z = grid.z;
		for (uint32_t light_index = 0; light_index < light_count; ++light_index)
		{
			const Light_Record& light = lights[light_index];
			//跳过未启用的记录
			if ((light.flags & 1u) == 0)
				continue;
			//方向光写入全部簇
			if (light.type == static_cast<uint32_t>(Light_Type::Directional))
			{
				//逐簇追加光源索引
				for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
					list_out[offsets_io[cluster]++] = light_index;
				continue;
			}
			//包围球参数
			const float center[3] = { light.pos_radius[0], light.pos_radius[1], light.pos_radius[2] };
			float radius = light.pos_radius[3];
			//半径为负按 0 处理
			if (!(radius > 0.0f))
				radius = 0.0f;
			//球心到相机的距离
			const float offset[3] = {
				center[0] - input.camera_position[0],
				center[1] - input.camera_position[1],
				center[2] - input.camera_position[2],
			};
			float distance_to_camera = length3(offset);
			//球心投影到裁剪空间
			float clip[4] = {};
			transform_point(input.view_proj, center, clip);
			//屏幕矩形与视图深度区间（默认全屏全深度）
			float u0 = 0.0f;
			float u1 = 1.0f;
			float v0 = 0.0f;
			float v1 = 1.0f;
			float z_low = input.near_plane;
			float z_high = input.far_plane;
			//球心在相机前方时按半径收紧
			if (clip[3] > 1e-6f)
			{
				float inv_w = 1.0f / clip[3];
				float center_u = clip[0] * inv_w * 0.5f + 0.5f;
				float center_v = 0.5f - clip[1] * inv_w * 0.5f;
				//半高世界尺寸 → 半径占屏幕比例（第三百零四轮修正：去掉多余的 × 2，与 CPU 参考同口径）
				float denom = distance_to_camera * tan_half;
				float dv = denom > 1e-6f ? radius / denom : 1.0f;
				float du = input.aspect > 1e-6f ? dv / input.aspect : 1.0f;
				u0 = center_u - du;
				u1 = center_u + du;
				v0 = center_v - dv;
				v1 = center_v + dv;
				z_low = distance_to_camera - radius;
				z_high = distance_to_camera + radius;
			}
			//跨近平面或位于相机背后：整屏全深度
			if (clip[3] <= 1e-6f || z_low <= input.near_plane)
			{
				u0 = 0.0f;
				u1 = 1.0f;
				v0 = 0.0f;
				v1 = 1.0f;
				z_low = input.near_plane;
				z_high = input.far_plane;
			}
			//屏幕矩形完全在视口外
			if (u1 < 0.0f || u0 > 1.0f || v1 < 0.0f || v0 > 1.0f)
				continue;
			//深度区间完全在近远平面外
			if (z_high < input.near_plane || z_low > input.far_plane)
				continue;
			//钳到视口
			u0 = clamp_float(u0, 0.0f, 1.0f);
			u1 = clamp_float(u1, 0.0f, 1.0f);
			v0 = clamp_float(v0, 0.0f, 1.0f);
			v1 = clamp_float(v1, 0.0f, 1.0f);
			//索引范围（保守上界，与计数参考一致）
			int32_t ix0 = index_floor(u0 * static_cast<float>(axis_x), axis_x);
			int32_t ix1 = index_floor(std::ceil(u1 * static_cast<float>(axis_x)) - 1.0f, axis_x);
			int32_t iy0 = index_floor(v0 * static_cast<float>(axis_y), axis_y);
			int32_t iy1 = index_floor(std::ceil(v1 * static_cast<float>(axis_y)) - 1.0f, axis_y);
			int32_t iz0 = index_floor((z_low - input.near_plane) / depth_span * static_cast<float>(axis_z), axis_z);
			int32_t iz1 = index_floor(std::ceil((z_high - input.near_plane) / depth_span * static_cast<float>(axis_z)) - 1.0f, axis_z);
			//逐簇追加光源索引
			for (int32_t iz = iz0; iz <= iz1; ++iz)
				for (int32_t iy = iy0; iy <= iy1; ++iy)
					for (int32_t ix = ix0; ix <= ix1; ++ix)
					{
						uint32_t cluster = (static_cast<uint32_t>(iz) * axis_y + static_cast<uint32_t>(iy)) * axis_x
							+ static_cast<uint32_t>(ix);
						list_out[offsets_io[cluster]++] = light_index;
					}
		}
		return entries;
	}
}
