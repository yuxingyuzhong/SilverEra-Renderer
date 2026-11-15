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
	//创建离屏渲染目标

	bool Render_Device::post_chain_set(const uint64_t* handles, uint32_t count)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 后处理链设置失败：尚未初始化");
			return false;
		}
		//若为清除链
		if (count == 0)
		{
			state().post_chain.clear();
			//链已清空 → 逐级专设参数与描边色一并失效
			for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
			{
				state().post_stage_param_valid[stage] = false;
				state().post_stage_color_valid[stage] = false;
				state().post_stage_extra_valid[stage] = false;
			}

			logger.info("渲染器:: 后处理链已清除（回落单级默认后处理）");
			return true;
		}
		//句柄数组为空而级数非零则整链拒绝
		if (handles == nullptr)
		{
			logger.error("渲染器:: 后处理链设置失败：句柄数组为空而级数非零");
			return false;
		}
		/*
		级数上限（处置 R-FRAME-12 之①）
		每级都要成对目标里的一级源 / 宿，且每帧后处理仅占 1 个实例位；超过上限即整链拒绝，
		避免调用方误用超长链导致隐性性能坍塌（上限取 8，足够覆盖常见后处理链）。
		*/
		if (count > POST_CHAIN_MAX_STAGES)
		{
			logger.error("渲染器:: 后处理链设置失败：级数 {} 超过上限 {}", count, POST_CHAIN_MAX_STAGES);
			return false;
		}
		//逐句柄校验已登记（任一句柄未登记即整链拒绝，保持原链不变）
		for (uint32_t index = 0; index < count; ++index)
		{
			//若该级句柄未登记
			if (!impl->pipeline.pipeline_has(handles[index]))
			{
				logger.error("渲染器:: 后处理链设置失败：第 {} 级句柄 {} 未登记", index, handles[index]);
				return false;
			}
		}
		//写入链（保持各句柄的登记顺序不变）
		state().post_chain.assign(handles, handles + count);
		//链已变 → 逐级专设参数与描边色失效（旧下标对应旧级序）
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			state().post_stage_param_valid[stage] = false;
			state().post_stage_color_valid[stage] = false;
				state().post_stage_extra_valid[stage] = false;
		}

		logger.info("渲染器:: 后处理链已设置（{} 级）", count);
		return true;
	}

	uint32_t Render_Device::post_chain_count(void) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则视为空链
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(state().post_chain.size());
	}

	uint32_t Render_Device::post_chain_get(uint64_t* out, uint32_t capacity) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//出参为空 / 未初始化 / 未设链则返回 0
		if (out == nullptr || impl == nullptr || !impl->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>(state().post_chain.size());
		if (count > capacity)
			count = capacity;
		//按当前级序逐级写出（级序可能因管线卸载而压缩）
		for (uint32_t index = 0; index < count; ++index)
			out[index] = state().post_chain[index];
		return count;
	}

	uint32_t Render_Device::post_chain_sample_counts(uint32_t* out, uint32_t capacity) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//出参为空 / 容量为零 / 未初始化则返回 0
		if (out == nullptr || capacity == 0 || impl == nullptr || !impl->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>(state().post_chain.size());
		if (count > capacity)
			count = capacity;
		//逐级查询管线采样数
		for (uint32_t index = 0; index < count; ++index)
			out[index] = impl->pipeline.pipeline_sample_count(state().post_chain[index]);
		return count;
	}

	void Render_Device::post_lut_bind(uint64_t texture_handle)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄 0 表示清除：改写为内置默认白纹理（2×2，使查找级原样输出）
		const uint64_t target_handle = texture_handle == 0 ? impl->resource_domain->state().default_texture : texture_handle;
		//取目标纹理的视图与采样器（非零且未登记则告警并保持原绑定）
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->resource_domain->state().texture.texture_bind_get(target_handle, view, sampler))
		{
			logger.warn("渲染器:: 查找表纹理句柄 {} 无效，绑定忽略", texture_handle);
			return;
		}
		/*
		写满全部帧槽的全部后处理描述符集之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//逐帧槽逐后处理集写入 binding 3（长命资源，一次写满全部槽）
		bool all_bound = true;
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			for (uint32_t index = 0; index < Pipeline_Context::POST_SET_PER_SLOT; ++index)
				//任一集写入失败即记为不完整
				if (!impl->pipeline.post_lut_bind(slot, sampler, view, index))
					all_bound = false;
		//全部写入成功才记录句柄（清除时记录 0）
		if (all_bound)
			impl->resource_domain->state().lut_texture = texture_handle == 0 ? 0 : texture_handle;
		logger.info("渲染器:: 3D LUT 绑定已更新（句柄 {}）；清除时为 0", texture_handle);
	}

	uint64_t Render_Device::post_lut_get(void) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->resource_domain->state().lut_texture;
	}

	void Render_Device::bloom_param_set(float threshold, float knee, float intensity)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：阈值须正、软膝非负且小于阈值、强度须为有限正值
		bool valid = (threshold > 0.0f) && (knee >= 0.0f) && (knee < threshold)
			&& (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 泛光参数非法（阈值 {}、软膝 {}、强度 {}），整次忽略",
				threshold, knee, intensity);
			return;
		}
		state().bloom_param[0] = threshold;
		state().bloom_param[1] = knee;
		state().bloom_param[2] = intensity;
	}

	void Render_Device::post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级泛光参数设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		/*
		参数校验（第一百零八轮放宽）
		三级参数同时供「泛光类」与「色彩 / 色温类」预设使用，后者需要带符号或任意区间，
		故此处只要求三者均为有限值且强度落在 (0,100)；不再施加泛光语义的阈值 / 软膝约束。
		*/
		bool finite = (threshold == threshold) && (knee == knee) && (intensity == intensity);
		bool valid = finite && (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 逐级后处理参数非法（级 {}：阈值 {}、软膝 {}、强度 {}），整次忽略",
				stage, threshold, knee, intensity);
			return;
		}
		state().post_stage_param[stage][0] = threshold;
		state().post_stage_param[stage][1] = knee;
		state().post_stage_param[stage][2] = intensity;
		state().post_stage_param[stage][3] = 0.0f;
		state().post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_param4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		/*
		参数校验（分级调色语义）
		黑位抬升 −1..1、伽马 0.01..10、增益 0..10、饱和度 0..4；
		任一越界或非有限值即整次忽略，保持该级原参数不变。
		*/
		bool finite = (x == x) && (y == y) && (z == z) && (w == w);
		bool valid = finite
			&& x >= -1.0f && x <= 1.0f
			&& y >= 0.01f && y <= 10.0f
			&& z >= 0.0f && z <= 10.0f
			&& w >= 0.0f && w <= 4.0f;
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 逐级四参非法（级 {}：lift {}、gamma {}、gain {}、sat {}），整次忽略",
				stage, x, y, z, w);
			return;
		}
		state().post_stage_param[stage][0] = x;
		state().post_stage_param[stage][1] = y;
		state().post_stage_param[stage][2] = z;
		state().post_stage_param[stage][3] = w;
		state().post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级通用四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		//非有限值整次忽略（保持原值）
		bool finite = (x == x) && (y == y) && (z == z) && (w == w)
			&& x <= 3.4e38f && x >= -3.4e38f && y <= 3.4e38f && y >= -3.4e38f
			&& z <= 3.4e38f && z >= -3.4e38f && w <= 3.4e38f && w >= -3.4e38f;
		if (!finite)
		{
			logger.warn("渲染器:: 逐级通用四参含非有限值（级 {}），整次忽略", stage);
			return;
		}
		state().post_stage_param[stage][0] = x;
		state().post_stage_param[stage][1] = y;
		state().post_stage_param[stage][2] = z;
		state().post_stage_param[stage][3] = w;
		state().post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_color_set(uint32_t stage, float r, float g, float b)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级描边色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0（黑），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			state().post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		//第四分量保留（恒 0）
		state().post_stage_color[stage][3] = 0.0f;
		state().post_stage_color_valid[stage] = true;
	}

	void Render_Device::post_stage_color4_set(uint32_t stage, float r, float g, float b, float a)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四分量色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0，否则钳到 [0, 1]
		const float channels[4] = { r, g, b, a };
		for (std::size_t index = 0; index < 4; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			state().post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		state().post_stage_color_valid[stage] = true;
	}

	void Render_Device::post_stage_extra_set(uint32_t stage, float x, float y, float z, float w)
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//设备推送块上限不足：该组不可用
		if (!state().post_extra_supported)
		{
			logger.warn("渲染器:: 逐级额外四参被拒绝：设备推送块上限不足 144 字节");
			return;
		}
		//级序须落在当前链内（链外下标无意义）
		if (stage >= state().post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级额外四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(state().post_chain.size()));
			return;
		}
		//非有限值整次忽略（保持原值）
		bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
		if (!finite)
		{
			logger.warn("渲染器:: 逐级额外四参含非有限值（级 {}），整次忽略", stage);
			return;
		}
		state().post_stage_extra[stage][0] = x;
		state().post_stage_extra[stage][1] = y;
		state().post_stage_extra[stage][2] = z;
		state().post_stage_extra[stage][3] = w;
		state().post_stage_extra_valid[stage] = true;
	}

	uint32_t Render_Device::post_stage_param_get(uint32_t stage, float out[4]) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//出参为空则无内容可写
		if (out == nullptr)
			return 0;
		//默认写零
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = 0.0f;
		//未初始化 / 级序不在链内 / 该级未设专设值 → 0
		if (impl == nullptr || !impl->initialized || stage >= state().post_chain.size()
			|| stage >= POST_CHAIN_MAX_STAGES || !state().post_stage_param_valid[stage])
			return 0;
		//写出该级专设值
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = state().post_stage_param[stage][each];
		return 1;
	}

	uint32_t Render_Device::post_chain_params_get(float* out, uint32_t capacity) const
	{
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//出参为空或容量不足一级则无内容可写
		if (out == nullptr || capacity < 4)
			return 0;
		//实际可写的级数（受链级数与容量共同限制）
		uint32_t stages = capacity / 4;
		//未初始化时链为空
		uint32_t chain_size = (impl == nullptr || !impl->initialized)
			? 0u : static_cast<uint32_t>(state().post_chain.size());
		if (chain_size > POST_CHAIN_MAX_STAGES)
			chain_size = POST_CHAIN_MAX_STAGES;
		if (stages > chain_size)
			stages = chain_size;
		//逐级写出（未专设的级写 0）
		for (uint32_t stage = 0; stage < stages; ++stage)
		{
			//该级是否设过专设值
			bool valid = state().post_stage_param_valid[stage];
			for (uint32_t each = 0; each < 4; ++each)
				out[stage * 4 + each] = valid ? state().post_stage_param[stage][each] : 0.0f;
		}
		return stages;
	}
}
