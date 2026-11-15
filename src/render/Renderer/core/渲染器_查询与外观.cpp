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

namespace engine
{
	//当前聚光灯数量（第一百一十五轮）
	uint32_t Render_Scene::spot_light_count(void) const
	{
		//未初始化返回 0
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return 0;
		//统计 is_spot 为真的光源
		uint32_t count = 0;
		for (uint32_t index = 0; index < state().point_light_count; ++index)
			if (state().point_lights[index].is_spot)
				++count;
		return count;
	}

	//资源计数快照（第一百一十三 / 一百一十四轮）
	Renderer_Resource_Counts Renderer_Core::resource_counts(void) const
	{
		//默认全零
		Renderer_Resource_Counts counts{};
		//未初始化直接返回
		if (impl == nullptr || !impl->initialized)
			return counts;
		//各模块有效记录数
		counts.meshes = impl->resource_domain->state().mesh.mesh_count();
		counts.textures = impl->resource_domain->state().texture.texture_count();
		counts.pipelines = impl->pipeline.pipeline_count();
		counts.fonts = self().ui().state().font.font_count();
		counts.render_targets = impl->resource_domain->state().render_target.target_count();
		//材质按有效记录计
		for (const Material_Record& record : impl->resource_domain->state().materials)
			if (record.valid)
				++counts.materials;
		return counts;
	}

