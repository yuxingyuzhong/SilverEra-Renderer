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

	void Renderer_Lighting::spot_shadow_params_set(float bias, float softness, float strength)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//三项范围校验（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f) || !(softness >= 0.0f && softness <= 1.0f)
			|| !(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 聚光灯阴影参数非法（{} / {} / {}），整次忽略", bias, softness, strength);
			return;
		}
		impl->spot_shadow_bias = bias;
		impl->spot_shadow_softness = softness;
		impl->spot_shadow_strength = strength;
	}

	void Renderer_Lighting::spot_shadow_params_get(float* bias_out, float* softness_out, float* strength_out) const
	{
		//未初始化按默认值输出
		const bool ready = impl != nullptr && impl->initialized;
		//偏置
		if (bias_out != nullptr)
			*bias_out = ready ? impl->spot_shadow_bias : 0.010f;
		//软度
		if (softness_out != nullptr)
			*softness_out = ready ? impl->spot_shadow_softness : 0.0f;
		//强度
		if (strength_out != nullptr)
			*strength_out = ready ? impl->spot_shadow_strength : 1.0f;
	}

	void Renderer_Lighting::spot_shadow_index_set(int index)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//序号须落在 0..7（取反比较使 NaN 判非法；int 入参无 NaN，仍保留统一写法）
		if (!(index >= 0 && index < static_cast<int>(Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)))
		{
			logger.warn("渲染器:: 聚光灯阴影序号非法（{}），整次忽略", index);
			return;
		}
		impl->spot_shadow_index = index;
	}

	int Renderer_Lighting::spot_shadow_index(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_index : 0;
	}

	void Renderer_Lighting::spot_shadow_all_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//与原值一致则无需处理
		if (impl->spot_shadow_all == enabled)
			return;
		impl->spot_shadow_all = enabled;
		//开启：按槽位 / 压缩口径补齐瓦片（第六百四十四轮；无聚光时至少建瓦片 1 占位）
		if (enabled)
		{
			//所需瓦片数（与帧内口径一致）
			std::size_t tile_limit = 0;
			bool slot_mapping = true;
			std::size_t spot_total = 0;
			for (std::size_t slot = 0; slot < impl->point_light_count; ++slot)
			{
				if (!impl->point_lights[slot].is_spot)
					continue;
				++spot_total;
				if (slot >= Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)
					slot_mapping = false;
				else
					tile_limit = slot + 1;
			}
			if (!slot_mapping)
				tile_limit = spot_total < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES ? spot_total : Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES;
			spot_shadow_tiles_ensure_impl(impl.get(), tile_limit > 0 ? tile_limit : 1u);
		}
		//矩阵与槽位映射随下一帧重算，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 多盏聚光灯阴影已{}", enabled ? "开启" : "关闭");
	}

	bool Renderer_Lighting::spot_shadow_atlas_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//与请求一致则无需处理
		if (impl->spot_shadow_atlas_requested == enabled)
			return impl->spot_shadow_atlas;
		impl->spot_shadow_atlas_requested = enabled;
		//重建图集并重绑
		spot_shadow_atlas_apply_impl(impl.get());
		logger.info("渲染器:: 聚光阴影图集已{}", impl->spot_shadow_atlas ? "开启" : "关闭");
		return impl->spot_shadow_atlas;
	}

	bool Renderer_Lighting::spot_shadow_atlas(void) const
	{
		//未初始化返回假
		return impl != nullptr && impl->initialized ? impl->spot_shadow_atlas : false;
	}

	bool Renderer_Lighting::spot_shadow_light_set(uint32_t index, bool enabled)
	{
		//若尚未初始化或槽位越界
		if (impl == nullptr || !impl->initialized || index >= Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)
			return false;
		//与原值一致则无需重绘
		if (impl->spot_shadow_light_enabled[index] == enabled)
			return true;
		impl->spot_shadow_light_enabled[index] = enabled;
		//旧阴影图不再可信（该槽位可能不再投影或重新投影）
		impl->shadow_pass_ready = false;
		return true;
	}

	bool Renderer_Lighting::spot_shadow_bias_scale_set(uint32_t index, float scale)
	{
		//若尚未初始化或槽位越界
		if (impl == nullptr || !impl->initialized || index >= 8)
			return false;
		//非有限值忽略（保持原值）
		if (!std::isfinite(scale))
			return false;
		impl->spot_shadow_bias_scale[index] = scale < 0.1f ? 0.1f : (scale > 10.0f ? 10.0f : scale);
		return true;
	}

	bool Renderer_Lighting::spot_shadow_quality_set(uint32_t index, float softness_scale, float strength_scale)
	{
		//若尚未初始化或槽位越界
		if (impl == nullptr || !impl->initialized || index >= 8)
			return false;
		//非有限值整次忽略（保持原值）
		if (!std::isfinite(softness_scale) || !std::isfinite(strength_scale))
			return false;
		impl->spot_shadow_softness_scale[index] =
			softness_scale < 0.25f ? 0.25f : (softness_scale > 4.0f ? 4.0f : softness_scale);
		impl->spot_shadow_strength_scale[index] =
			strength_scale < 0.0f ? 0.0f : (strength_scale > 1.0f ? 1.0f : strength_scale);
		return true;
	}

	bool Renderer_Lighting::spot_shadow_priority_set(uint32_t index, float priority)
	{
		//若尚未初始化或槽位越界
		if (impl == nullptr || !impl->initialized || index >= 32)
			return false;
		//非有限值忽略（保持原值）
		if (!std::isfinite(priority))
			return false;
		impl->spot_shadow_priority[index] = priority;
		return true;
	}

	bool Renderer_Lighting::spot_shadow_all(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_all : false;
	}

	void Renderer_Lighting::spot_shadow_bias_auto_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->spot_shadow_bias_auto = enabled;
		//逐瓦片偏置随下一帧重算，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 聚光灯阴影偏置自校准已{}", enabled ? "开启" : "关闭");
	}

	bool Renderer_Lighting::spot_shadow_bias_auto(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_bias_auto : false;
	}

	void Renderer_Lighting::spot_shadow_bias_tiles_get(float out[8]) const
	{
		//输出数组必须有效
		if (out == nullptr)
			return;
		//未初始化写默认偏置
		if (impl == nullptr || !impl->initialized)
		{
			for (std::size_t index = 0; index < 8; ++index)
				out[index] = 0.010f;
			return;
		}
		for (std::size_t index = 0; index < 8; ++index)
			out[index] = impl->spot_shadow_bias_tiles[index];
	}

	/*
	按需创建并绑定多盏聚光灯阴影瓦片（第五百一十一轮；第五百一十三轮扩为 8 瓦片）
	瓦片 0 常驻（与单盏行为一致）；瓦片 1..max_tiles-1 首次需要时创建，失败仅告警并保持「无阴影」占位。
	*/
	void spot_shadow_tiles_ensure_impl(Renderer_Impl_Holder::Impl* impl, std::size_t max_tiles)
	{
		//瓦片数钳到 [1, SPOT_SHADOW_TILES]
		if (max_tiles == 0)
			max_tiles = 1;
		if (max_tiles > Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES)
			max_tiles = Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES;
		/*
		图集生效时独立瓦片不再需要（第七百零七轮；Q-A）
		图集已承担全部瓦片采样与渲染，跳过创建可消除首帧资源分配与 8 份瓦片显存占用；
		图集创建失败（实际未生效）时仍走独立瓦片回落路径。
		*/
		if (impl->spot_shadow_atlas)
			return;
		//默认采样器（与瓦片 0 共用）
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		impl->resource_domain->state().texture.texture_bind_get(impl->resource_domain->state().default_texture, default_view, sampler);
		for (std::size_t tile = 0; tile < max_tiles; ++tile)
		{
			//已建立则跳过
			if (impl->spot_shadow_targets[tile] != 0)
				continue;
			if (!impl->resource_domain->state().render_target.target_create(impl->spot_shadow_targets[tile], impl->shadow_size,
				impl->shadow_size, VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				impl->spot_shadow_targets[tile] = 0;
				logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 创建失败，按无阴影处理", tile);
				continue;
			}
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
			VkImageView depth_view = VK_NULL_HANDLE;
			VkExtent2D extent{};
			if (!impl->resource_domain->state().render_target.target_get(impl->spot_shadow_targets[tile], image, view, depth_view, extent))
			{
				impl->resource_domain->state().render_target.target_destroy(impl->spot_shadow_targets[tile]);
				impl->spot_shadow_targets[tile] = 0;
				logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 查询失败，按无阴影处理", tile);
				continue;
			}
			//图集生效时 binding 9 全部元素仍指向图集，不改绑逐瓦片视图
			if (impl->spot_shadow_atlas)
				continue;
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
				if (!impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, view,
					static_cast<uint32_t>(tile)))
					logger.warn("渲染器:: 多盏聚光灯阴影瓦片 {} 绑定失败（帧槽 {}）", tile, slot);
		}
	}

	/*
	聚光阴影图集切换（第六百七十轮；C1）
	按当前请求重建图集（4S × 2S）并重写 binding 9 的 8 个数组元素；
	请求关闭或创建失败时恢复逐瓦片视图（缺瓦片回落默认白纹理）。
	*/
	void spot_shadow_atlas_apply_impl(Renderer_Impl_Holder::Impl* impl)
	{
		//等尽在途帧，避免重建期间仍被采样
		impl->frame.frames_wait();
		//回收旧图集
		if (impl->spot_shadow_atlas_target != 0)
		{
			impl->resource_domain->state().render_target.target_destroy(impl->spot_shadow_atlas_target);
			impl->spot_shadow_atlas_target = 0;
		}
		impl->spot_shadow_atlas = false;
		//默认占位视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		impl->resource_domain->state().texture.texture_bind_get(impl->resource_domain->state().default_texture, default_view, sampler);
		//需要图集时按 4×2 创建（每瓦片 shadow_size 见方）
		if (impl->spot_shadow_atlas_requested)
		{
			const uint32_t atlas_width = impl->shadow_size * 4;
			const uint32_t atlas_height = impl->shadow_size * 2;
			if (impl->resource_domain->state().render_target.target_create(impl->spot_shadow_atlas_target, atlas_width, atlas_height,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				impl->spot_shadow_atlas = true;
				/*
				图集接管后回收独立瓦片目标（第七百零七轮；Q-A）
				本函数开头已等尽在途帧，销毁安全；此后瓦片由图集承担，不再逐帧懒创建。
				*/
				for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
				{
					//未建立的瓦片跳过
					if (impl->spot_shadow_targets[tile] == 0)
						continue;
					impl->resource_domain->state().render_target.target_destroy(impl->spot_shadow_targets[tile]);
					impl->spot_shadow_targets[tile] = 0;
				}
			}
			else
			{
				impl->spot_shadow_atlas_target = 0;
				logger.warn("渲染器:: 聚光阴影图集创建失败（{}×{}），回落独立瓦片", atlas_width, atlas_height);
			}
		}
		//图集颜色视图（未生效时为空）
		VkImageView atlas_view = VK_NULL_HANDLE;
		if (impl->spot_shadow_atlas)
		{
			VkImage atlas_image = VK_NULL_HANDLE;
			VkImageView atlas_depth_view = VK_NULL_HANDLE;
			VkExtent2D atlas_extent{};
			if (!impl->resource_domain->state().render_target.target_get(impl->spot_shadow_atlas_target, atlas_image, atlas_view,
				atlas_depth_view, atlas_extent))
			{
				impl->resource_domain->state().render_target.target_destroy(impl->spot_shadow_atlas_target);
				impl->spot_shadow_atlas_target = 0;
				impl->spot_shadow_atlas = false;
				atlas_view = VK_NULL_HANDLE;
				logger.warn("渲染器:: 聚光阴影图集查询失败，回落独立瓦片");
			}
		}
		//逐帧槽重写 binding 9 的全部数组元素
		for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
		{
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			{
				//默认回落白纹理占位
				VkImageView view = default_view;
				if (impl->spot_shadow_atlas)
					view = atlas_view;
				else if (impl->spot_shadow_targets[tile] != 0)
				{
					VkImage tile_image = VK_NULL_HANDLE;
					VkImageView tile_view = VK_NULL_HANDLE;
					VkImageView tile_depth_view = VK_NULL_HANDLE;
					VkExtent2D tile_extent{};
					if (impl->resource_domain->state().render_target.target_get(impl->spot_shadow_targets[tile], tile_image, tile_view,
						tile_depth_view, tile_extent))
						view = tile_view;
				}
				if (!impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, view,
					static_cast<uint32_t>(tile)))
					logger.warn("渲染器:: 聚光阴影图集切换：帧槽 {} 元素 {} 绑定失败", slot, tile);
			}
		}
		//阴影图内容口径变化，下一帧必须重绘
		impl->shadow_pass_ready = false;
	}
}
