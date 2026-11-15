#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "渲染器内部工具.h"

namespace engine
{
	//设置距离雾（第三十八轮新增）

	void Render_Scene::shadow_enable_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		/*
	首次开启时懒登记阴影管线
	未启用阴影的进程不承担管线创建开销；登记失败则保持关闭并告警（不改变其余状态）。
	阴影目标为 R8G8B8A8_UNORM（见 init ⑳ ①），故该管线必须按同一颜色格式创建，
	不能经公开 pipeline_load（它固定跟随交换链格式，sRGB 会破坏打包深度）。
	*/
	if (enabled && owner->impl->shadow_pipeline == 0)
	{
		//加载阴影着色器模块（顺带解析顶点输入位置）
		VkShaderModule shadow_vert = VK_NULL_HANDLE;
		VkShaderModule shadow_frag = VK_NULL_HANDLE;
		uint32_t shadow_input_locations = 0;
		if (!owner->impl->shader.module_load(shadow_vert, "阴影.vert.spv", &shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：顶点着色器加载失败，阴影保持关闭");
			state().shadow_enabled = false;
			return;
		}
		if (!owner->impl->shader.module_load(shadow_frag, "阴影.frag.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：片元着色器加载失败，阴影保持关闭");
			owner->impl->shader.module_destroy(shadow_vert);
			state().shadow_enabled = false;
			return;
		}
		//按阴影目标的 UNORM 颜色格式登记具名管线
		uint64_t shadow_pipeline = 0;
		//阴影目标固定单采样（第九十六轮），故该管线采样数固定 1，不跟随 MSAA
		if (!owner->impl->pipeline.pipeline_register(shadow_pipeline, shadow_vert, shadow_frag,
			Blend_Mode::Opaque, VK_FORMAT_R8G8B8A8_UNORM, true, 1, shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：图形管线创建失败，阴影保持关闭");
			owner->impl->shader.module_destroy(shadow_vert);
			owner->impl->shader.module_destroy(shadow_frag);
			state().shadow_enabled = false;
			return;
		}
		//记录具名管线（着色器模块所有权在门面，随 pipeline_unload / shutdown 回收）
		Renderer_Impl_Holder::Impl::Extra_Pipeline_Record record{};
		record.handle = shadow_pipeline;
		record.vert_module = shadow_vert;
		record.frag_module = shadow_frag;
		owner->impl->extra_pipelines.push_back(record);
		owner->impl->shadow_pipeline = shadow_pipeline;
	}
	state().shadow_enabled = enabled;
	}

	bool Render_Scene::shadow_enabled(void) const
	{
		//未初始化一律按关闭处理
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized && state().shadow_enabled;
	}

	void Render_Scene::shadow_bias_set(float bias)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//偏置须落在 0..0.05（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f))
		{
			logger.warn("渲染器:: 阴影深度偏置非法（{}），整次忽略", bias);
			return;
		}
		state().shadow_bias = bias;
	}

	float Render_Scene::shadow_bias(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().shadow_bias : 0.003f;
	}