	//累计场景 UBO 上传字节数（第六十轮新增）
	uint64_t Renderer_Core::scene_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->scene_upload_bytes : 0;
	}

	//PBR-lite 镜面开关（第六十四轮新增）
	void Render_Scene::pbr_enable_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().pbr_enabled = enabled;
	}

	//PBR-lite 是否启用（第六十四轮新增）
	bool Render_Scene::pbr_enabled(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().pbr_enabled : false;
	}

	//PBR-lite 粗糙度缩放（第六十四轮新增）
	void Render_Scene::pbr_roughness_set(float scale)
	{
		//未初始化静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//合法区间 0.05..4；越界或非数整次忽略
		if (!(scale >= 0.05f && scale <= 4.0f))
		{
			logger.warn("渲染器:: PBR 粗糙度缩放 {} 越界（0.05..4），已忽略", scale);
			return;
		}
		state().pbr_roughness = scale;
	}

	//当前 PBR-lite 粗糙度缩放（第六十四轮新增）
	float Render_Scene::pbr_roughness(void) const
	{
		//未初始化返回默认 1
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().pbr_roughness : 1.0f;
	}

	//PBR 金属度（第六十五轮新增）
	void Render_Scene::pbr_metallic_set(float metallic)
	{
		//未初始化静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//合法区间 0..1；越界或非数整次忽略
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: PBR 金属度 {} 越界（0..1），已忽略", metallic);
			return;
		}
		state().pbr_metallic = metallic;
	}

	//当前 PBR 金属度（第六十五轮新增）
	float Render_Scene::pbr_metallic(void) const
	{
		//未初始化返回默认 0
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().pbr_metallic : 0.0f;
	}

	//PBR 掠射高光能量修正（第六百七十二轮；D1 第一步）
	void Render_Scene::pbr_energy_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//与原值一致则无需处理
		if (state().pbr_energy == enabled)
			return;
		state().pbr_energy = enabled;
	}

	//当前 PBR 掠射高光能量修正是否开启（第六百七十二轮）
	bool Render_Scene::pbr_energy(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().pbr_energy : false;
	}

	//PBR 粗糙度绝对口径（第七百轮；D1 收尾）
	void Render_Scene::pbr_roughness_absolute_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//与原值一致则无需处理
		if (state().pbr_roughness_absolute == enabled)
			return;
		state().pbr_roughness_absolute = enabled;
	}

	//当前 PBR 粗糙度绝对口径是否开启（第七百轮）
	bool Render_Scene::pbr_roughness_absolute(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().pbr_roughness_absolute : false;
	}

	/*
	二次元风格化 · 光照色阶量化与边缘光（第一百七十四轮新增）
	steps：0 = 关闭；1..8 = 量化档数（越界钳制）。
	rim：0 = 关闭；0..1（越界钳制，非有限值按 0）。
	两者均只写场景 UBO 的保留分量，默认值下画面逐字节不变。
	*/
	void Render_Scene::toon_set(uint32_t steps, float rim)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//色阶数钳到 [0, 8]
		state().toon_steps = steps > 8 ? 8 : steps;
		//边缘光强度钳到 [0, 1]（非有限值按 0）
		float effective_rim = std::isfinite(rim) ? rim : 0.0f;
		state().toon_rim = effective_rim > 1.0f ? 1.0f : (effective_rim > 0.0f ? effective_rim : 0.0f);
	}

	//当前色阶数（第一百七十四轮新增）
	uint32_t Render_Scene::toon_steps(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().toon_steps : 0;
	}

	//当前边缘光强度（第一百七十四轮新增）
	float Render_Scene::toon_rim(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().toon_rim : 0.0f;
	}

	/*
	设置反向外扩描边（第二百二十八轮）
	宽度钳到 [0, 1]（非有限值按 0），颜色逐分量钳到 [0, 1]（非有限值按 0）；
	描边管线未登记时仅记录参数（绘制项不会额外登记描边项，画面不变）。
	*/
	void Render_Scene::outline_set(float width, const float color[3])
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//外扩宽度钳到 [0, 1]（非有限值按 0 = 关闭）
		float effective_width = std::isfinite(width) ? width : 0.0f;
		state().outline_width = effective_width > 1.0f ? 1.0f : (effective_width > 0.0f ? effective_width : 0.0f);
		//描边色逐分量钳到 [0, 1]（非有限值按 0）
		for (std::size_t index = 0; index < 3; ++index)
		{
			float channel = color != nullptr ? color[index] : 0.0f;
			if (!std::isfinite(channel))
				channel = 0.0f;
			state().outline_color[index] = channel > 1.0f ? 1.0f : (channel > 0.0f ? channel : 0.0f);
		}
	}

	//关闭反向外扩描边（第二百二十八轮新增）
	void Render_Scene::outline_clear(void)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		state().outline_width = 0.0f;
	}

	//当前外扩宽度（第二百二十八轮新增）
	float Render_Scene::outline_width(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().outline_width : 0.0f;
	}

	//读取当前描边色（第二百二十八轮新增）
	void Render_Scene::outline_color(float out[3]) const
	{
		//出参为空则忽略
		if (out == nullptr)
			return;
		//未初始化写全零（黑）
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = state().outline_color[index];
	}

	//设置暗部色调（第一百七十八轮新增）
	void Render_Scene::toon_shadow_tint_set(float r, float g, float b)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//逐分量归一：非有限值按 1（白 = 关闭），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 1.0f;
			state().toon_shadow_tint[index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
	}

	//读取暗部色调（第一百七十八轮新增）
	void Render_Scene::toon_shadow_tint(float out[3]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return;
		//未初始化时写默认白（关闭）
		const bool ready = owner != nullptr && owner->impl != nullptr && owner->impl->initialized;
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = ready ? state().toon_shadow_tint[index] : 1.0f;
	}

	//设置卡通高光阈值（第一百七十九轮新增）
	void Render_Scene::toon_specular_set(float threshold)
	{
		//若尚未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return;
		//阈值钳到 [0, 1]（非有限值按 0 = 关闭）
		const float effective = std::isfinite(threshold) ? threshold : 0.0f;
		state().toon_specular = effective > 1.0f ? 1.0f : (effective > 0.0f ? effective : 0.0f);
	}

	//读取卡通高光阈值（第一百七十九轮新增）
	float Render_Scene::toon_specular(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->impl != nullptr && owner->impl->initialized ? state().toon_specular : 0.0f;
	}

	//累计实例缓冲上传字节数（第六十一轮新增）
	uint64_t Renderer_Core::instance_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->instance_upload_bytes : 0;
	}

	//读回阴影图（第五十七轮新增）
	bool Renderer_Core::shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out)
	{
		//前置：已初始化、阴影已开启、阴影目标存在且参数齐备
		if (impl == nullptr || !impl->initialized || !self().scene().state().shadow_enabled || impl->shadow_target == 0
			|| pixels == nullptr || capacity == 0)
			return false;
		//等尽在途帧，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回打包深度）
		return impl->resource_domain->state().render_target.target_readback(impl->shadow_target, pixels, capacity, width_out, height_out);
	}

	//设置清屏色（RGBA）
	void Render_Scene::clear_color_set(const float color[4])
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized || color == nullptr)
			return;
		/*
		逐分量钳制到 [0,1] 后拷贝（自下一次 frame_render 起生效）
		越界与非数（NaN）均被归一：负 / 非数 → 0，大于 1 → 1；故清屏色恒落在合法色域内。
		*/
		for (std::size_t index = 0; index < 4; ++index)
		{
			//当前分量
			float value = color[index];
			//若为负或非数则归 0
			if (!(value >= 0.0f))
				value = 0.0f;
			//若大于 1 则归 1
			else if (value > 1.0f)
				value = 1.0f;
			state().clear_color[index] = value;
		}
	}
}
