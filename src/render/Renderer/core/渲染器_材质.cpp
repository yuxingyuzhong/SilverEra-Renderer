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
	//设置后续绘制所用混合档（切档触发图形管线重建）

	bool Render_Resource::material_load_mtl(std::vector<Material_Handle>& handles_out, const std::string& path,
		std::vector<std::string>* names_out)
	{
		//默认无输出（句柄与名称都清空）
		handles_out.clear();
		if (names_out != nullptr)
			names_out->clear();
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: MTL 材质加载失败：尚未初始化");
			return false;
		}
		//解析材质库
		std::vector<Mtl_Material> 材质项;
		if (!mtl_materials_load(path, 材质项))
			return false;
		//MTL 所在目录（保留 UTF-8 编码；贴图路径直接前缀拼接）
		const std::size_t cut = path.find_last_of("/\\");
		const std::string 目录 = (cut == std::string::npos) ? std::string() : path.substr(0, cut + 1);

		//本次已创建的资源（失败时整体回退）
		std::vector<uint64_t> 纹理;
		std::vector<Material_Handle> 已建;
		//逐项创建
		for (const Mtl_Material& 项 : 材质项)
		{
			Material_Data 描述;
			//反照率取 Kd，第四分量为不透明度
			描述.tint_override = true;
			描述.tint[0] = 项.diffuse[0];
			描述.tint[1] = 项.diffuse[1];
			描述.tint[2] = 项.diffuse[2];
			描述.tint[3] = 项.opacity;
			/*
			PBR 近似
			Ns 是镜面指数：0 最粗糙、1000 最光滑；按 roughness = √(2 / (Ns + 2)) 换算，
			下限取 0.06（材质层的合法域是 (0.05, 4]，恰好 0.05 会被判非法）。
			OBJ 材质库没有金属度语义，一律按电介质（0）处理。
			*/
			描述.pbr_override = true;
			float 粗糙 = std::sqrt(2.0f / (项.shininess + 2.0f));
			if (粗糙 < 0.06f)
				粗糙 = 0.06f;
			if (粗糙 > 4.0f)
				粗糙 = 4.0f;
			描述.pbr_roughness = 粗糙;
			描述.pbr_metallic = 0.0f;
			//自发光强度取 Ke 的亮度（钳到 [0,1]）
			float 亮度 = 0.2126f * 项.emissive[0] + 0.7152f * 项.emissive[1] + 0.0722f * 项.emissive[2];
			if (亮度 < 0.0f)
				亮度 = 0.0f;
			if (亮度 > 1.0f)
				亮度 = 1.0f;
			描述.emissive = 亮度;
			//半透明：材质层没有整体透明度字段，改用 alpha 裁剪近似
			if (项.opacity < 0.999f)
				描述.alpha_cutoff = 0.5f;
			//漫反射贴图（解码失败不阻断材质创建）
			if (!项.diffuse_map.empty())
			{
				uint64_t 贴图 = 0;
				if (texture_load_file(贴图, 目录 + 项.diffuse_map))
				{
					描述.texture = 贴图;
					纹理.push_back(贴图);
				}
				else
					logger.warn("渲染器:: MTL 漫反射贴图加载失败（{}），该材质按无贴图创建", 项.diffuse_map);
			}
			//法线 / 凹凸贴图
			if (!项.normal_map.empty())
			{
				uint64_t 贴图 = 0;
				if (texture_load_file(贴图, 目录 + 项.normal_map))
				{
					描述.normal_texture = 贴图;
					描述.normal_strength = 1.0f;
					纹理.push_back(贴图);
				}
				else
					logger.warn("渲染器:: MTL 法线贴图加载失败（{}），该材质按无法线创建", 项.normal_map);
			}
			//创建材质
			Material_Handle 材质 = 0;
			if (!material_load(材质, 描述))
			{
				logger.warn("渲染器:: MTL 材质 {} 创建失败，回退本次已建资源", 项.name);
				//回退材质
				for (Material_Handle 已建项 : 已建)
					material_unload(已建项);
				//回退纹理
				for (uint64_t 贴图 : 纹理)
					texture_unload(贴图);
				handles_out.clear();
				//名称输出同步清空
				if (names_out != nullptr)
					names_out->clear();
				return false;
			}
			已建.push_back(材质);
			handles_out.push_back(材质);
			//名称与句柄同序输出（第四百六十七轮）
			if (names_out != nullptr)
				names_out->push_back(项.name);
		}
		return true;
	}

	bool Render_Resource::material_instance_validate(const Material_Instance_Data& data) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//模板句柄须为已登记材质
		if (data.模板 == 0 || data.模板 > (*state_ptr).materials.size() || !(*state_ptr).materials[data.模板 - 1].valid)
			return false;
		//覆盖底色：四分量有限且落在 [0,1]
		if (data.use_tint)
		{
			for (std::size_t index = 0; index < 4; ++index)
			{
				const float value = data.tint[index];
				if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
					return false;
			}
		}
		//覆盖自发光：有限且落在 [0,1]
		if (data.use_emissive)
		{
			if (!std::isfinite(data.emissive) || data.emissive < 0.0f || data.emissive > 1.0f)
				return false;
		}
		//逐实例卡通（第四百七十一轮）：色阶 0..8、边缘光 / 高光阈值 0..1
		if (data.use_toon)
		{
			if (!std::isfinite(data.toon_steps) || data.toon_steps < 0.0f || data.toon_steps > 8.0f)
				return false;
			if (!std::isfinite(data.toon_rim) || data.toon_rim < 0.0f || data.toon_rim > 1.0f)
				return false;
			if (!std::isfinite(data.toon_specular) || data.toon_specular < 0.0f || data.toon_specular > 1.0f)
				return false;
		}
		//逐实例 PBR（第五百六十四轮）：粗糙度 (0.05, 4]、金属度 [0, 1]
		if (data.use_pbr)
		{
			if (!std::isfinite(data.pbr_roughness) || data.pbr_roughness <= 0.05f || data.pbr_roughness > 4.0f)
				return false;
			if (!std::isfinite(data.pbr_metallic) || data.pbr_metallic < 0.0f || data.pbr_metallic > 1.0f)
				return false;
		}
		//逐实例清漆（第五百九十四轮）：强度 (0, 1]、粗糙度 [0.03, 1]
		if (data.use_clearcoat)
		{
			if (!std::isfinite(data.clear_coat) || data.clear_coat <= 0.0f || data.clear_coat > 1.0f)
				return false;
			if (!std::isfinite(data.clear_coat_roughness) || data.clear_coat_roughness < 0.03f || data.clear_coat_roughness > 1.0f)
				return false;
		}
		return true;
	}

	bool Render_Resource::material_load(Material_Handle& handle_out, const Material_Data& data)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 材质登记失败：尚未初始化");
			return false;
		}
		//待登记记录
		Material_Record record{};
		record.pipeline = data.pipeline;
		record.texture = data.texture;
		/*
		逐物体 PBR（第六十八轮）
		与非数 / 越界字段一律按「不覆盖」处理，避免把非法参数写进实例属性；
		合法域：粗糙度 (0.05, 4]、金属度 [0, 1]。
		*/
		record.pbr_override = data.pbr_override
			&& data.pbr_roughness > 0.05f && data.pbr_roughness <= 4.0f
			&& data.pbr_metallic >= 0.0f && data.pbr_metallic <= 1.0f;
		if (record.pbr_override)
		{
			record.pbr_roughness = data.pbr_roughness;
			record.pbr_metallic = data.pbr_metallic;
		}
		//若声明覆盖但参数非法则告警（按不覆盖登记，保持默认画面）
		if (data.pbr_override && !record.pbr_override)
			logger.warn("渲染器:: 材质 PBR 参数非法（粗糙度 {}、金属度 {}），按不覆盖登记",
				data.pbr_roughness, data.pbr_metallic);
		/*
		逐物体 PBR 特性覆盖（第 777 轮）
		掩码只取低 6 位（bit5 = 光泽度映射）；仅当 pbr_gloss 合法（0..1）时 bit5 才生效。
		光泽度映射按 roughness = clamp(1 − gloss, 0.05, 1) 覆盖本材质粗糙度，并强制登记为逐物体 PBR 覆盖。
		*/
		record.pbr_features_override = data.pbr_features_override;
		record.pbr_features = data.pbr_features & PBR_FEATURE_ALL;
		//逐材质色调算子（第 779 轮；0..4 有效，其余按继承全局）
		record.tone_operator = data.tone_operator <= 4 ? data.tone_operator : 0xFF;
		//色调编号借用 alpha 传递：仅默认管线 ＋ 不透明档 ＋ 未启用覆盖率写入时才安全（第 780 轮）
		record.tone_alpha_opaque = data.pipeline == 0 && data.blend == Blend_Mode::Opaque
			&& !data.alpha_to_coverage;
		if (record.pbr_features_override && (record.pbr_features & 0x20u) != 0
			&& std::isfinite(data.pbr_gloss) && data.pbr_gloss >= 0.0f && data.pbr_gloss <= 1.0f)
		{
			record.pbr_gloss = data.pbr_gloss;
			//光泽度映射改写粗糙度，故本材质必然走逐物体覆盖路径
			record.pbr_override = true;
			record.pbr_roughness = std::max(1.0f - data.pbr_gloss, 0.05f);
		}
		/*
		逐物体卡通参数（第四百零八轮）
		合法域：色阶数 [0, 8]、边缘光 [0, 1]、卡通高光阈值 [0, 1]；
		任一项非有限或越界则按「不覆盖」登记（不把脏值写进实例属性），覆盖标志本身不受影响。
		*/
		record.toon_override = data.toon_override
			&& std::isfinite(data.toon_steps) && data.toon_steps >= 0.0f && data.toon_steps <= 8.0f
			&& std::isfinite(data.toon_rim) && data.toon_rim >= 0.0f && data.toon_rim <= 1.0f
			&& std::isfinite(data.toon_specular) && data.toon_specular >= 0.0f && data.toon_specular <= 1.0f;
		if (record.toon_override)
		{
			record.toon_steps = data.toon_steps;
			record.toon_rim = data.toon_rim;
			record.toon_specular = data.toon_specular;
			/*
			暗部色调（第四百零九轮）
			各分量合法域 [0, 1]；非有限或越界按 1（不色偏），不影响卡通覆盖标志本身。
			*/
			for (std::size_t index = 0; index < 3; ++index)
				record.toon_shadow_tint[index] = (std::isfinite(data.toon_shadow_tint[index])
					&& data.toon_shadow_tint[index] >= 0.0f && data.toon_shadow_tint[index] <= 1.0f)
					? data.toon_shadow_tint[index] : 1.0f;
		}
		//若声明卡通覆盖但参数非法则告警
		if (data.toon_override && !record.toon_override)
			logger.warn("渲染器:: 材质卡通参数非法（色阶 {}、边缘光 {}、高光阈值 {}），按不覆盖登记",
				data.toon_steps, data.toon_rim, data.toon_specular);
		/*
		逐物体反照率色调（第六十九轮）
		四个分量都在 [0, 1] 才视为覆盖；否则按「不覆盖」登记，不把脏值送进推送常量。
		*/
		record.tint_override = data.tint_override
			&& data.tint[0] >= 0.0f && data.tint[0] <= 1.0f
			&& data.tint[1] >= 0.0f && data.tint[1] <= 1.0f
			&& data.tint[2] >= 0.0f && data.tint[2] <= 1.0f
			&& data.tint[3] >= 0.0f && data.tint[3] <= 1.0f;
		if (record.tint_override)
			for (std::size_t index = 0; index < 4; ++index)
				record.tint[index] = data.tint[index];
		//若声明色调覆盖但参数非法则告警
		if (data.tint_override && !record.tint_override)
			logger.warn("渲染器:: 材质色调非法（{} / {} / {} / {}），按不覆盖登记",
				data.tint[0], data.tint[1], data.tint[2], data.tint[3]);
		/*
		逐物体自发光（第七十轮）
		合法域 [0, 8]；越界 / 非数按 0（无自发光）登记，不把脏值写进实例属性。
		*/
		record.emissive = (data.emissive >= 0.0f && data.emissive <= 8.0f) ? data.emissive : 0.0f;
		if (!(data.emissive >= 0.0f && data.emissive <= 8.0f))
			logger.warn("渲染器:: 材质自发光 {} 越界（合法域 [0, 8]），按 0 登记", data.emissive);
		/*
		PBR 贴图化（第一百二十四轮）
		法线强度合法域 (0, 4]；越界 / 非数按 0（不启用）登记。
		贴图句柄原样记录，绑定阶段无效句柄回落内置默认白纹理。
		*/
		record.normal_texture = data.normal_texture;
		record.roughness_texture = data.roughness_texture;
		record.emissive_texture = data.emissive_texture;
		record.ao_texture = data.ao_texture;
		record.metallic_texture = data.metallic_texture;
		/*
		贴图 UV 变换（第一百七十一轮）
		非有限值按默认登记（缩放 1、偏移 0），避免把脏值写进推送常量。
		*/
		for (std::size_t index = 0; index < 2; ++index)
		{
			record.uv_scale[index] = std::isfinite(data.uv_scale[index]) ? data.uv_scale[index] : 1.0f;
			record.uv_offset[index] = std::isfinite(data.uv_offset[index]) ? data.uv_offset[index] : 0.0f;
		}
		/*
		UV 旋转与 Alpha 裁剪（第一百八十三轮）
		旋转角非有限值按 0（不旋转）；裁剪阈值非有限值或越界按 0（关闭），避免脏值影响片元丢弃。
		*/
		record.uv_rotation = std::isfinite(data.uv_rotation) ? data.uv_rotation : 0.0f;
		record.alpha_cutoff = (std::isfinite(data.alpha_cutoff) && data.alpha_cutoff > 0.0f && data.alpha_cutoff <= 1.0f)
			? data.alpha_cutoff : 0.0f;
		//清漆层（第二百三十三轮；强度非有限或越界按 0，粗糙度非有限或越界按 0.15）
		record.clear_coat = (std::isfinite(data.clear_coat) && data.clear_coat > 0.0f && data.clear_coat <= 1.0f)
			? data.clear_coat : 0.0f;
		record.clear_coat_roughness = (std::isfinite(data.clear_coat_roughness)
			&& data.clear_coat_roughness >= 0.03f && data.clear_coat_roughness <= 1.0f)
			? data.clear_coat_roughness : 0.15f;
		record.normal_strength = (data.normal_strength > 0.0f && data.normal_strength <= 4.0f)
			? data.normal_strength : 0.0f;
		if (data.normal_strength != 0.0f && record.normal_strength == 0.0f)
			logger.warn("渲染器:: 材质法线强度 {} 越界（合法域 (0, 4]），按 0 登记", data.normal_strength);
		//覆盖率写入（第一百二十六轮；仅 MSAA > 1× 时由管线侧生效）
		record.alpha_to_coverage = data.alpha_to_coverage;
		//双面渲染（第一百七十二轮；true 时使用关闭背面剔除的共享管线）
		record.double_sided = data.double_sided;
		//深度写入（第一百七十三轮；false 时使用不写深度的共享管线）
		record.depth_write = data.depth_write;
		/*
		若未指定管线且需要共享管线：混合档非不透明，或显式要求覆盖率写入 / 双面渲染 / 关闭深度写入
		（覆盖率写入、双面渲染与关闭深度写入均不能沿用默认场景管线，故不透明档也须登记专用管线）
		*/
		if (record.pipeline == 0 && (data.blend != Blend_Mode::Opaque
			|| data.alpha_to_coverage || data.double_sided || !data.depth_write))
		{
			/*
			组合键缓存（第一百七十三轮扩为八组合）
			下标 = 档位序 × 8 + 组合位（bit2 = 关闭深度写入、bit1 = 覆盖率写入、bit0 = 双面渲染）；
			同组合材质共享一条具名管线，不足则扩容。
			*/
			std::size_t index = static_cast<std::size_t>(data.blend) * 8
				+ (data.depth_write ? 0 : 4) + (data.alpha_to_coverage ? 2 : 0) + (data.double_sided ? 1 : 0);
			std::vector<uint64_t>& table = (*state_ptr).material_pipelines;
			if (table.size() <= index)
				table.resize(index + 1, 0);
			//该组合尚未登记则以默认场景着色器对登记一条（此后同组合材质共享之）
			if (table[index] == 0)
			{
				if (!owner->resource().pipeline_load_ex(table[index], "场景.vert.spv", "场景.frag.spv",
					data.blend, data.alpha_to_coverage, data.double_sided, data.depth_write))
				{
					logger.error("渲染器:: 材质登记失败：具名管线登记失败");
					return false;
				}
			}
			//材质使用该组合的共享管线（所有权归门面，卸载不释放）
			record.pipeline = table[index];
		}
		//标记记录有效
		record.valid = true;
		//分配句柄：优先复用第一个空槽，否则追加
		Material_Handle handle = 0;
		for (std::size_t index = 0; index < (*state_ptr).materials.size(); ++index)
		{
			//若该槽已空
			if (!(*state_ptr).materials[index].valid)
			{
				(*state_ptr).materials[index] = record;
				handle = static_cast<Material_Handle>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			(*state_ptr).materials.push_back(record);
			handle = static_cast<Material_Handle>((*state_ptr).materials.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 材质已登记（句柄 {}，管线 {}，纹理 {}）", handle, record.pipeline, record.texture);
		return true;
	}

	void Render_Resource::material_unload(Material_Handle handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//句柄为零或越界即视为无需卸载（幂等）
		if (handle == 0 || handle > (*state_ptr).materials.size())
			return;
		//取目标记录
		Material_Record& record = (*state_ptr).materials[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//复位记录（标记槽为空；具名管线为门面共享资源，不随材质卸载释放）
		record = Material_Record{};
		logger.info("渲染器:: 材质已卸载（句柄 {}）", handle);
	}

	void Render_Resource::material_bind_set(Material_Handle handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//句柄为零则回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖
		if (handle == 0)
		{
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			material_emissive_clear();
			(*state_ptr).material_normal_strength = 0.0f;
			(*state_ptr).material_uv_scale[0] = 1.0f;
			(*state_ptr).material_uv_scale[1] = 1.0f;
			(*state_ptr).material_uv_offset[0] = 0.0f;
			(*state_ptr).material_uv_offset[1] = 0.0f;
			(*state_ptr).material_uv_rotation = 0.0f;
			(*state_ptr).material_alpha_cutoff = 0.0f;
			(*state_ptr).material_clear_coat = 0.0f;
			(*state_ptr).material_clear_coat_roughness = 0.15f;
			//清除逐物体卡通覆盖（第四百零八轮）
			(*state_ptr).material_toon_active = false;
			(*state_ptr).material_toon_steps = 0.0f;
			(*state_ptr).material_toon_rim = 0.0f;
			(*state_ptr).material_toon_specular = 0.0f;
			//清除逐物体暗部色调（第四百零九轮；回落 (1,1,1) 不色偏）
			(*state_ptr).material_toon_tint[0] = 1.0f;
			(*state_ptr).material_toon_tint[1] = 1.0f;
			(*state_ptr).material_toon_tint[2] = 1.0f;
			return;
		}
		//句柄无效则回落默认并告警
		if (handle > (*state_ptr).materials.size() || !(*state_ptr).materials[handle - 1].valid)
		{
			logger.warn("渲染器:: 材质句柄 {} 无效，回落默认管线与默认纹理", handle);
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			material_emissive_clear();
			(*state_ptr).material_normal_strength = 0.0f;
			(*state_ptr).material_uv_scale[0] = 1.0f;
			(*state_ptr).material_uv_scale[1] = 1.0f;
			(*state_ptr).material_uv_offset[0] = 0.0f;
			(*state_ptr).material_uv_offset[1] = 0.0f;
			(*state_ptr).material_uv_rotation = 0.0f;
			(*state_ptr).material_alpha_cutoff = 0.0f;
			(*state_ptr).material_clear_coat = 0.0f;
			(*state_ptr).material_clear_coat_roughness = 0.15f;
			//清除逐物体卡通覆盖（第四百零八轮）
			(*state_ptr).material_toon_active = false;
			(*state_ptr).material_toon_steps = 0.0f;
			(*state_ptr).material_toon_rim = 0.0f;
			(*state_ptr).material_toon_specular = 0.0f;
			//清除逐物体暗部色调（第四百零九轮；回落 (1,1,1) 不色偏）
			(*state_ptr).material_toon_tint[0] = 1.0f;
			(*state_ptr).material_toon_tint[1] = 1.0f;
			(*state_ptr).material_toon_tint[2] = 1.0f;
			return;
		}
		//取目标记录
		const Material_Record& record = (*state_ptr).materials[handle - 1];
		//绑定材质管线与纹理（管线句柄未登记 / 纹理句柄无效时各自内部回落默认）
		pipeline_bind_set(record.pipeline);
		texture_bind_set(record.texture);
		/*
		PBR 贴图化（第一百二十四轮）
		法线 / 粗糙度贴图无效或未配置时回落内置默认白纹理（采样为 (1,1,1)，画面不变）；
		法线强度经推送常量 offset 80 的 x 分量下发给场景片元。
		*/
		(*state_ptr).material_normal_strength = record.normal_strength;
		{
			//默认白纹理（兜底视图与采样器）
			VkImageView white_view = VK_NULL_HANDLE;
			VkSampler white_sampler = VK_NULL_HANDLE;
			(*state_ptr).texture.texture_bind_get((*state_ptr).default_texture, white_view, white_sampler);
			/*
			材质贴图采样器（第一百七十轮）
			纹理模块全部纹理共用同一采样器，故五个贴图只取用这一个采样器。
			*/
			VkSampler material_sampler = white_sampler;
			//法线贴图（无效则用默认白纹理）
			VkImageView normal_view = white_view;
			if (record.normal_texture != 0)
				(*state_ptr).texture.texture_bind_get(record.normal_texture, normal_view, material_sampler);
			//粗糙度贴图（无效则用默认白纹理）
			VkImageView roughness_view = white_view;
			if (record.roughness_texture != 0)
				(*state_ptr).texture.texture_bind_get(record.roughness_texture, roughness_view, material_sampler);
			//自发光颜色贴图（第一百六十八轮；无效则用默认白纹理）
			VkImageView emissive_view = white_view;
			if (record.emissive_texture != 0)
				(*state_ptr).texture.texture_bind_get(record.emissive_texture, emissive_view, material_sampler);
			//环境光遮蔽贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView ao_view = white_view;
			if (record.ao_texture != 0)
				(*state_ptr).texture.texture_bind_get(record.ao_texture, ao_view, material_sampler);
			//金属度贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView metallic_view = white_view;
			if (record.metallic_texture != 0)
				(*state_ptr).texture.texture_bind_get(record.metallic_texture, metallic_view, material_sampler);
			/*
			一次性写入全部材质贴图（第一百七十轮）
			任一视图取用失败则整次跳过，避免把空句柄写进描述符集。
			*/
			if (normal_view != VK_NULL_HANDLE && roughness_view != VK_NULL_HANDLE
				&& emissive_view != VK_NULL_HANDLE && ao_view != VK_NULL_HANDLE && metallic_view != VK_NULL_HANDLE)
			{
				for (uint32_t slot = 0; slot < core->config.frame_overlap; ++slot)
					(*state_ptr).pipeline.material_textures_bind(slot, material_sampler,
						normal_view, roughness_view, emissive_view, ao_view, metallic_view);
				//同步材质贴图状态（逐绘制项采样集按该快照写入）
				(*state_ptr).material_views[0] = normal_view;
				(*state_ptr).material_views[1] = roughness_view;
				(*state_ptr).material_views[2] = emissive_view;
				(*state_ptr).material_views[3] = ao_view;
				(*state_ptr).material_views[4] = metallic_view;
			}
		}
		//按记录的 PBR 覆盖标志设置 / 清除逐物体材质（第六十八轮）
		if (record.pbr_override)
			(*state_ptr).material_pbr_active = true;
		else
			(*state_ptr).material_pbr_active = false;
		(*state_ptr).material_pbr_roughness = record.pbr_override ? record.pbr_roughness : 1.0f;
		(*state_ptr).material_pbr_metallic = record.pbr_override ? record.pbr_metallic : 0.0f;
		//同步逐物体 PBR 特性覆盖（第 777 轮；无覆盖时清零 → 片元回落全局开关）
		(*state_ptr).material_pbr_features_active = record.pbr_features_override;
		(*state_ptr).material_pbr_features = record.pbr_features_override ? record.pbr_features : 0u;
		//逐材质色调算子（第 779 轮；0xFF = 继承全局默认）
		//透明 / 覆盖率写入材质不携带编号（第 780 轮；避免改写 alpha 破坏混合）
		(*state_ptr).material_tone_operator = record.tone_alpha_opaque ? record.tone_operator : 0xFF;
		//按记录的卡通覆盖标志同步逐物体卡通参数（第四百零八轮；无覆盖时三项清零即回落全局）
		(*state_ptr).material_toon_active = record.toon_override;
		(*state_ptr).material_toon_steps = record.toon_override ? record.toon_steps : 0.0f;
		(*state_ptr).material_toon_rim = record.toon_override ? record.toon_rim : 0.0f;
		(*state_ptr).material_toon_specular = record.toon_override ? record.toon_specular : 0.0f;
		//逐物体暗部色调（第四百零九轮；无覆盖时回落 (1,1,1) 不色偏）
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).material_toon_tint[index] = record.toon_override ? record.toon_shadow_tint[index] : 1.0f;
		//按记录同步逐物体色调（第六十九轮）
		(*state_ptr).material_tint_active = record.tint_override;
		for (std::size_t index = 0; index < 4; ++index)
			(*state_ptr).material_tint[index] = record.tint_override ? record.tint[index] : 1.0f;
		//同步逐物体自发光（第七十轮）
		(*state_ptr).material_emissive = record.emissive;
		//同步贴图 UV 变换（第一百七十一轮；材质默认 (1,1)/(0,0) 即恒等）
		for (std::size_t index = 0; index < 2; ++index)
		{
			(*state_ptr).material_uv_scale[index] = record.uv_scale[index];
			(*state_ptr).material_uv_offset[index] = record.uv_offset[index];
		}
		//同步 UV 旋转与 Alpha 裁剪（第一百八十三轮；默认 0 即关闭）
		(*state_ptr).material_uv_rotation = record.uv_rotation;
		(*state_ptr).material_alpha_cutoff = record.alpha_cutoff;
		//同步清漆层（第二百三十三轮；默认 0 即关闭；强度钳 [0,1]、粗糙度钳 [0.03,1]）
		float coat = std::isfinite(record.clear_coat) ? record.clear_coat : 0.0f;
		(*state_ptr).material_clear_coat = coat > 1.0f ? 1.0f : (coat > 0.0f ? coat : 0.0f);
		float coat_roughness = std::isfinite(record.clear_coat_roughness) ? record.clear_coat_roughness : 0.15f;
		(*state_ptr).material_clear_coat_roughness = coat_roughness < 0.03f ? 0.03f : (coat_roughness > 1.0f ? 1.0f : coat_roughness);
	}
}
