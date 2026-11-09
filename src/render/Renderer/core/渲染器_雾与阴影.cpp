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
	void Renderer_Lighting::fog_set(const float color[3], float density, float start, float end)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始非负、结束大于起始；
		任一非法即整次忽略并告警（保持既有雾参数）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f) && (end > start) && (end <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 雾参数非法（密度 {}、起始 {}、结束 {}），整次忽略", density, start, end);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			impl->fog_color[index] = color[index];
		impl->fog_density = density;
		impl->fog_start = start;
		impl->fog_end = end;
		//切回线性区间模式（第一百四十三轮）
		impl->fog_mode = 0;
	}

	//设置指数平方雾（第一百四十三轮新增）
	void Renderer_Lighting::fog_exp2_set(const float color[3], float density, float start)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始须非负且有限；
		任一非法即整次忽略并告警（保持既有雾参数与模式）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 指数平方雾参数非法（密度 {}、起始 {}），整次忽略", density, start);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			impl->fog_color[index] = color[index];
		impl->fog_density = density;
		impl->fog_start = start;
		//切到指数平方模式
		impl->fog_mode = 1;
	}

	//读取当前雾模式（第一百四十三轮新增）
	uint32_t Renderer_Lighting::fog_mode_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->fog_mode;
	}

	//设置雾的高度衰减（第一百四十三轮新增）
	void Renderer_Lighting::fog_height_set(float falloff)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//系数须在 [0, 10]（取反比较使 NaN 判非法）
		if (!(falloff >= 0.0f && falloff <= 10.0f))
		{
			logger.warn("渲染器:: 雾高度衰减系数非法（{}），整次忽略", falloff);
			return;
		}
		impl->fog_height_falloff = falloff;
	}

	//关闭距离雾（第三十八轮新增）
	void Renderer_Lighting::fog_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//密度归零即关闭（颜色与区间保持既有值，便于再次开启）
		impl->fog_density = 0.0f;
	}

	//设置环境光颜色（第四十轮新增）
	void Renderer_Lighting::ambient_color_set(const float color[3])
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 环境光颜色非法（{}），整次忽略", color[index]);
				return;
			}
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_color[index] = color[index];
	}

	//设置半球环境光（第四十七轮新增）
	void Renderer_Lighting::ambient_ground_set(const float ground_color[3], float hemisphere)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || ground_color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(ground_color[index] >= 0.0f && ground_color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 半球环境光颜色非法（{}），整次忽略", ground_color[index]);
				return;
			}
		//强度按 0..1 钳制（非数按 0 = 关闭处理）
		float blend = 0.0f;
		//仅有限值参与混合
		if (hemisphere >= 0.0f && hemisphere <= 1.0f)
			blend = hemisphere;
		else if (hemisphere > 1.0f)
			blend = 1.0f;
		//写入地面颜色与混合强度
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_ground[index] = ground_color[index];
		impl->ambient_ground[3] = blend;
	}

	//设置补光（第四十七轮新增；第二盏方向光；第五十轮支持逐光高光缩放）
	void Renderer_Core::light_fill_set(const float direction[3], const float color[3], float intensity, float specular)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验（口径同 light_set）
		方向长度须非零、颜色各分量须落在 [0,1]、强度须非负且有限、高光缩放须落在 0..100；任一非法即整次拒绝。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool fill_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (specular >= 0.0f) && (specular <= 100.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fill_valid = false;
		//若补光参数非法
		if (!fill_valid)
		{
			logger.warn("渲染器:: 补光参数非法（强度 {}、高光缩放 {}），整次拒绝", intensity, specular);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->fill_direction[index] = direction[index] * inverse_length;
			impl->fill_color[index] = color[index];
		}
		impl->fill_intensity = intensity;
		impl->fill_specular = specular;
	}

	//设置方向光的逐光高光缩放（第五十轮新增）
	void Renderer_Lighting::directional_specular_set(float scale)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//缩放须落在 0..100（取反比较使 NaN 判非法）
		if (!(scale >= 0.0f && scale <= 100.0f))
		{
			logger.warn("渲染器:: 方向光高光缩放非法（{}），整次忽略", scale);
			return;
		}
		impl->directional_specular = scale;
	}

	//开关方向光阴影（第五十二轮新增）
	void Renderer_Lighting::shadow_enable_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
	首次开启时懒登记阴影管线
	未启用阴影的进程不承担管线创建开销；登记失败则保持关闭并告警（不改变其余状态）。
	阴影目标为 R8G8B8A8_UNORM（见 init ⑳ ①），故该管线必须按同一颜色格式创建，
	不能经公开 pipeline_load（它固定跟随交换链格式，sRGB 会破坏打包深度）。
	*/
	if (enabled && impl->shadow_pipeline == 0)
	{
		//加载阴影着色器模块（顺带解析顶点输入位置）
		VkShaderModule shadow_vert = VK_NULL_HANDLE;
		VkShaderModule shadow_frag = VK_NULL_HANDLE;
		uint32_t shadow_input_locations = 0;
		if (!impl->shader.module_load(shadow_vert, "阴影.vert.spv", &shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：顶点着色器加载失败，阴影保持关闭");
			impl->shadow_enabled = false;
			return;
		}
		if (!impl->shader.module_load(shadow_frag, "阴影.frag.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：片元着色器加载失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shadow_enabled = false;
			return;
		}
		//按阴影目标的 UNORM 颜色格式登记具名管线
		uint64_t shadow_pipeline = 0;
		//阴影目标固定单采样（第九十六轮），故该管线采样数固定 1，不跟随 MSAA
		if (!impl->pipeline.pipeline_register(shadow_pipeline, shadow_vert, shadow_frag,
			Blend_Mode::Opaque, VK_FORMAT_R8G8B8A8_UNORM, true, 1, shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：图形管线创建失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shader.module_destroy(shadow_frag);
			impl->shadow_enabled = false;
			return;
		}
		//记录具名管线（着色器模块所有权在门面，随 pipeline_unload / shutdown 回收）
		Impl::Extra_Pipeline_Record record{};
		record.handle = shadow_pipeline;
		record.vert_module = shadow_vert;
		record.frag_module = shadow_frag;
		impl->extra_pipelines.push_back(record);
		impl->shadow_pipeline = shadow_pipeline;
	}
	impl->shadow_enabled = enabled;
	}

	//阴影是否启用（第五十二轮）
	bool Renderer_Lighting::shadow_enabled(void) const
	{
		//未初始化一律按关闭处理
		return impl != nullptr && impl->initialized && impl->shadow_enabled;
	}

	//设置阴影深度偏置（第五十二轮）
	void Renderer_Lighting::shadow_bias_set(float bias)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//偏置须落在 0..0.05（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f))
		{
			logger.warn("渲染器:: 阴影深度偏置非法（{}），整次忽略", bias);
			return;
		}
		impl->shadow_bias = bias;
	}

	//当前阴影深度偏置（第五十二轮）
	float Renderer_Lighting::shadow_bias(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_bias : 0.003f;
	}

	//设置阴影软度（第五十三轮新增）
	void Renderer_Lighting::shadow_softness_set(float softness)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//软度须落在 0..1（取反比较使 NaN 判非法）
		if (!(softness >= 0.0f && softness <= 1.0f))
		{
			logger.warn("渲染器:: 阴影软度非法（{}），整次忽略", softness);
			return;
		}
		impl->shadow_softness = softness;
	}

	//当前阴影软度（第五十三轮）
	float Renderer_Lighting::shadow_softness(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_softness : 0.0f;
	}

	//设置阴影强度（第五十三轮新增）
	void Renderer_Lighting::shadow_strength_set(float strength)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//强度须落在 0..1（取反比较使 NaN 判非法）
		if (!(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 阴影强度非法（{}），整次忽略", strength);
			return;
		}
		impl->shadow_strength = strength;
	}

	//当前阴影强度（第五十三轮）
	float Renderer_Lighting::shadow_strength(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_strength : 1.0f;
	}

	/*
	设置聚光灯阴影质量参数（第四百七十六轮新增）
	三项与方向光阴影独立；任一非法即整次忽略，已设值保持不变。
	*/
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

	//当前聚光灯阴影参数（第四百七十六轮）
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

	/*
	设置聚光灯阴影序号（第四百七十九轮新增；第六百九十九轮仍限 8 瓦片）
	序号指向已登记的光源槽位（0..7，点光与聚光共用）；该槽不是聚光灯时按无聚光阴影处理；
	前向 8..15 槽可参与光照但不投影，故序号上限仍为 8。
	*/
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

	//当前聚光灯阴影序号（第四百七十九轮）
	int Renderer_Lighting::spot_shadow_index(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_index : 0;
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
		impl->texture.texture_bind_get(impl->default_texture, default_view, sampler);
		for (std::size_t tile = 0; tile < max_tiles; ++tile)
		{
			//已建立则跳过
			if (impl->spot_shadow_targets[tile] != 0)
				continue;
			if (!impl->render_target.target_create(impl->spot_shadow_targets[tile], impl->shadow_size,
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
			if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], image, view, depth_view, extent))
			{
				impl->render_target.target_destroy(impl->spot_shadow_targets[tile]);
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
	设置多盏聚光灯阴影开关（第五百一十一轮）
	开启时立即补齐瓦片并置阴影图待重绘；关闭时沿用 spot_shadow_index 的单盏语义。
	*/
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

	/*
	聚光阴影图集开关（第六百七十轮；C1）
	请求值与实际状态分开：创建失败时请求仍保持，避免逐帧重试；返回实际生效状态。
	*/
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

	//当前聚光阴影图集是否实际生效（第六百七十轮）
	bool Renderer_Lighting::spot_shadow_atlas(void) const
	{
		//未初始化返回假
		return impl != nullptr && impl->initialized ? impl->spot_shadow_atlas : false;
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
			impl->render_target.target_destroy(impl->spot_shadow_atlas_target);
			impl->spot_shadow_atlas_target = 0;
		}
		impl->spot_shadow_atlas = false;
		//默认占位视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		impl->texture.texture_bind_get(impl->default_texture, default_view, sampler);
		//需要图集时按 4×2 创建（每瓦片 shadow_size 见方）
		if (impl->spot_shadow_atlas_requested)
		{
			const uint32_t atlas_width = impl->shadow_size * 4;
			const uint32_t atlas_height = impl->shadow_size * 2;
			if (impl->render_target.target_create(impl->spot_shadow_atlas_target, atlas_width, atlas_height,
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
					impl->render_target.target_destroy(impl->spot_shadow_targets[tile]);
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
			if (!impl->render_target.target_get(impl->spot_shadow_atlas_target, atlas_image, atlas_view,
				atlas_depth_view, atlas_extent))
			{
				impl->render_target.target_destroy(impl->spot_shadow_atlas_target);
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
					if (impl->render_target.target_get(impl->spot_shadow_targets[tile], tile_image, tile_view,
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

	/*
	逐盏聚光灯阴影开关（第六百四十八轮；A2 数据化）
	按前向槽位设置该盏聚光灯是否参与阴影；越界返回假（配置不变）。
	与原值一致时为空操作；变化后置阴影图待重绘（槽位映射随下一帧重算）。
	*/
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

	/*
	PCSS 软阴影开关与预算（第六百五十七轮）
	仅影响着色器采样（不重建阴影图）；enabled = false 恢复旧口径。
	search 钳到 [1, 32] 纹素、max_radius 钳到 [1, 16] 纹素；非有限值整次忽略。
	*/
	void Renderer_Lighting::shadow_pcss_set(bool enabled, float search, float max_radius)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//非有限值整次忽略
		if (!std::isfinite(search) || !std::isfinite(max_radius))
			return;
		//搜索尺度与 PCF 半径钳到有效区间
		impl->shadow_pcss_search = search < 1.0f ? 1.0f : (search > 32.0f ? 32.0f : search);
		impl->shadow_pcss_max = max_radius < 1.0f ? 1.0f : (max_radius > 16.0f ? 16.0f : max_radius);
		impl->shadow_pcss_enabled = enabled;
	}

	/*
	逐盏聚光灯阴影偏置缩放（第六百五十八轮）
	仅影响采样偏置（不重建阴影图）；钳到 [0.1, 10]，非有限值忽略，越界槽位返回假。
	*/
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

	/*
	逐盏聚光灯阴影质量缩放（第七百零八轮）
	仅影响采样（不重建阴影图）：软度钳到 [0.25, 4]、强度钳到 [0, 1]，默认 1 / 1。
	非有限值整次忽略，越界槽位返回假。
	*/
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

	/*
	逐盏聚光灯阴影优先级（第七百零九轮）
	仅用于「可投影聚光灯超过 8 盏」时的取舍，不影响单盏与 ≤ 8 盏路径；非有限值忽略，越界槽位返回假。
	*/
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

	/*
	阴影分辨率预算（第六百五十九轮）
	仅作为 scene_apply 的配置来源；实际边长换算在 scene_apply 内完成（依赖级联 / 聚光单位数）。
	*/
	void Renderer_Lighting::shadow_budget_set(uint64_t bytes)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//存到配置副本（scene_apply 直接读 Light_Data，此处只为门面等价入口）
		impl->shadow_budget_bytes = bytes > 1073741824ull ? 1073741824ull : bytes;
	}

	/*
	曝光补偿（第六百六十轮）
	EV 钳到 [-4, 4]，非有限值忽略；0（默认）为乘性恒等。
	*/
	bool Renderer_Lighting::exposure_set(float ev)
	{
		//若尚未初始化或值非有限
		if (impl == nullptr || !impl->initialized || !std::isfinite(ev))
			return false;
		impl->exposure = ev < -4.0f ? -4.0f : (ev > 4.0f ? 4.0f : ev);
		return true;
	}

	//当前曝光补偿（EV）
	float Renderer_Lighting::exposure_get(void) const
	{
		return impl != nullptr && impl->initialized ? impl->exposure : 0.0f;
	}

	//当前是否多盏聚光灯阴影（第五百一十一轮）
	bool Renderer_Lighting::spot_shadow_all(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_all : false;
	}

	/*
	设置聚光灯阴影偏置自校准（第五百二十一轮）
	开启后逐瓦片偏置按「参考跨度 9 / 该瓦片跨度」缩放；关闭恢复全局偏置。
	*/
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

	//当前是否开启偏置自校准（第五百二十一轮）
	bool Renderer_Lighting::spot_shadow_bias_auto(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->spot_shadow_bias_auto : false;
	}

	//最近一帧逐瓦片有效偏置（第五百二十一轮）
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
	设置阴影图分辨率（第五十四轮新增）
	仅接受 512 / 1024 / 2048；切换时先等尽在途帧，再建新目标、重新绑定各帧槽 binding 1，
	全部成功后才销毁旧目标（任一步失败均保持旧目标可用）。
	*/
	void Renderer_Lighting::shadow_resolution_set(uint32_t resolution)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//仅接受三档边长
		if (resolution != 512 && resolution != 1024 && resolution != 2048)
		{
			logger.warn("渲染器:: 阴影分辨率非法（{}），整次忽略（仅支持 512 / 1024 / 2048）", resolution);
			return;
		}
		//与原值一致则无需重建
		if (resolution == impl->shadow_size)
			return;
		//等尽在途帧，避免重建期间仍被采样
		if (!impl->frame.frames_wait())
		{
			logger.error("渲染器:: 阴影分辨率切换失败：等待在途帧失败");
			return;
		}
		//旧阴影图视图（失败回绑用）
		VkImage old_image = VK_NULL_HANDLE;
		VkImageView old_view = VK_NULL_HANDLE;
		VkImageView old_depth_view = VK_NULL_HANDLE;
		VkExtent2D old_extent{};
		if (!impl->render_target.target_get(impl->shadow_target, old_image, old_view,
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
			if (impl->spot_shadow_targets[tile] == 0)
				continue;
			VkImage old_spot_image = VK_NULL_HANDLE;
			VkImageView old_spot_depth_view = VK_NULL_HANDLE;
			VkExtent2D old_spot_extent{};
			if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], old_spot_image, old_spot_views[tile],
				old_spot_depth_view, old_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：旧聚光灯阴影图查询失败（瓦片 {}）", tile);
				return;
			}
		}
		//先建新目标（失败则旧目标原样保留）；颜色格式固定 UNORM，理由见 init ⑳ ①
		uint64_t candidate = 0;
		//阴影目标固定单采样（第九十六轮）
		if (!impl->render_target.target_create(candidate, resolution, resolution,
			VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影目标创建失败（保持 {}）", impl->shadow_size);
			return;
		}
		//聚光灯阴影目标同分辨率重建（第四百八十七轮；第五百零七轮扩为 4 张）
		uint64_t candidate_spot_tiles[Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES] = {};
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未建立则跳过
			if (impl->spot_shadow_targets[tile] == 0)
				continue;
			if (!impl->render_target.target_create(candidate_spot_tiles[tile], resolution, resolution,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影目标创建失败（瓦片 {}）", tile);
				impl->render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < tile; ++created)
					if (candidate_spot_tiles[created] != 0)
						impl->render_target.target_destroy(candidate_spot_tiles[created]);
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
		if (!impl->render_target.target_get(candidate, new_image, new_view, new_depth_view, new_extent)
			|| !impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影图查询失败");
			impl->render_target.target_destroy(candidate);
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
				if (candidate_spot_tiles[tile] != 0)
					impl->render_target.target_destroy(candidate_spot_tiles[tile]);
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
			if (!impl->render_target.target_get(candidate_spot_tiles[tile], new_spot_image, new_spot_views[tile],
				new_spot_depth_view, new_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影图查询失败（瓦片 {}）", tile);
				impl->render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++created)
					if (candidate_spot_tiles[created] != 0)
						impl->render_target.target_destroy(candidate_spot_tiles[created]);
				return;
			}
		}
		//逐帧槽改绑新阴影图
		for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
		{
			//若某槽绑定失败，则把已改绑的槽全部回绑旧图并放弃新目标
			if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, new_view))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 绑定失败，回绑旧阴影图", slot);
				for (std::size_t revert = 0; revert <= slot; ++revert)
					impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
				impl->render_target.target_destroy(candidate);
				for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
					if (candidate_spot_tiles[tile] != 0)
						impl->render_target.target_destroy(candidate_spot_tiles[tile]);
				return;
			}
		}
		//逐帧槽改绑新聚光灯阴影图（第四百八十七轮；第五百零七轮四瓦片）
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//该瓦片未重建则无需改绑
			if (candidate_spot_tiles[tile] == 0)
				continue;
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			{
				//任一槽失败：方向光与全部聚光瓦片绑定一并回绑旧图
				if (!impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, new_spot_views[tile],
					static_cast<uint32_t>(tile)))
				{
					logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 聚光灯阴影绑定失败（瓦片 {}），回绑旧图", slot, tile);
					for (std::size_t revert = 0; revert < impl->config.frame_overlap; ++revert)
					{
						impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
						for (std::size_t target_tile = 0; target_tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++target_tile)
						{
							//优先回绑旧视图；旧瓦片不存在时保持新视图
							if (candidate_spot_tiles[target_tile] == 0)
								continue;
							VkImageView revert_view = old_spot_views[target_tile] != VK_NULL_HANDLE
								? old_spot_views[target_tile] : new_spot_views[target_tile];
							impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(revert), sampler, revert_view,
								static_cast<uint32_t>(target_tile));
						}
					}
					impl->render_target.target_destroy(candidate);
					for (std::size_t created = 0; created < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++created)
						if (candidate_spot_tiles[created] != 0)
							impl->render_target.target_destroy(candidate_spot_tiles[created]);
					return;
				}
			}
		}
		//全部改绑成功后销毁旧目标并更新状态
		impl->render_target.target_destroy(impl->shadow_target);
		impl->shadow_target = candidate;
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			//未重建的瓦片保持原目标
			if (candidate_spot_tiles[tile] == 0)
				continue;
			impl->render_target.target_destroy(impl->spot_shadow_targets[tile]);
			impl->spot_shadow_targets[tile] = candidate_spot_tiles[tile];
		}
		impl->shadow_size = resolution;
		/*
		聚光阴影图集按新边长重建（第六百七十轮；仅请求仍有效时）
		重建失败时 apply 内部回落独立瓦片，请求保留以避免逐帧重试。
		*/
		if (impl->spot_shadow_atlas_requested)
			spot_shadow_atlas_apply_impl(impl.get());
		//新目标尚无有效内容，下一帧必须重绘阴影图
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影图分辨率已切换为 {}", resolution);
	}

	//当前阴影图分辨率（第五十四轮）
	uint32_t Renderer_Lighting::shadow_resolution(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_size : Impl::SHADOW_SIZE;
	}

	//设置阴影光源正交视锥范围（第一百零七轮）
	void Renderer_Lighting::shadow_range_set(float half_extent, float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：半宽为正、近平面为正、远平面大于近平面（非数一并拒绝）
		if (!(half_extent > 0.0f) || !(near_plane > 0.0f) || !(far_plane > near_plane))
		{
			logger.warn("渲染器:: 阴影视锥参数非法（半宽 {}，近 {}，远 {}），本次忽略",
				half_extent, near_plane, far_plane);
			return;
		}
		impl->shadow_half_extent = half_extent;
		impl->shadow_near = near_plane;
		impl->shadow_far = far_plane;
		//光源空间矩阵随下一帧更新，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影视锥已设为 半宽 {}，近 {}，远 {}", half_extent, near_plane, far_plane);
	}

	//当前阴影正交范围（第一百零七轮）
	void Renderer_Lighting::shadow_range_get(float* half_extent_out, float* near_out, float* far_out) const
	{
		//默认值与未初始化一致
		float half_extent = 12.0f;
		float near_plane = 1.0f;
		float far_plane = 60.0f;
		//已初始化则取当前值
		if (impl != nullptr && impl->initialized)
		{
			half_extent = impl->shadow_half_extent;
			near_plane = impl->shadow_near;
			far_plane = impl->shadow_far;
		}
		if (half_extent_out != nullptr)
			*half_extent_out = half_extent;
		if (near_out != nullptr)
			*near_out = near_plane;
		if (far_out != nullptr)
			*far_out = far_plane;
	}

	//设置级联阴影配置（第一百二十一轮）
	void Renderer_Lighting::shadow_cascade_set(uint32_t count, float far_scale, float split_distance)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：级联数仅 1 / 2 / 3、远区倍数 > 1、切换距离 > 0（非数一并拒绝）
		if ((count != 1 && count != 2 && count != 3) || !(far_scale > 1.0f) || !(split_distance > 0.0f))
		{
			logger.warn("渲染器:: 级联阴影参数非法（级联数 {}，远区倍数 {}，切换距离 {}），本次忽略",
				count, far_scale, split_distance);
			return;
		}
		impl->cascade_count = count;
		impl->cascade_far_scale = far_scale;
		impl->cascade_split_distance = split_distance;
		//分片方式或范围改变，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 级联阴影已设为 {} 级（远区倍数 {}，切换距离 {}，单片边长 {}）",
			count, far_scale, split_distance,
			count > 1 ? impl->shadow_size / count : impl->shadow_size);
	}

	//当前级联阴影配置（第一百二十一轮）
	void Renderer_Lighting::shadow_cascade_get(uint32_t* count_out, float* far_scale_out, float* split_distance_out) const
	{
		//默认值与未初始化一致
		uint32_t count = 1;
		float far_scale = 3.0f;
		float split_distance = 12.0f;
		//已初始化则取当前值
		if (impl != nullptr && impl->initialized)
		{
			count = impl->cascade_count;
			far_scale = impl->cascade_far_scale;
			split_distance = impl->cascade_split_distance;
		}
		if (count_out != nullptr)
			*count_out = count;
		if (far_scale_out != nullptr)
			*far_scale_out = far_scale;
		if (split_distance_out != nullptr)
			*split_distance_out = split_distance;
	}

	//设置级联接缝淡化比例（第一百四十八轮）
	void Renderer_Lighting::shadow_cascade_blend_set(float blend)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//比例须在 [0, 0.5]（取反比较使 NaN 判非法）
		if (!(blend >= 0.0f && blend <= 0.5f))
		{
			logger.warn("渲染器:: 级联接缝淡化比例非法（{}），本次忽略", blend);
			return;
		}
		impl->cascade_blend = blend;
	}

	//当前级联接缝淡化比例（第一百四十八轮）
	float Renderer_Lighting::shadow_cascade_blend(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->cascade_blend;
	}

	//设置阴影纹素尺寸上限（第六百六十一轮）
	void Renderer_Lighting::shadow_texel_max_set(float texel)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
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
		if (effective != impl->shadow_texel_max)
		{
			impl->shadow_texel_max = effective;
			impl->shadow_pass_ready = false;
			logger.info("渲染器:: 阴影纹素上限已设为 {}", effective);
		}
	}

	//当前阴影纹素尺寸上限（第六百六十一轮）
	float Renderer_Lighting::shadow_texel_max(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_texel_max : 0.0f;
	}

	//设置级联切换带自动淡化（第六百六十一轮）
	void Renderer_Lighting::shadow_cascade_blend_auto_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->cascade_blend_auto = enabled;
	}

	//当前是否开启级联切换带自动淡化（第六百六十一轮）
	bool Renderer_Lighting::shadow_cascade_blend_auto(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized && impl->cascade_blend_auto;
	}

	//设置阴影批量提交（第六百六十九轮）
	void Renderer_Lighting::shadow_batch_submit_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->shadow_batch_submit = enabled;
	}

	//当前是否开启阴影批量提交（第六百六十九轮）
	bool Renderer_Lighting::shadow_batch_submit(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized && impl->shadow_batch_submit;
	}

	//累计实际执行的阴影 pass 次数（第五十五轮）
	uint64_t Renderer_Lighting::shadow_pass_count(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->shadow_pass_count : 0;
	}

}
