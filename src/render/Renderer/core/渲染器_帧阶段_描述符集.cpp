#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	void frame_stage_descriptor_sets(Renderer_Impl_Holder::Impl* impl, uint32_t slot)
	{
		/*
		为本帧待绘制项填装描述符集与实例缓冲句柄
		逐绘制项采样集（第一百七十四轮）：每项按登记时的纹理快照写一条独立 set1，
		使同帧内不同物体（含文字图集）各自采样自己的纹理；超出预留条数的项回落共享集。
		*/
		{
			//默认采样资源（回落用）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
			//阴影图视图（未建立阴影目标时以默认白纹理占位）
			VkImageView shadow_view = default_view;
			if (impl->shadow_target != 0)
			{
				VkImage shadow_image = VK_NULL_HANDLE;
				VkImageView shadow_depth_view = VK_NULL_HANDLE;
				VkExtent2D shadow_extent{};
				impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view,
					shadow_depth_view, shadow_extent);
			}
			//聚光灯阴影图视图（第四百八十三轮；第五百零七轮扩为 4 张；未建立时以默认白纹理占位）
			VkImageView spot_shadow_views[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = { default_view, default_view,
				default_view, default_view, default_view, default_view, default_view, default_view };
			/*
			图集（第六百七十轮；C1）
			实际生效时 8 个数组元素全部写入同一张图集视图，瓦片定位由图集内像素原点完成；
			否则沿用逐瓦片视图。
			*/
			if (impl->spot_shadow_atlas && impl->spot_shadow_atlas_target != 0)
			{
				VkImage atlas_image = VK_NULL_HANDLE;
				VkImageView atlas_view = VK_NULL_HANDLE;
				VkImageView atlas_depth_view = VK_NULL_HANDLE;
				VkExtent2D atlas_extent{};
				if (impl->render_target.target_get(impl->spot_shadow_atlas_target, atlas_image, atlas_view,
					atlas_depth_view, atlas_extent))
					for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
						spot_shadow_views[tile] = atlas_view;
			}
			else
			{
				for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
				{
					//该瓦片未建立则保持默认白纹理
					if (impl->spot_shadow_targets[tile] == 0)
						continue;
					VkImage spot_shadow_image = VK_NULL_HANDLE;
					VkImageView spot_shadow_depth_view = VK_NULL_HANDLE;
					VkExtent2D spot_shadow_extent{};
					impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_shadow_image,
						spot_shadow_views[tile], spot_shadow_depth_view, spot_shadow_extent);
				}
			}
			//天空盒立方体视图（未设置时用内置纯白立方体）
			VkImageView sky_view = VK_NULL_HANDLE;
			VkSampler sky_sampler = VK_NULL_HANDLE;
			const uint64_t sky_texture = impl->sky_texture != 0 ? impl->sky_texture : impl->default_sky_texture;
			impl->texture.texture_bind_get(sky_texture, sky_view, sky_sampler);
			//写逐绘制项集前等尽在途帧（这些集会被下一轮同槽帧复用）
			impl->frame.frames_wait();
			//本帧逐绘制项集游标
			uint32_t draw_index = 0;
			/*
			第一遍：填装公共字段并收集采样快照（第二百二十一轮：批量写入）
			条件与逐项写入完全一致（有采样快照且预留未用尽），只是把「写哪一条集」延后到一次批量提交。
			*/
			impl->draw_set_inputs.clear();
			for (auto& item : impl->pending_items)
			{
				//本帧槽的实例缓冲（模型矩阵来源；不消费实例属性的项留空，第三百四十一轮）
				item.instance_buffer = item.instance_input ? impl->instance_buffers[slot] : VK_NULL_HANDLE;
				//本帧槽的场景集（set0）：粒子专用项取粒子集，其余取场景集
				item.scene_set = item.particle_scene_set
					? impl->pipeline.particle_set_get(slot) : impl->pipeline.scene_set_get(slot);
				//默认回落共享采样器集（set1）
				item.sampler_set = impl->pipeline.sampler_set_get(slot);
				item.descriptor_set_count = 2;
				//若该项有采样快照且预留未用尽则收集待写快照
				if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE
					&& draw_index < Pipeline_Context::DRAW_SET_PER_SLOT)
				{
					Pipeline_Context::Draw_Set_Views views{};
					views.sampler = item.draw_sampler;
					views.albedo = item.draw_albedo;
					views.normal = item.draw_normal != VK_NULL_HANDLE ? item.draw_normal : default_view;
					views.roughness = item.draw_roughness != VK_NULL_HANDLE ? item.draw_roughness : default_view;
					views.emissive = item.draw_emissive != VK_NULL_HANDLE ? item.draw_emissive : default_view;
					views.ao = item.draw_ao != VK_NULL_HANDLE ? item.draw_ao : default_view;
					views.metallic = item.draw_metallic != VK_NULL_HANDLE ? item.draw_metallic : default_view;
					views.shadow = shadow_view != VK_NULL_HANDLE ? shadow_view : default_view;
					views.sky = sky_view != VK_NULL_HANDLE ? sky_view : default_view;
					//聚光灯阴影图（第四百八十三轮；binding 9；第五百一十三轮写入全部 8 个数组元素）
					for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
						views.spot_shadow[tile] = spot_shadow_views[tile];
					//逐项场景深度（粒子软粒子：HiZ 历史视图；未提供时不写，回落反照率占位）
					views.depth = item.draw_depth;
					views.depth_sampler = item.draw_depth_sampler;
					impl->draw_set_inputs.push_back(views);
					++draw_index;
				}
			}
			//一次写入本帧全部逐绘制项采样集
			const uint32_t draw_set_written = static_cast<uint32_t>(impl->draw_set_inputs.size());
			if (draw_set_written > 0)
				impl->pipeline.draw_sets_bind_batch(slot, impl->draw_set_inputs.data(), draw_set_written);
			/*
			第二遍：回填专属采样集句柄
			收集顺序与逐项写入一致（同一批集下标），故只有前 draw_set_written 项使用专属集，其余回落共享集。
			*/
			uint32_t assign_cursor = 0;
			for (auto& item : impl->pending_items)
			{
				if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE
					&& assign_cursor < draw_set_written)
				{
					item.sampler_set = impl->pipeline.draw_set_get(slot, assign_cursor);
					++assign_cursor;
				}
			}
		}
	}

	//帧阶段：实例模型矩阵写入本帧槽实例缓冲（返回假表示本帧失败）
}
