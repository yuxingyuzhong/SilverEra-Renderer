#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//阶段函数声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"
//glm 数学库（光源空间矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	//帧阶段：泵事件 + 本帧输入锁存与原生 UI 交互

	bool frame_stage_shadow_pass(Renderer_Impl_Holder::Impl* impl, bool spot_shadow_light_valid)
	{
		/*
		⑤b 阴影 pass（第五十二轮；第五十五轮按需重绘）
		以光源视角把「参与投影的几何」渲染进阴影目标（RGBA8 打包深度）；
		该 pass 走独立的离屏提交（推进帧槽），故必须在取主 pass 帧槽之前完成；
		随后等尽在途帧，保证主 pass 采样时阴影图已写完。
		若本帧输入签名与上一帧完全一致则跳过整个 pass（阴影图原样复用），静态场景省一次离屏提交与等待。
		*/
		//本帧阴影 pass 计数清零（第六百四十七轮；重绘路径内重新累计）
		impl->last_shadow_passes = 0;
		impl->last_spot_shadow_passes = 0;
		//本帧阴影提交次数归零（第六百六十五轮；C2 基线）
		impl->last_shadow_submits = 0;
		if ((impl->shadow_enabled || spot_shadow_light_valid) && impl->shadow_pipeline != 0
			&& impl->shadow_target != 0 && !impl->pending_items.empty())
		{
			//本帧参与投影的项数
			std::size_t shadow_item_count = 0;
			for (const auto& item : impl->pending_items)
			{
				//跳过文字等非投影项
				if (item.cast_shadow)
					++shadow_item_count;
			}
			//本帧阴影输入签名（只混参与投影的实例矩阵；第六百四十四轮）
			uint64_t shadow_signature = shadow_signature_build(impl->light_view_proj,
				impl->light_view_proj_far, impl->light_view_proj_far2, &impl->spot_view_proj_tiles[0][0],
				impl->pending_items, impl->pending_instances,
				shadow_item_count, impl->shadow_size, impl->cascade_count);
			//输入与上一帧完全一致且阴影图可用 → 跳过重绘
			if (shadow_item_count != 0 && impl->shadow_pass_ready && impl->shadow_signature == shadow_signature)
			{
				//阴影图复用，主 pass 直接采样
				++impl->shadow_reuses;
				impl->shadow_last_redrawn = 0;
			}
			else if (shadow_item_count != 0)
			{
				//阴影 pass 使用的帧槽（render_to_target 内部同样推进帧计数）
				uint32_t shadow_slot = impl->frame.slot_index_get();
				//等待该槽上一轮提交完成，再写实例缓冲
				if (!impl->frame.slot_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待帧槽失败");
					return false;
				}
				//写入该槽实例缓冲（与主 pass 使用同一批模型矩阵）
				if (!impl->pending_instances.empty())
				{
					VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[shadow_slot],
						impl->instance_allocations[shadow_slot], impl->pending_instances.data(), instance_bytes, 0))
					{
						logger.error("渲染器:: 阴影 pass 实例缓冲写入失败");
						return false;
					}
					/*
					使该帧槽的实例缓存失效（第六十一轮）
					阴影 pass 直接整块写入该槽缓冲，绕过了按需写入的缓存；
					主 pass 之后若复用该槽，须以完整写入重建缓存，避免按旧缓存跳过必要更新。
					*/
					if (impl->instance_block_cached.size() > shadow_slot)
						impl->instance_block_cached[shadow_slot] = 0;
					//累计阴影 pass 的实例上传字节
					impl->instance_upload_bytes += static_cast<uint64_t>(instance_bytes);
				}
				//组装阴影绘制项（仅投影几何；改用阴影管线并以推送常量下发光源空间矩阵）
				//阴影列表复用缓冲（第一百八十二轮；避免逐帧新建向量）
				std::vector<Frame_Render_Item>& shadow_items = impl->shadow_items_scratch;
				shadow_items.clear();
				shadow_items.reserve(shadow_item_count);
				VkPipeline shadow_pipeline = impl->pipeline.pipeline_by_handle_get(impl->shadow_pipeline);
				VkPipelineLayout shadow_layout = impl->pipeline.layout_get();
				for (const auto& item : impl->pending_items)
				{
					//文字等非投影项跳过
					if (!item.cast_shadow)
						continue;
					Frame_Render_Item copy = item;
					copy.pipeline = shadow_pipeline;
					copy.layout = shadow_layout;
					/*
					实例缓冲指向阴影 pass 刚写入的槽
					pending_items 中的 instance_buffer 恒为空（主 pass 稍后才填装），
					若此处不指定，顶点属性 binding 1（实例模型矩阵）无缓冲，实例矩阵退化会让
					所有几何塌到原点、阴影图只剩一点，表现为整幅画面全遮蔽。
					*/
					copy.instance_buffer = impl->instance_buffers[shadow_slot];
					copy.scene_set = VK_NULL_HANDLE;
					copy.sampler_set = VK_NULL_HANDLE;
					copy.descriptor_set_count = 0;
					copy.push_constants = impl->light_view_proj;
					copy.push_constant_size = 64;
					copy.push_constant_offset = 0;
					shadow_items.push_back(copy);
				}
				//阴影目标附件
				Frame_Render_Target shadow_target{};
				if (!impl->render_target.target_get(impl->shadow_target, shadow_target.color_image,
					shadow_target.color_view, shadow_target.depth_view, shadow_target.extent))
				{
					logger.error("渲染器:: 阴影目标附件查询失败");
					return false;
				}
				/*
				阴影目标格式（第二百一十轮）
				并行录制需在次级命令缓冲的继承信息里声明附件的颜色 / 深度格式与采样数，
				故此处补齐（阴影颜色恒为 RGBA8 UNORM，深度取交换链深度格式，恒单采样）。
				*/
				shadow_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
				shadow_target.depth_format = impl->swapchain.depth_format_get();
				shadow_target.sample_count = 1;
				//阴影图清空为全 0（打包深度 0 = 最远）
				const float shadow_clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				/*
				按级联分片绘制（第一百二十一轮）
				单级：整幅目标一次绘制，行为与引入级联前逐字节一致；
				双级：目标沿 X 切成左右两片，各占「边长的一半」，第 1 片先清屏整幅、第 2 片以「保留」写入。
				*/
				uint32_t cascade_total = impl->cascade_count == 3 ? 3u : (impl->cascade_count == 2 ? 2u : 1u);
				uint32_t tile_size = cascade_total > 1 ? (impl->shadow_size / cascade_total) : impl->shadow_size;
				/*
				阴影列表合批（第一百二十九轮；第二百一十七轮移到分片循环外）
				各分片的投影项完全相同、仅推送常量（光源矩阵）不同，而合批判据只比较项内静态字段
				与推送常量的**内容**；此处各类项尚未改写的推送常量完全一致，故合批结果与逐片合批等价，
				单级 / 多级级联下都只需合并一次，省去重复扫描。
				*/
				if (impl->static_batching)
					impl->frame_merged_items += batch_adjacent_items(shadow_items, impl->batch_scratch);
				//方向光阴影关闭（仅聚光阴影）时跳过级联 pass，保留方向光图原样（第四百八十四轮）
				const uint32_t directional_cascade_total = impl->shadow_enabled ? cascade_total : 0u;
				/*
				批量提交规划（第六百六十九轮；C2）
				先统计本帧阴影段数（级联 ＋ 有效聚光瓦片）；开启且段数 > 1 时收集目标 / 矩阵 / 清屏标志，
				循环内只填表不提交，循环后一次批量提交；否则走逐 pass 旧路径。
				*/
				uint32_t spot_tile_total = 0;
				//图集实际生效时瓦片是否可渲染不再取决于独立目标是否存在
				const bool spot_atlas_active = impl->spot_shadow_atlas && impl->spot_shadow_atlas_target != 0;
				if (spot_shadow_light_valid)
				{
					for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
						if (impl->spot_shadow_spans[tile] > 0.0f
							&& (spot_atlas_active || impl->spot_shadow_targets[tile] != 0))
							++spot_tile_total;
				}
				const bool batch_path = impl->shadow_batch_submit
					&& (directional_cascade_total + spot_tile_total) > 1;
				//批量段表（级联上限 3 ＋ 聚光瓦片 8）
				Frame_Render_Target batch_targets[3 + Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = {};
				const float* batch_matrices[3 + Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = {};
				bool batch_clear[3 + Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = {};
				uint32_t batch_count = 0;
				for (uint32_t cascade = 0; cascade < directional_cascade_total; ++cascade)
				{
					//本片目标（多级时收缩视口到对应方形小格；X 按片序偏移、Y 恒为顶格）
					Frame_Render_Target cascade_target = shadow_target;
					if (cascade_total > 1)
					{
						uint32_t tile_origin = cascade * tile_size;
						cascade_target.viewport_rect.offset = { static_cast<int32_t>(tile_origin), 0 };
						cascade_target.viewport_rect.extent = { tile_size, tile_size };
					}
					//本片使用的光源空间矩阵
					const float* cascade_matrix = cascade == 0 ? impl->light_view_proj
						: (cascade == 1 ? impl->light_view_proj_far : impl->light_view_proj_far2);
					//第 1 片负责清屏，后续片保留已有内容
					const float* cascade_clear = cascade == 0 ? shadow_clear : nullptr;
					if (batch_path)
					{
						batch_targets[batch_count] = cascade_target;
						batch_matrices[batch_count] = cascade_matrix;
						batch_clear[batch_count] = cascade == 0;
						++batch_count;
					}
					else
					{
						for (auto& shadow_item : shadow_items)
							shadow_item.push_constants = cascade_matrix;
						if (!impl->frame.render_to_target(cascade_target, shadow_items.data(),
							static_cast<uint32_t>(shadow_items.size()), cascade_clear, nullptr, nullptr, true))
						{
							logger.error("渲染器:: 阴影 pass 提交失败（级联 {}）", cascade);
							return false;
						}
						//累计本帧阴影提交次数（第六百六十五轮；现口径一片一次）
						++impl->last_shadow_submits;
					}
				}
				/*
				聚光灯阴影 pass（第四百八十四轮；第五百零七轮扩为多瓦片）
				方向光级联之外，按有效瓦片逐张追加：默认写入独立的 spot_shadow_targets[tile]，单级整幅（无分片）；
				图集开启时改为写入同一张 4×2 图集的对应子视口（第六百七十轮）。
				复用同一阴影管线与实例缓冲，推送常量换成该瓦片的矩阵；首张清屏，其后保留已有内容。
				*/
				//聚光灯阴影 pass 计数（第六百四十七轮）
				uint32_t spot_passes = 0;
				/*
				图集目标（第六百七十轮；C1）
				实际生效时所有瓦片共用一张 4S × 2S 目标，按瓦片号收缩视口到对应小格；
				首瓦片清整张图（颜色 ＋ 深度），其余瓦片保留。
				*/
				Frame_Render_Target atlas_target{};
				if (spot_atlas_active)
				{
					if (!impl->render_target.target_get(impl->spot_shadow_atlas_target, atlas_target.color_image,
						atlas_target.color_view, atlas_target.depth_view, atlas_target.extent))
					{
						logger.error("渲染器:: 聚光阴影图集附件查询失败");
						return false;
					}
					atlas_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
					atlas_target.depth_format = impl->swapchain.depth_format_get();
					atlas_target.sample_count = 1;
				}
				if (spot_shadow_light_valid)
				{
					for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
					{
						//该瓦片无有效聚光灯则跳过
						if (impl->spot_shadow_spans[tile] <= 0.0f)
							continue;
						//图集未生效且该瓦片目标缺失则跳过
						if (!spot_atlas_active && impl->spot_shadow_targets[tile] == 0)
							continue;
						Frame_Render_Target spot_target{};
						if (spot_atlas_active)
						{
							//复用图集目标并收缩到本瓦片小格（渲染区同步收缩，逐片独立清屏）
							spot_target = atlas_target;
							spot_target.viewport_rect.offset = { static_cast<int32_t>((tile % 4) * impl->shadow_size),
								static_cast<int32_t>((tile / 4) * impl->shadow_size) };
							spot_target.viewport_rect.extent = { impl->shadow_size, impl->shadow_size };
							spot_target.render_area_from_viewport = true;
						}
						else if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_target.color_image,
							spot_target.color_view, spot_target.depth_view, spot_target.extent))
						{
							logger.error("渲染器:: 聚光灯阴影目标附件查询失败（瓦片 {}）", tile);
							return false;
						}
						else
						{
							spot_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
							spot_target.depth_format = impl->swapchain.depth_format_get();
							spot_target.sample_count = 1;
						}
						//本段清屏标志（图集逐瓦片独立清屏；独立图恒清）
						const float* spot_clear = shadow_clear;
						if (batch_path)
						{
							batch_targets[batch_count] = spot_target;
							batch_matrices[batch_count] = impl->spot_view_proj_tiles[tile];
							batch_clear[batch_count] = spot_clear != nullptr;
							++batch_count;
						}
						else
						{
							for (auto& shadow_item : shadow_items)
								shadow_item.push_constants = impl->spot_view_proj_tiles[tile];
							if (!impl->frame.render_to_target(spot_target, shadow_items.data(),
								static_cast<uint32_t>(shadow_items.size()), spot_clear, nullptr, nullptr, true))
							{
								logger.error("渲染器:: 聚光灯阴影 pass 提交失败（瓦片 {}）", tile);
								return false;
							}
							//累计本帧阴影提交次数（第六百六十五轮；现口径一瓦片一次）
							++impl->last_shadow_submits;
						}
						//图集段已逐片清屏（无需首段整图清屏标记）
						++spot_passes;
					}
				}
				/*
				批量提交（第六百六十九轮；C2）
				全部段已填表后一次提交；成功即记为 1 次提交，失败按硬错误返回（调用方回落旧路径由上帧开关决定）。
				*/
				if (batch_path)
				{
					if (!impl->frame.render_to_targets_batch(batch_targets, batch_matrices, batch_count,
						shadow_items.data(), static_cast<uint32_t>(shadow_items.size()), shadow_clear, batch_clear, true))
					{
						logger.error("渲染器:: 阴影批量提交失败（{} 段）", batch_count);
						return false;
					}
					impl->last_shadow_submits = 1;
				}
				//等尽在途帧：确保阴影图写入完成后再由主 pass 采样
				if (!impl->frame.frames_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待完成失败");
					return false;
				}
				//记录本帧阴影 pass 数（方向光级联 ＋ 聚光瓦片；第六百四十七轮）
				impl->last_shadow_passes = directional_cascade_total + spot_passes;
				impl->last_spot_shadow_passes = spot_passes;
				//记录阴影 pass 的并行段数（最后一次分片录制的结果；第二百一十二轮）
				impl->last_shadow_parallel_segments = impl->frame.last_parallel_segments_get();
				//读回阴影 pass 的 GPU 耗时（第一百二十八轮；此时该提交已完成）
				impl->shadow_gpu_ms = impl->frame.shadow_gpu_ms_read();
				//记录本帧签名并标记阴影图可复用
				impl->shadow_signature = shadow_signature;
				impl->shadow_pass_ready = true;
				++impl->shadow_pass_count;
				//重绘观测（第一百四十五轮）
				++impl->shadow_redraws;
				impl->last_shadow_items = static_cast<uint32_t>(shadow_item_count);
				impl->shadow_last_redrawn = 1;
			}
		}
		return true;
	}
}
