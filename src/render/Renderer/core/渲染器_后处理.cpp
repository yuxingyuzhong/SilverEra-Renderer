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
	//创建离屏渲染目标

	bool Render_Device::post_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
		{
			logger.error("渲染器:: 后处理链设置失败：尚未初始化");
			return false;
		}
		//若为清除链
		if (count == 0)
		{
			(*state_ptr).post_chain.clear();
			//链已清空 → 逐级专设参数与描边色一并失效
			for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
			{
				(*state_ptr).post_stage_param_valid[stage] = false;
				(*state_ptr).post_stage_color_valid[stage] = false;
				(*state_ptr).post_stage_extra_valid[stage] = false;
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
			if (!owner->resource().pipeline_valid(handles[index]))
			{
				logger.error("渲染器:: 后处理链设置失败：第 {} 级句柄 {} 未登记", index, handles[index]);
				return false;
			}
		}
		//写入链（保持各句柄的登记顺序不变）
		(*state_ptr).post_chain.assign(handles, handles + count);
		//链已变 → 逐级专设参数与描边色失效（旧下标对应旧级序）
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			(*state_ptr).post_stage_param_valid[stage] = false;
			(*state_ptr).post_stage_color_valid[stage] = false;
				(*state_ptr).post_stage_extra_valid[stage] = false;
		}

		logger.info("渲染器:: 后处理链已设置（{} 级）", count);
		return true;
	}

	uint32_t Render_Device::post_chain_count(void) const
	{
		//未初始化则视为空链
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return static_cast<uint32_t>((*state_ptr).post_chain.size());
	}

	uint32_t Render_Device::post_chain_get(uint64_t* out, uint32_t capacity) const
	{
		//出参为空 / 未初始化 / 未设链则返回 0
		if (out == nullptr || state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>((*state_ptr).post_chain.size());
		if (count > capacity)
			count = capacity;
		//按当前级序逐级写出（级序可能因管线卸载而压缩）
		for (uint32_t index = 0; index < count; ++index)
			out[index] = (*state_ptr).post_chain[index];
		return count;
	}

	uint32_t Render_Device::post_chain_sample_counts(uint32_t* out, uint32_t capacity) const
	{
		//出参为空 / 容量为零 / 未初始化则返回 0
		if (out == nullptr || capacity == 0 || state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>((*state_ptr).post_chain.size());
		if (count > capacity)
			count = capacity;
		//逐级查询管线采样数
		for (uint32_t index = 0; index < count; ++index)
			out[index] = owner->resource().pipeline_sample_count((*state_ptr).post_chain[index]);
		return count;
	}

	void Render_Device::post_lut_bind(uint64_t texture_handle)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//转交资源域完成纹理解析、逐槽绑定与句柄记录
		owner->resource().post_lut_bind(texture_handle, state_ptr->config.frame_overlap);
	}

	uint64_t Render_Device::post_lut_get(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return owner->resource().post_lut_get();
	}

	void Render_Device::bloom_param_set(float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
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
		(*state_ptr).bloom_param[0] = threshold;
		(*state_ptr).bloom_param[1] = knee;
		(*state_ptr).bloom_param[2] = intensity;
	}

	void Render_Device::post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级泛光参数设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
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
		(*state_ptr).post_stage_param[stage][0] = threshold;
		(*state_ptr).post_stage_param[stage][1] = knee;
		(*state_ptr).post_stage_param[stage][2] = intensity;
		(*state_ptr).post_stage_param[stage][3] = 0.0f;
		(*state_ptr).post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_param4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
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
		(*state_ptr).post_stage_param[stage][0] = x;
		(*state_ptr).post_stage_param[stage][1] = y;
		(*state_ptr).post_stage_param[stage][2] = z;
		(*state_ptr).post_stage_param[stage][3] = w;
		(*state_ptr).post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级通用四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
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
		(*state_ptr).post_stage_param[stage][0] = x;
		(*state_ptr).post_stage_param[stage][1] = y;
		(*state_ptr).post_stage_param[stage][2] = z;
		(*state_ptr).post_stage_param[stage][3] = w;
		(*state_ptr).post_stage_param_valid[stage] = true;
	}

	void Render_Device::post_stage_color_set(uint32_t stage, float r, float g, float b)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级描边色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0（黑），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			(*state_ptr).post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		//第四分量保留（恒 0）
		(*state_ptr).post_stage_color[stage][3] = 0.0f;
		(*state_ptr).post_stage_color_valid[stage] = true;
	}

	void Render_Device::post_stage_color4_set(uint32_t stage, float r, float g, float b, float a)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四分量色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0，否则钳到 [0, 1]
		const float channels[4] = { r, g, b, a };
		for (std::size_t index = 0; index < 4; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			(*state_ptr).post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		(*state_ptr).post_stage_color_valid[stage] = true;
	}

	void Render_Device::post_stage_extra_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//设备推送块上限不足：该组不可用
		if (!(*state_ptr).post_extra_supported)
		{
			logger.warn("渲染器:: 逐级额外四参被拒绝：设备推送块上限不足 144 字节");
			return;
		}
		//级序须落在当前链内（链外下标无意义）
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级额外四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
			return;
		}
		//非有限值整次忽略（保持原值）
		bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
		if (!finite)
		{
			logger.warn("渲染器:: 逐级额外四参含非有限值（级 {}），整次忽略", stage);
			return;
		}
		(*state_ptr).post_stage_extra[stage][0] = x;
		(*state_ptr).post_stage_extra[stage][1] = y;
		(*state_ptr).post_stage_extra[stage][2] = z;
		(*state_ptr).post_stage_extra[stage][3] = w;
		(*state_ptr).post_stage_extra_valid[stage] = true;
	}

	uint32_t Render_Device::post_stage_param_get(uint32_t stage, float out[4]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return 0;
		//默认写零
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = 0.0f;
		//未初始化 / 级序不在链内 / 该级未设专设值 → 0
		if (state_ptr == nullptr || !state_ptr->initialized || stage >= (*state_ptr).post_chain.size()
			|| stage >= POST_CHAIN_MAX_STAGES || !(*state_ptr).post_stage_param_valid[stage])
			return 0;
		//写出该级专设值
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = (*state_ptr).post_stage_param[stage][each];
		return 1;
	}

	uint32_t Render_Device::post_chain_params_get(float* out, uint32_t capacity) const
	{
		//出参为空或容量不足一级则无内容可写
		if (out == nullptr || capacity < 4)
			return 0;
		//实际可写的级数（受链级数与容量共同限制）
		uint32_t stages = capacity / 4;
		//未初始化时链为空
		uint32_t chain_size = (state_ptr == nullptr || !state_ptr->initialized)
			? 0u : static_cast<uint32_t>((*state_ptr).post_chain.size());
		if (chain_size > POST_CHAIN_MAX_STAGES)
			chain_size = POST_CHAIN_MAX_STAGES;
		if (stages > chain_size)
			stages = chain_size;
		//逐级写出（未专设的级写 0）
		for (uint32_t stage = 0; stage < stages; ++stage)
		{
			//该级是否设过专设值
			bool valid = (*state_ptr).post_stage_param_valid[stage];
			for (uint32_t each = 0; each < 4; ++each)
				out[stage * 4 + each] = valid ? (*state_ptr).post_stage_param[stage][each] : 0.0f;
		}
		return stages;
	}
}
