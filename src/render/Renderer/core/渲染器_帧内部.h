#pragma once
#include "渲染器内部状态.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"

namespace engine
{
		/*
		相邻等价绘制项合批（第一百二十九轮；主 pass 与阴影 pass 共用）
		合并条件：管线 / 布局 / 顶点与索引缓冲 / 索引区 / 推送常量逐字节 / 后处理参数 /
		实例缓冲相同且实例区首尾相接 / 阴影标记 / 采样数全等。
		只累加实例数、不重排顺序，故绘制结果逐字节不变。
		返回被合并掉的项数（第一百三十五轮）；scratch 为复用缓冲（第一百三十六轮，避免逐帧分配）。
		*/
	inline std::size_t batch_adjacent_items(std::vector<Frame_Render_Item>& items, std::vector<Frame_Render_Item>& scratch)
		{
			//不足两项无需合并
			if (items.size() < 2)
				return 0;
			//复用外部缓冲作为合并结果存储（保留首个等价项并累加实例数）
			scratch.clear();
			scratch.reserve(items.size());
			for (const Frame_Render_Item& item : items)
			{
				//能否并入前一项
				bool can_merge = false;
				//若已有可比较的前一项
				if (!scratch.empty())
				{
					const Frame_Render_Item& last = scratch.back();
					//推送常量是否逐字节一致（0 字节表示两侧都不推送）
					bool push_same = last.push_constant_size == item.push_constant_size
						&& last.push_constant_offset == item.push_constant_offset;
					if (push_same && last.push_constant_size != 0)
						push_same = last.push_constants != nullptr && item.push_constants != nullptr
							&& memcmp(last.push_constants, item.push_constants, last.push_constant_size) == 0;
					//后处理参数是否一致（场景项恒相等）
					bool post_same = memcmp(last.post_params, item.post_params, sizeof(last.post_params)) == 0;
					//描边色是否一致（第一百八十一轮；场景项恒全零）
					if (post_same)
						post_same = memcmp(last.post_color, item.post_color, sizeof(last.post_color)) == 0;
					//逐级额外四参是否一致（第四百零一轮；场景项恒全零）
					if (post_same)
						post_same = memcmp(last.post_extra, item.post_extra, sizeof(last.post_extra)) == 0;
					//实例区是否首尾相接
					bool instance_adjacent = last.instance_buffer == item.instance_buffer
						&& last.instance_offset + static_cast<VkDeviceSize>(last.instance_count) * INSTANCE_STRIDE_BYTES
							== item.instance_offset;
					can_merge = push_same && post_same && instance_adjacent
						&& last.instance_input && item.instance_input
						&& !last.transparent && !item.transparent
						&& last.pipeline == item.pipeline
						&& last.layout == item.layout
						&& last.vertex_buffer == item.vertex_buffer
						&& last.index_buffer == item.index_buffer
						&& last.index_offset == item.index_offset
						&& last.index_count == item.index_count
						&& last.cast_shadow == item.cast_shadow
						&& last.sample_count == item.sample_count
						/*
						采样状态须一致（第一百七十四轮）
						合批会让多个实例共用一条采样器集，纹理不同则不能合并。
						*/
						&& last.draw_sampler == item.draw_sampler
						&& last.draw_albedo == item.draw_albedo
						&& last.draw_normal == item.draw_normal
						&& last.draw_roughness == item.draw_roughness
						&& last.draw_emissive == item.draw_emissive
						&& last.draw_ao == item.draw_ao
						&& last.draw_metallic == item.draw_metallic;
				}
				//可合并则把实例数并入前一项（实例数据仍按原顺序连续）
				if (can_merge)
					scratch.back().instance_count += item.instance_count;
				else
					scratch.push_back(item);
			}
			//合并前项数
			std::size_t original_size = items.size();
			items.swap(scratch);
			//返回被合并掉的项数（原项数 − 合并后项数）
			return original_size > items.size() ? original_size - items.size() : 0;
		}

		/*
		阴影输入签名（第五十五轮；第一百二十一轮加级联信息；第六百四十四轮只混参与投影的实例）
		把光源空间矩阵（含远区级联矩阵与聚光瓦片）、**仅参与投影的绘制项实例矩阵**、投影项数、
		阴影图边长与级联数混入 64 位 FNV-1a；用于判定阴影图能否跨帧复用（完全一致才跳过重绘）。
		非投影项（文字 / 粒子等）的实例变化不再使签名变化，避免无谓的整张阴影重绘。
		*/
	inline uint64_t shadow_signature_build(const float light_view_proj[16], const float light_view_proj_far[16],
			const float light_view_proj_far2[16], const float spot_view_proj_tiles[128],
			const std::vector<Frame_Render_Item>& items,
			const std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& instances,
			std::size_t shadow_item_count, uint32_t shadow_size, uint32_t cascade_count)
		{
			//FNV-1a 偏移基
			uint64_t hash = 1469598103934665603ull;
			//逐 float 混入位模式
			auto mix_float = [&hash](float value)
			{
				uint32_t bits = 0;
				std::memcpy(&bits, &value, sizeof(bits));
				hash ^= static_cast<uint64_t>(bits);
				hash *= 1099511628211ull;
			};
			//光源空间矩阵（近区）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj[index]);
			//远区级联矩阵（单级时为零矩阵，不影响既有签名分布）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj_far[index]);
			//第三级级联矩阵（第一百四十六轮；非三级时为零矩阵）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj_far2[index]);
			//聚光灯阴影矩阵（第四百八十四轮；第五百零七轮扩为 4 瓦片 64 浮点；第五百一十三轮扩为 8 瓦片 128 浮点）
			for (std::size_t index = 0; index < 128; ++index)
				mix_float(spot_view_proj_tiles[index]);
			/*
			实例矩阵（每实例 INSTANCE_FLOAT_COUNT 个 float）
			只遍历参与投影的绘制项，并按 instance_offset 定位其连续实例区间；
			越界（防御性）即停止该区间的混入，不影响已混内容。
			*/
			for (const Frame_Render_Item& item : items)
			{
				//非投影项跳过
				if (!item.cast_shadow)
					continue;
				//该项首个实例下标与其实例数
				const std::size_t first = static_cast<std::size_t>(item.instance_offset / INSTANCE_STRIDE_BYTES);
				const std::size_t count = static_cast<std::size_t>(item.instance_count);
				for (std::size_t instance = 0; instance < count; ++instance)
				{
					//实例下标越界则停止
					const std::size_t base = first + instance;
					if (base >= instances.size())
						break;
					//逐分量混入该实例矩阵
					for (std::size_t index = 0; index < INSTANCE_FLOAT_COUNT; ++index)
						mix_float(instances[base][index]);
				}
			}
			//投影项数与阴影图边长
			hash ^= static_cast<uint64_t>(shadow_item_count);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(shadow_size);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(cascade_count);
			hash *= 1099511628211ull;
			return hash;
		}
}
