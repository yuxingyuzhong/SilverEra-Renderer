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
		(*state_ptr).global_post_param[0] = threshold;
		(*state_ptr).global_post_param[1] = knee;
		(*state_ptr).global_post_param[2] = intensity;
	}

	/*
	建立泛光预设（第 778 轮 · 通用引擎化整改）
	显式创建三个 16F 中间目标（提取 / 模糊 / 叠加）与三条 16F 管线（亮部提取 / 模糊 / 泛光叠加）；
	不订阅后处理链与呈现链 —— 由调用方 post_chain_set 与 render_target_present_chain_set 自行装配。
	若 preset 已持有资源则先释放，故可重复调用（尺寸变化时直接重建）。
	*/
	bool Render_Device::post_bloom_preset_build(Bloom_Preset& preset, const Bloom_Preset_Desc& desc,
		uint32_t width, uint32_t height)
	{
		//未初始化则失败
		if (state_ptr == nullptr || !state_ptr->initialized)
		{
			logger.error("渲染器:: 泛光预设建立失败：尚未初始化");
			return false;
		}
		//释放上一次建立的资源，保证可重复调用
		post_bloom_preset_release(preset);
		//显式尺寸为 0 时取当前客户区
		const uint32_t preset_width = width != 0 ? width : extent_width();
		const uint32_t preset_height = height != 0 ? height : extent_height();
		//若尺寸无效
		if (preset_width == 0 || preset_height == 0)
		{
			logger.warn("渲染器:: 泛光预设建立失败：尺寸为 0");
			return false;
		}
		//按 16F 创建三个中间目标（半分辨率时提取 / 模糊取 1/2 边长，叠加恒取全尺寸）
		for (std::size_t index = 0; index < 3; ++index)
		{
			uint32_t target_width = preset_width;
			uint32_t target_height = preset_height;
			if (desc.half_res && index < 2)
			{
				target_width = preset_width / 2 > 0 ? preset_width / 2 : 1;
				target_height = preset_height / 2 > 0 ? preset_height / 2 : 1;
			}
			if (!owner->resource().render_target_create_format(preset.targets[index], target_width, target_height,
				static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
				static_cast<uint32_t>((*state_ptr).swapchain.depth_format_get())))
			{
				logger.warn("渲染器:: 泛光预设中间目标创建失败（{}×{}，第 {} 个）", target_width, target_height,
					static_cast<uint32_t>(index));
				post_bloom_preset_release(preset);
				return false;
			}
		}
		//登记三条 16F 后处理管线（颜色格式与中间目标一致）
		const char* fragments[3] = { "亮部提取.frag.spv", "模糊.frag.spv", "泛光叠加.frag.spv" };
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (!owner->resource().pipeline_load_ex(preset.pipelines[index], "后处理.vert.spv", fragments[index],
				Blend_Mode::Opaque, false, false, true, false, true, false, 0, 0,
				static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT)))
			{
				logger.warn("渲染器:: 泛光预设管线登记失败（{}）", fragments[index]);
				post_bloom_preset_release(preset);
				return false;
			}
		}
		logger.info("渲染器:: 泛光预设已建立（{}×{}，{}）", preset_width, preset_height,
			desc.half_res ? "半分辨率" : "全分辨率");
		return true;
	}

	/*
	按描述下发泛光预设的三级参数（第 778 轮）
	须先 post_chain_set，且链的前三级恰为本预设的提取 / 模糊 / 叠加管线；顺序不符时整次拒绝并告警。
	*/
	bool Render_Device::post_bloom_preset_params(const Bloom_Preset& preset, const Bloom_Preset_Desc& desc)
	{
		//未初始化则失败
		if (state_ptr == nullptr || !state_ptr->initialized)
		{
			logger.error("渲染器:: 泛光预设参数下发失败：尚未初始化");
			return false;
		}
		const std::vector<uint64_t>& chain = (*state_ptr).post_chain;
		//链须已含本预设的前三级且顺序一致
		if (chain.size() < 3 || chain[0] != preset.pipelines[0] || chain[1] != preset.pipelines[1]
			|| chain[2] != preset.pipelines[2])
		{
			logger.warn("渲染器:: 泛光预设参数下发被拒绝：后处理链前三级不是本预设（请先 post_chain_set）");
			return false;
		}
		//阈值 / 软膝 / 强度按合法域回落默认（与第七百六十一 / 763 轮口径一致）
		const float threshold = (std::isfinite(desc.threshold) && desc.threshold > 0.0f) ? desc.threshold : 0.6f;
		const float knee = (std::isfinite(desc.knee) && desc.knee >= 0.0f) ? desc.knee : 0.1f;
		const float intensity = (std::isfinite(desc.intensity) && desc.intensity > 0.0f && desc.intensity < 100.0f)
			? desc.intensity : 1.0f;
		//半径钳到 1..4（非有限值回落默认 4）；半分辨率按纹素折算后再钳一次
		float radius = std::isfinite(desc.radius) ? desc.radius : 4.0f;
		radius = radius < 1.0f ? 1.0f : (radius > 4.0f ? 4.0f : radius);
		if (desc.half_res)
		{
			radius *= 0.5f;
			if (radius < 1.0f)
				radius = 1.0f;
		}
		post_stage_param_set(0, threshold, knee, 1.0f);
		post_stage_param_set(1, threshold, knee, radius);
		post_stage_param_set(2, threshold, knee, intensity);
		return true;
	}

	/*
	释放泛光预设持有的中间目标与管线（第 778 轮；第 787 轮补引用摘除）
	目标即将归还资源域，故先摘除设备帧路径对它们的引用（目标组），避免下一帧继续引用已释放资源；
	管线卸载本身会从后处理链中摘除对应级。
	*/
	void Render_Device::post_bloom_preset_release(Bloom_Preset& preset)
	{
		//未初始化则只清句柄
		if (state_ptr == nullptr)
		{
			for (std::size_t index = 0; index < 3; ++index)
			{
				preset.targets[index] = 0;
				preset.pipelines[index] = 0;
			}
			return;
		}
		//先判定目标组是否仍引用本预设的中间目标（有则整组清除，由调用方重新装配）
		bool referenced = false;
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (preset.targets[index] == 0)
				continue;
			for (uint64_t extra : (*state_ptr).present_extra)
				if (extra == preset.targets[index])
					referenced = true;
		}
		if (referenced && !(*state_ptr).present_extra.empty())
		{
			(*state_ptr).present_extra.clear();
			logger.info("渲染器:: 泛光预设释放：目标组已清除，调用方需重新装配后处理链");
		}
		for (std::size_t index = 0; index < 3; ++index)
		{
			//中间目标交还资源域
			if (preset.targets[index] != 0)
				owner->resource().render_target_release(preset.targets[index]);
			//管线交还资源域（内部摘除后处理链引用）
			if (preset.pipelines[index] != 0)
				owner->resource().pipeline_unload(preset.pipelines[index]);
			preset.targets[index] = 0;
			preset.pipelines[index] = 0;
		}
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

	/*
	指定后处理链某一级的全局回落色调算子（第 781 轮）
	该级的逐像素覆盖仍来自场景 alpha（材质级），本值只用于未覆盖像素；越界按 0（ACES）。
	*/
	void Render_Device::post_stage_tone_operator_set(uint32_t stage, uint32_t tone_operator)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//级序须落在当前链内
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 回落色调算子设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>((*state_ptr).post_chain.size()));
			return;
		}
		//写入第四分量并标记该级参数有效（前三参保持原值 / 默认 0）
		(*state_ptr).post_stage_param[stage][3] = static_cast<float>(tone_operator <= 5 ? tone_operator : 0u);
		(*state_ptr).post_stage_param_valid[stage] = true;
	}

	/*
	读取后处理链某一级生效的色调算子（第 805 轮）
	口径：该级有专设参数时取其第四分量（0..5）；未专设时回落全局默认算子；
	级序无效（未初始化 / 链外）返回 0，与"越界按 ACES"一致。
	*/
	uint32_t Render_Device::post_stage_tone_operator_get(uint32_t stage) const
	{
		//未初始化或级序无效则按 0
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		if (stage >= (*state_ptr).post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
			return 0;
		//该级已专设则取专设值
		if ((*state_ptr).post_stage_param_valid[stage])
		{
			float value = (*state_ptr).post_stage_param[stage][3];
			if (!(value == value) || value < 0.0f || value > 5.0f)
				return 0;
			return static_cast<uint32_t>(value + 0.5f);
		}
		//否则回落全局默认算子
		return state_ptr->hdr_tone_operator <= 5 ? state_ptr->hdr_tone_operator : 0;
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
