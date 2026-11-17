#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
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
		if (owner == nullptr || !owner->device().valid())
			return 0;
		//统计 is_spot 为真的光源
		uint32_t count = 0;
		for (uint32_t index = 0; index < (*state_ptr).point_light_count; ++index)
			if ((*state_ptr).point_lights[index].is_spot)
				++count;
		return count;
	}

	//资源计数快照（第一百一十三 / 一百一十四轮）
	Renderer_Resource_Counts Render_Device::resource_counts(void) const
	{
		//默认全零
		Renderer_Resource_Counts counts{};
		//未初始化直接返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return counts;
		//各域有效记录数
		owner->resource().resource_counts_fill(counts);
		counts.fonts = owner->ui().font_resource_count();
		return counts;
	}

	//累计场景 UBO 上传字节数（第六十轮新增）
	uint64_t Render_Device::scene_upload_bytes(void) const
	{
		//未初始化返回 0
		return state_ptr != nullptr && state_ptr->initialized ? state_ptr->scene_upload_bytes : 0;
	}

	//PBR-lite 镜面开关（第六十四轮新增）
	void Render_Scene::pbr_enable_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).pbr_enabled = enabled;
	}

	//PBR-lite 是否启用（第六十四轮新增）
	bool Render_Scene::pbr_enabled(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_enabled : false;
	}

	//PBR-lite 粗糙度缩放（第六十四轮新增）
	void Render_Scene::pbr_roughness_set(float scale)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		//合法区间 0.05..4；越界或非数整次忽略
		if (!(scale >= 0.05f && scale <= 4.0f))
		{
			logger.warn("渲染器:: PBR 粗糙度缩放 {} 越界（0.05..4），已忽略", scale);
			return;
		}
		(*state_ptr).pbr_roughness = scale;
	}

	//当前 PBR-lite 粗糙度缩放（第六十四轮新增）
	float Render_Scene::pbr_roughness(void) const
	{
		//未初始化返回默认 1
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_roughness : 1.0f;
	}

	//PBR 金属度（第六十五轮新增）
	void Render_Scene::pbr_metallic_set(float metallic)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		//合法区间 0..1；越界或非数整次忽略
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: PBR 金属度 {} 越界（0..1），已忽略", metallic);
			return;
		}
		(*state_ptr).pbr_metallic = metallic;
	}

	//当前 PBR 金属度（第六十五轮新增）
	float Render_Scene::pbr_metallic(void) const
	{
		//未初始化返回默认 0
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_metallic : 0.0f;
	}

	//PBR 掠射高光能量修正（第六百七十二轮；D1 第一步）
	void Render_Scene::pbr_energy_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		//与原值一致则无需处理
		if ((*state_ptr).pbr_energy == enabled)
			return;
		(*state_ptr).pbr_energy = enabled;
	}

	//多重散射能量补偿（方向九第 716 轮；默认关闭）
	void Render_Scene::pbr_multiscatter_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		//与原值一致则无需处理
		if ((*state_ptr).pbr_multiscatter == enabled)
			return;
		(*state_ptr).pbr_multiscatter = enabled;
	}

	//当前多重散射能量补偿是否开启（方向九第 716 轮）
	bool Render_Scene::pbr_multiscatter(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_multiscatter : false;
	}

	//当前 PBR 掠射高光能量修正是否开启（第六百七十二轮）
	bool Render_Scene::pbr_energy(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_energy : false;
	}

	//PBR 粗糙度绝对口径（第七百轮；D1 收尾）
	void Render_Scene::pbr_roughness_absolute_set(bool enabled)
	{
		//未初始化静默返回
		if (owner == nullptr || !owner->device().valid())
			return;
		//与原值一致则无需处理
		if ((*state_ptr).pbr_roughness_absolute == enabled)
			return;
		(*state_ptr).pbr_roughness_absolute = enabled;
	}

	//当前 PBR 粗糙度绝对口径是否开启（第七百轮）
	bool Render_Scene::pbr_roughness_absolute(void) const
	{
		//未初始化返回默认假
		return owner != nullptr && owner->device().valid() ? (*state_ptr).pbr_roughness_absolute : false;
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
		if (owner == nullptr || !owner->device().valid())
			return;
		//色阶数钳到 [0, 8]
		(*state_ptr).toon_steps = steps > 8 ? 8 : steps;
		//边缘光强度钳到 [0, 1]（非有限值按 0）
		float effective_rim = std::isfinite(rim) ? rim : 0.0f;
		(*state_ptr).toon_rim = effective_rim > 1.0f ? 1.0f : (effective_rim > 0.0f ? effective_rim : 0.0f);
	}

	//当前色阶数（第一百七十四轮新增）
	uint32_t Render_Scene::toon_steps(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->device().valid() ? (*state_ptr).toon_steps : 0;
	}

	//当前边缘光强度（第一百七十四轮新增）
	float Render_Scene::toon_rim(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->device().valid() ? (*state_ptr).toon_rim : 0.0f;
	}

	/*
	设置反向外扩描边（第二百二十八轮）
	宽度钳到 [0, 1]（非有限值按 0），颜色逐分量钳到 [0, 1]（非有限值按 0）；
	描边管线未登记时仅记录参数（绘制项不会额外登记描边项，画面不变）。
	*/
	void Render_Scene::outline_set(float width, const float color[3])
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//外扩宽度钳到 [0, 1]（非有限值按 0 = 关闭）
		float effective_width = std::isfinite(width) ? width : 0.0f;
		(*state_ptr).outline_width = effective_width > 1.0f ? 1.0f : (effective_width > 0.0f ? effective_width : 0.0f);
		//描边色逐分量钳到 [0, 1]（非有限值按 0）
		for (std::size_t index = 0; index < 3; ++index)
		{
			float channel = color != nullptr ? color[index] : 0.0f;
			if (!std::isfinite(channel))
				channel = 0.0f;
			(*state_ptr).outline_color[index] = channel > 1.0f ? 1.0f : (channel > 0.0f ? channel : 0.0f);
		}
	}

	//关闭反向外扩描边（第二百二十八轮新增）
	void Render_Scene::outline_clear(void)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).outline_width = 0.0f;
	}

	//当前外扩宽度（第二百二十八轮新增）
	float Render_Scene::outline_width(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->device().valid() ? (*state_ptr).outline_width : 0.0f;
	}

	//读取当前描边色（第二百二十八轮新增）
	void Render_Scene::outline_color(float out[3]) const
	{
		//出参为空则忽略
		if (out == nullptr)
			return;
		//未初始化写全零（黑）
		if (owner == nullptr || !owner->device().valid())
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = (*state_ptr).outline_color[index];
	}

	//设置暗部色调（第一百七十八轮新增）
	void Render_Scene::toon_shadow_tint_set(float r, float g, float b)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//逐分量归一：非有限值按 1（白 = 关闭），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 1.0f;
			(*state_ptr).toon_shadow_tint[index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
	}

	//读取暗部色调（第一百七十八轮新增）
	void Render_Scene::toon_shadow_tint(float out[3]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return;
		//未初始化时写默认白（关闭）
		const bool ready = owner != nullptr && owner->device().valid();
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = ready ? (*state_ptr).toon_shadow_tint[index] : 1.0f;
	}

	//设置卡通高光阈值（第一百七十九轮新增）
	void Render_Scene::toon_specular_set(float threshold)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//阈值钳到 [0, 1]（非有限值按 0 = 关闭）
		const float effective = std::isfinite(threshold) ? threshold : 0.0f;
		(*state_ptr).toon_specular = effective > 1.0f ? 1.0f : (effective > 0.0f ? effective : 0.0f);
	}

	//读取卡通高光阈值（第一百七十九轮新增）
	float Render_Scene::toon_specular(void) const
	{
		//未初始化返回默认 0（关闭）
		return owner != nullptr && owner->device().valid() ? (*state_ptr).toon_specular : 0.0f;
	}

	//累计实例缓冲上传字节数（第六十一轮新增）
	uint64_t Render_Device::instance_upload_bytes(void) const
	{
		//未初始化返回 0
		return state_ptr != nullptr && state_ptr->initialized ? state_ptr->instance_upload_bytes : 0;
	}

	//读回阴影图（第五十七轮新增）
	bool Render_Device::shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out)
	{
		//前置：已初始化、阴影已开启、阴影目标存在且参数齐备
		if (state_ptr == nullptr || !state_ptr->initialized || !owner->scene().shadow_enabled() || state_ptr->shadow_target == 0
			|| pixels == nullptr || capacity == 0)
			return false;
		//转交渲染目标模块（等在途帧并同步读回打包深度）
		return owner->resource().render_target_readback(state_ptr->shadow_target, pixels, capacity, width_out, height_out);
	}

	//读取清屏色值快照
	void Render_Scene::clear_color_get(float out[4]) const
	{
		if (out == nullptr)
			return;
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = state_ptr->clear_color[index];
	}

	//当前级联数量
	uint32_t Render_Scene::shadow_cascade_count(void) const
	{
		return state_ptr->cascade_count;
	}

	//设置清屏色（RGBA）
	void Render_Scene::clear_color_set(const float color[4])
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || color == nullptr)
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
			(*state_ptr).clear_color[index] = value;
		}
	}
}