	void Render_Scene::shadow_softness_set(float softness)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//软度须落在 0..1（取反比较使 NaN 判非法）
		if (!(softness >= 0.0f && softness <= 1.0f))
		{
			logger.warn("渲染器:: 阴影软度非法（{}），整次忽略", softness);
			return;
		}
		state().shadow_softness = softness;
	}

	float Render_Scene::shadow_softness(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().shadow_softness : 0.0f;
	}

	void Render_Scene::shadow_strength_set(float strength)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//强度须落在 0..1（取反比较使 NaN 判非法）
		if (!(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 阴影强度非法（{}），整次忽略", strength);
			return;
		}
		state().shadow_strength = strength;
	}

	float Render_Scene::shadow_strength(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().shadow_strength : 1.0f;
	}

	void Render_Scene::shadow_pcss_set(bool enabled, float search, float max_radius)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//非有限值整次忽略
		if (!std::isfinite(search) || !std::isfinite(max_radius))
			return;
		//搜索尺度与 PCF 半径钳到有效区间
		state().shadow_pcss_search = search < 1.0f ? 1.0f : (search > 32.0f ? 32.0f : search);
		state().shadow_pcss_max = max_radius < 1.0f ? 1.0f : (max_radius > 16.0f ? 16.0f : max_radius);
		state().shadow_pcss_enabled = enabled;
	}

	void Render_Scene::shadow_budget_set(uint64_t bytes)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//存到配置副本（scene_apply 直接读 Light_Data，此处只为门面等价入口）
		state().shadow_budget_bytes = bytes > 1073741824ull ? 1073741824ull : bytes;
	}

	void Render_Scene::shadow_resolution_set(uint32_t resolution)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//仅接受三档边长
		if (resolution != 512 && resolution != 1024 && resolution != 2048)
		{
			logger.warn("渲染器:: 阴影分辨率非法（{}），整次忽略（仅支持 512 / 1024 / 2048）", resolution);
			return;
		}
		//与原值一致则无需重建
		if (resolution == state().shadow_size)
			return;
		//等尽在途帧，避免重建期间仍被采样
		if (!owner->impl->frame.frames_wait())
		{
			logger.error("渲染器:: 阴影分辨率切换失败：等待在途帧失败");
			return;
		}
		//旧阴影图视图（失败回绑用）
		VkImage old_image = VK_NULL_HANDLE;
		VkImageView old_view = VK_NULL_HANDLE;
		VkImageView old_depth_view = VK_NULL_HANDLE;
		VkExtent2D old_extent{};
		if (!owner->resource().state().render_target.target_get(owner->impl->shadow_target, old_image, old_view,
			old_depth_view, old_extent))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：旧阴影图查询失败");
			return;
		}
		//旧聚光灯阴影图视图（第四百八十七轮；第五百零七轮扩为 4 张；失败回绑用）
		VkImageView old_spot_views[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
			VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未建立则跳过
			if (owner->impl->spot_shadow_targets[tile] == 0)
				continue;
			VkImage old_spot_image = VK_NULL_HANDLE;
			VkImageView old_spot_depth_view = VK_NULL_HANDLE;
			VkExtent2D old_spot_extent{};
			if (!owner->resource().state().render_target.target_get(owner->impl->spot_shadow_targets[tile], old_spot_image, old_spot_views[tile],
				old_spot_depth_view, old_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：旧聚光灯阴影图查询失败（瓦片 {}）", tile);
				return;
			}
		}
		//先建新目标（失败则旧目标原样保留）；颜色格式固定 UNORM，理由见 init ⑳ ①
		uint64_t candidate = 0;
		//阴影目标固定单采样（第九十六轮）
		if (!owner->resource().state().render_target.target_create(candidate, resolution, resolution,
			VK_FORMAT_R8G8B8A8_UNORM, owner->impl->swapchain.depth_format_get(), 1))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影目标创建失败（保持 {}）", state().shadow_size);
			return;
		}
		//聚光灯阴影目标同分辨率重建（第四百八十七轮；第五百零七轮扩为 4 张）
		uint64_t candidate_spot_tiles[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = {};
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未建立则跳过
			if (owner->impl->spot_shadow_targets[tile] == 0)
				continue;
			if (!owner->resource().state().render_target.target_create(candidate_spot_tiles[tile], resolution, resolution,
				VK_FORMAT_R8G8B8A8_UNORM, owner->impl->swapchain.depth_format_get(), 1))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影目标创建失败（瓦片 {}）", tile);
				owner->resource().state().render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < tile; ++created)
					if (candidate_spot_tiles[created] != 0)
						owner->resource().state().render_target.target_destroy(candidate_spot_tiles[created]);
				return;
			}
		}
		//取新目标颜色视图与默认采样器
		VkImage new_image = VK_NULL_HANDLE;
		VkImageView new_view = VK_NULL_HANDLE;
		VkImageView new_depth_view = VK_NULL_HANDLE;
		VkExtent2D new_extent{};
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!owner->resource().state().render_target.target_get(candidate, new_image, new_view, new_depth_view, new_extent)
			|| !owner->resource().state().texture.texture_bind_get(owner->resource().state().default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影图查询失败");
			owner->resource().state().render_target.target_destroy(candidate);
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
				if (candidate_spot_tiles[tile] != 0)
					owner->resource().state().render_target.target_destroy(candidate_spot_tiles[tile]);
			return;
		}
		//新聚光灯阴影图颜色视图（第四百八十七轮；第五百零七轮扩为 4 张）
		VkImageView new_spot_views[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = { default_view, default_view, default_view,
			default_view, default_view, default_view, default_view, default_view };
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未重建则沿用默认白纹理占位
			if (candidate_spot_tiles[tile] == 0)
				continue;
			VkImage new_spot_image = VK_NULL_HANDLE;
			VkImageView new_spot_depth_view = VK_NULL_HANDLE;
			VkExtent2D new_spot_extent{};
			if (!owner->resource().state().render_target.target_get(candidate_spot_tiles[tile], new_spot_image, new_spot_views[tile],
				new_spot_depth_view, new_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影图查询失败（瓦片 {}）", tile);
				owner->resource().state().render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++created)
					if (candidate_spot_tiles[created] != 0)
						owner->resource().state().render_target.target_destroy(candidate_spot_tiles[created]);
				return;
			}
		}
		//逐帧槽改绑新阴影图
		for (std::size_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
		{
			//若某槽绑定失败，则把已改绑的槽全部回绑旧图并放弃新目标
			if (!owner->impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, new_view))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 绑定失败，回绑旧阴影图", slot);
				for (std::size_t revert = 0; revert <= slot; ++revert)
					owner->impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
				owner->resource().state().render_target.target_destroy(candidate);
				for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
					if (candidate_spot_tiles[tile] != 0)
						owner->resource().state().render_target.target_destroy(candidate_spot_tiles[tile]);
				return;
			}
		}
		//逐帧槽改绑新聚光灯阴影图（第四百八十七轮；第五百零七轮四瓦片）
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未重建则无需改绑
			if (candidate_spot_tiles[tile] == 0)
				continue;
			for (std::size_t slot = 0; slot < owner->impl->config.frame_overlap; ++slot)
			{
				//任一槽失败：方向光与全部聚光瓦片绑定一并回绑旧图
				if (!owner->impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, new_spot_views[tile],
					static_cast<uint32_t>(tile)))
				{
					logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 聚光灯阴影绑定失败（瓦片 {}），回绑旧图", slot, tile);
					for (std::size_t revert = 0; revert < owner->impl->config.frame_overlap; ++revert)
					{
						owner->impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
						for (std::size_t target_tile = 0; target_tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++target_tile)
						{
							//优先回绑旧视图；旧瓦片不存在时保持新视图
							if (candidate_spot_tiles[target_tile] == 0)
								continue;
							VkImageView revert_view = old_spot_views[target_tile] != VK_NULL_HANDLE
								? old_spot_views[target_tile] : new_spot_views[target_tile];
							owner->impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(revert), sampler, revert_view,
								static_cast<uint32_t>(target_tile));
						}
					}
					owner->resource().state().render_target.target_destroy(candidate);
					for (std::size_t created = 0; created < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++created)
						if (candidate_spot_tiles[created] != 0)
							owner->resource().state().render_target.target_destroy(candidate_spot_tiles[created]);
					return;
				}
			}
		}
		//全部改绑成功后销毁旧目标并更新状态
		owner->resource().state().render_target.target_destroy(owner->impl->shadow_target);
		owner->impl->shadow_target = candidate;
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//未重建的瓦片保持原目标
			if (candidate_spot_tiles[tile] == 0)
				continue;
			owner->resource().state().render_target.target_destroy(owner->impl->spot_shadow_targets[tile]);
			owner->impl->spot_shadow_targets[tile] = candidate_spot_tiles[tile];
		}
		state().shadow_size = resolution;
		/*
		聚光阴影图集按新边长重建（第六百七十轮；仅请求仍有效时）
		重建失败时 apply 内部回落独立瓦片，请求保留以避免逐帧重试。
		*/
		if (state().spot_shadow_atlas_requested)
			spot_shadow_atlas_apply_impl(owner->impl.get());
		//新目标尚无有效内容，下一帧必须重绘阴影图
		owner->impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影图分辨率已切换为 {}", resolution);
	}

	uint32_t Render_Scene::shadow_resolution(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().shadow_size : Renderer_Impl_Holder::Impl::SHADOW_SIZE;
	}

	void Render_Scene::shadow_range_set(float half_extent, float near_plane, float far_plane)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//参数校验：半宽为正、近平面为正、远平面大于近平面（非数一并拒绝）
		if (!(half_extent > 0.0f) || !(near_plane > 0.0f) || !(far_plane > near_plane))
		{
			logger.warn("渲染器:: 阴影视锥参数非法（半宽 {}，近 {}，远 {}），本次忽略",
				half_extent, near_plane, far_plane);
			return;
		}
		state().shadow_half_extent = half_extent;
		state().shadow_near = near_plane;
		state().shadow_far = far_plane;
		//光源空间矩阵随下一帧更新，旧阴影图不再可信
		owner->impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影视锥已设为 半宽 {}，近 {}，远 {}", half_extent, near_plane, far_plane);
	}

	void Render_Scene::shadow_range_get(float* half_extent_out, float* near_out, float* far_out) const
	{
		//默认值与未初始化一致
		float half_extent = 12.0f;
		float near_plane = 1.0f;
		float far_plane = 60.0f;
		//已初始化则取当前值
		if (owner != nullptr && owner->impl != nullptr && owner->impl->initialized)
		{
			half_extent = state().shadow_half_extent;
			near_plane = state().shadow_near;
			far_plane = state().shadow_far;
		}
		if (half_extent_out != nullptr)
			*half_extent_out = half_extent;
		if (near_out != nullptr)
			*near_out = near_plane;
		if (far_out != nullptr)
			*far_out = far_plane;
	}

	void Render_Scene::shadow_cascade_set(uint32_t count, float far_scale, float split_distance)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//参数校验：级联数仅 1 / 2 / 3、远区倍数 > 1、切换距离 > 0（非数一并拒绝）
		if ((count != 1 && count != 2 && count != 3) || !(far_scale > 1.0f) || !(split_distance > 0.0f))
		{
			logger.warn("渲染器:: 级联阴影参数非法（级联数 {}，远区倍数 {}，切换距离 {}），本次忽略",
				count, far_scale, split_distance);
			return;
		}
		state().cascade_count = count;
		state().cascade_far_scale = far_scale;
		state().cascade_split_distance = split_distance;
		//分片方式或范围改变，旧阴影图不再可信
		owner->impl->shadow_pass_ready = false;
		logger.info("渲染器:: 级联阴影已设为 {} 级（远区倍数 {}，切换距离 {}，单片边长 {}）",
			count, far_scale, split_distance,
			count > 1 ? state().shadow_size / count : state().shadow_size);
	}

	void Render_Scene::shadow_cascade_get(uint32_t* count_out, float* far_scale_out, float* split_distance_out) const
	{
		//默认值与未初始化一致
		uint32_t count = 1;
		float far_scale = 3.0f;
		float split_distance = 12.0f;
		//已初始化则取当前值
		if (owner != nullptr && owner->impl != nullptr && owner->impl->initialized)
		{
			count = state().cascade_count;
			far_scale = state().cascade_far_scale;
			split_distance = state().cascade_split_distance;
		}
		if (count_out != nullptr)
			*count_out = count;
		if (far_scale_out != nullptr)
			*far_scale_out = far_scale;
		if (split_distance_out != nullptr)
			*split_distance_out = split_distance;
	}

	void Render_Scene::shadow_cascade_blend_set(float blend)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//比例须在 [0, 0.5]（取反比较使 NaN 判非法）
		if (!(blend >= 0.0f && blend <= 0.5f))
		{
			logger.warn("渲染器:: 级联接缝淡化比例非法（{}），本次忽略", blend);
			return;
		}
		state().cascade_blend = blend;
	}

	float Render_Scene::shadow_cascade_blend(void) const
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0.0f;
		return state().cascade_blend;
	}

	void Render_Scene::shadow_texel_max_set(float texel)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//非有限值或负数非法（取反比较使 NaN 判非法）
		if (!(texel >= 0.0f) || !std::isfinite(texel))
		{
			logger.warn("渲染器:: 阴影纹素上限非法（{}），本次忽略", texel);
			return;
		}
		//上限 64 世界单位 / 纹素（避免配置笔误）
		const float effective = texel > 64.0f ? 64.0f : texel;
		//数值变化才使旧阴影图失效（避免逐帧重建）；日志同样只在变化时输出（避免 scene_apply 逐帧刷屏）
		if (effective != state().shadow_texel_max)
		{
			state().shadow_texel_max = effective;
			owner->impl->shadow_pass_ready = false;
			logger.info("渲染器:: 阴影纹素上限已设为 {}", effective);
		}
	}

	float Render_Scene::shadow_texel_max(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().shadow_texel_max : 0.0f;
	}

	void Render_Scene::shadow_cascade_blend_auto_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().cascade_blend_auto = enabled;
	}

	bool Render_Scene::shadow_cascade_blend_auto(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized && state().cascade_blend_auto;
	}

	void Render_Scene::shadow_batch_submit_set(bool enabled)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().shadow_batch_submit = enabled;
	}

	bool Render_Scene::shadow_batch_submit(void) const
	{
		//未初始化返回默认值
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized && state().shadow_batch_submit;
	}

	uint64_t Render_Scene::shadow_pass_count(void) const
	{
		//未初始化返回 0
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? owner->impl->shadow_pass_count : 0;
	}
}
