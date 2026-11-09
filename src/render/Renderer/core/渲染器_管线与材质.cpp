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
	//设置后续绘制所用混合档（切档触发图形管线重建）
	void Renderer_Pipeline::blend_mode_set(Blend_Mode mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//同档重复设置为空操作
		if (impl->blend_mode == mode)
			return;
		/*
		触发图形管线重建（内部先等设备空闲，确保旧管线不再被在途帧引用）
		重建成功才更新已记录档位；失败则记录错误并保持档位记录（此时图形管线可能已不可用，
		不改变帧循环时序：本帧仍会清屏与推进，仅绘制项跳过）。
		*/
		if (!impl->pipeline.pipeline_default_blend_set(mode))
		{
			logger.error("渲染器:: 混合档切换失败（保持已记录档位 {}）", static_cast<int32_t>(impl->blend_mode));
			return;
		}
		impl->blend_mode = mode;
		logger.info("渲染器:: 混合档已切换为 {}", static_cast<int32_t>(mode));
	}

	//登记具名图形管线
	bool Renderer_Pipeline::pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage)
	{
		//尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			handle_out = 0;
			logger.error("渲染器:: 管线登记失败：尚未初始化");
			return false;
		}
		//公共入口一律开启背面剔除
		return impl->pipeline_load_ex(handle_out, vert_shader, frag_shader, blend_mode, alpha_to_coverage, false);
	}

	//登记具名图形管线（内部实现；可指定是否关闭背面剔除与是否写深度）
	bool Renderer_Impl_Holder::Impl::pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
		const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
		bool depth_write, bool cull_front, bool depth_test, bool depth_format_override,
		VkFormat depth_format, uint32_t sample_count)
	{
		//默认无有效句柄
		handle_out = 0;
		//加载顶点着色器模块（顺带解析顶点输入位置，供管线裁剪属性）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		uint32_t vert_input_locations = 0;
		if (!shader.module_load(vert_module, vert_shader, &vert_input_locations))
		{
			logger.error("渲染器:: 管线登记失败：顶点着色器 {} 加载失败", vert_shader);
			return false;
		}
		//加载片元着色器模块
		VkShaderModule frag_module = VK_NULL_HANDLE;
		if (!shader.module_load(frag_module, frag_shader))
		{
			logger.error("渲染器:: 管线登记失败：片元着色器 {} 加载失败", frag_shader);
			//回收已加载的顶点着色器模块，避免泄漏
			shader.module_destroy(vert_module);
			return false;
		}
		//登记到管线模块（具名管线与默认场景管线共享同一管线布局）
		uint64_t handle = 0;
		if (!pipeline.pipeline_register(handle, vert_module, frag_module, blend_mode,
			VK_FORMAT_UNDEFINED, cull_none, sample_count, vert_input_locations, depth_write, false,
			alpha_to_coverage, cull_front, depth_test, depth_format_override, depth_format))
		{
			logger.error("渲染器:: 管线登记失败：图形管线创建失败");
			//回收两个着色器模块，避免泄漏
			shader.module_destroy(vert_module);
			shader.module_destroy(frag_module);
			return false;
		}
		//记录具名管线（着色器模块所有权在门面）
		Extra_Pipeline_Record record{};
		record.handle = handle;
		record.vert_module = vert_module;
		record.frag_module = frag_module;
		extra_pipelines.push_back(record);
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 具名管线已登记（句柄 {}）", handle);
		return true;
	}

	/*
	确保 UI 管线就绪（惰性登记）
	纯色 / 文字两条管线均固定：Alpha 混合、双面、关闭深度测试、深度附件格式 UNDEFINED、单采样；
	UI 阶段只有颜色附件，若沿用场景深度格式会在动态渲染格式不匹配处被校验层拦截。
	*/
	bool Renderer_Impl_Holder::Impl::ui_pipeline_ensure(void)
	{
		//已尝试过则直接返回结果
		if (ui_pipeline_ready)
			return ui_solid_pipeline != 0 && ui_text_pipeline != 0;
		ui_pipeline_ready = true;

		//登记纯色管线
		uint64_t solid = 0;
		if (pipeline_load_ex(solid, "UI纯色.vert.spv", "UI纯色.frag.spv", Blend_Mode::Alpha,
			false, true, false, false, false, true, VK_FORMAT_UNDEFINED, 1))
			ui_solid_pipeline = solid;
		else
			logger.warn("渲染器:: UI 纯色管线登记失败（原生 UI 只绘制文字）");

		//登记文字管线
		uint64_t text = 0;
		if (pipeline_load_ex(text, "UI文字.vert.spv", "UI文字.frag.spv", Blend_Mode::Alpha,
			false, true, false, false, false, true, VK_FORMAT_UNDEFINED, 1))
			ui_text_pipeline = text;
		else
			logger.warn("渲染器:: UI 文字管线登记失败（原生 UI 只绘制纯色）");

		return ui_solid_pipeline != 0 && ui_text_pipeline != 0;
	}

	/*
	UI 阶段绘制入口
	先绘制原生 UI，再把同一命令缓冲交给外部 UI 回调（ImGui 等平台后端）；
	最后绘制者在最上层，故外部 UI 覆盖原生 UI。
	*/
	void Renderer_Impl_Holder::Impl::ui_draw_entry(VkCommandBuffer command_buffer, void* user_data)
	{
		//用户数据为本实现体
		Impl* self = static_cast<Impl*>(user_data);
		//空实现体直接返回
		if (self == nullptr || command_buffer == VK_NULL_HANDLE)
			return;
		/*
		原生 UI 绘制
		目标尺寸取交换链尺寸：UI 阶段恒在最终呈现图像上绘制（SSAA / 后处理链均不吃 UI）。
		*/
		if (self->ui_render.valid())
		{
			const VkExtent2D extent = self->swapchain.extent_get();
			const uint32_t slot = self->frame.slot_index_get();
			self->ui_render.draw(self->ui, command_buffer, extent, slot);
		}
		//外部 UI 回调（未注册时为空操作）
		if (const Frame_Ui_Callback* external = self->ui_channel.draw_callback_get())
			external->fn(command_buffer, external->user_data);
	}

	/*
	登记反向外扩描边项（第二百二十八轮）
	把主体绘制项复制一份，换「剔除正面」的描边管线，并把宽度 / 颜色推入 offset 112 的推送块；
	描边关闭（宽度为 0）、描边管线未登记或正在绘制文字时为空操作。
	*/
	void Renderer_Impl_Holder::Impl::outline_item_push(const Frame_Render_Item& item)
	{
		//描边关闭或管线未登记则空操作
		if (outline_width <= 0.0f || outline_pipeline == 0 || in_text_draw)
			return;
		//推送块：x = 外扩宽度、yzw = 描边色（与后处理描边色共用 offset 112 槽位）
		outline_push[0] = outline_width;
		outline_push[1] = outline_color[0];
		outline_push[2] = outline_color[1];
		outline_push[3] = outline_color[2];
		//复制主体项（实例区与索引区原样复用），换描边管线与描边推送块
		Frame_Render_Item outline_item = item;
		outline_item.pipeline = pipeline.pipeline_by_handle_get(outline_pipeline);
		outline_item.push_constants = outline_push.data();
		outline_item.push_constant_offset = static_cast<uint32_t>(sizeof(float) * 28);
		outline_item.push_constant_size = static_cast<uint32_t>(sizeof(float) * outline_push.size());
		//描边项不投影阴影、不参与合批（管线与推送块均与主体项不同）
		outline_item.cast_shadow = false;
		outline_item.transparent = true;
		pending_items.push_back(outline_item);
	}

	//卸载具名图形管线
	void Renderer_Pipeline::pipeline_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄 0 为默认场景管线，禁止卸载
		if (handle == 0)
			return;
		/*
		先等全部在途帧完成
		图形管线可能仍被在途命令缓冲引用（其绑定状态已被记录进该命令缓冲），
		未等在途帧用尽即销毁会触发校验告警且属未定义行为。
		*/
		impl->frame.frames_wait();
		//在门面记录中查找目标
		for (auto it = impl->extra_pipelines.begin(); it != impl->extra_pipelines.end(); ++it)
		{
			//跳过非目标记录
			if (it->handle != handle)
				continue;
			//注销管线模块中的图形管线
			impl->pipeline.pipeline_release(handle);
			//销毁该管线的两个着色器模块
			if (it->vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->vert_module);
			if (it->frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->frag_module);
			//移除门面记录
			impl->extra_pipelines.erase(it);
			//若卸载的是当前所选管线则回落默认场景管线
			if (impl->bound_pipeline_request == handle)
			{
				impl->bound_pipeline_request = 0;
				impl->pipeline.pipeline_activate(0);
			}
			//若该管线在后处理链中则一并摘除（避免链残留失效句柄导致整帧回落）
			std::size_t chain_before = impl->post_chain.size();
			impl->post_chain.erase(std::remove(impl->post_chain.begin(), impl->post_chain.end(), handle), impl->post_chain.end());
			//若确有摘除则告警
			if (impl->post_chain.size() != chain_before)
			{
				/*
				级序已压缩（第二十五轮修复）
				逐级专设参数按旧下标存放，压缩后会错位到别的级，故整体清除（宁可回到全局参数，不可错配）。
				*/
				for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
				{
					impl->post_stage_param_valid[stage] = false;
					impl->post_stage_color_valid[stage] = false;
				impl->post_stage_extra_valid[stage] = false;
				}
				logger.warn("渲染器:: 后处理链已摘除被卸载管线（句柄 {}，剩余 {} 级）；逐级参数已整体清除",
					handle, impl->post_chain.size());
			}
			logger.info("渲染器:: 具名管线已卸载（句柄 {}）", handle);
			return;
		}
	}

	//设置后续绘制所用图形管线
	void Renderer_Pipeline::pipeline_bind_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//目标句柄（0 = 默认场景管线）
		uint64_t target = handle;
		//若句柄非零且尚未登记则回落默认
		if (target != 0 && !impl->pipeline.pipeline_has(target))
		{
			logger.warn("渲染器:: 管线句柄 {} 未登记，回落默认场景管线", handle);
			target = 0;
		}
		//激活目标管线（失败则保持原激活不变）
		if (!impl->pipeline.pipeline_activate(target))
			return;
		impl->bound_pipeline_request = target;
	}

	/*
	从 MTL 材质库创建材质（第四百二十三轮）
	字段换算与贴图解码都在这里完成；贴图相对 MTL 所在目录解析（UTF-8 窄路径直接拼接，避免宽窄转换）。
	任一材质创建失败即回退本次已建的全部材质与纹理，并清空输出。
	*/
	bool Renderer_Material::material_load_mtl(std::vector<Material_Handle>& handles_out, const std::string& path,
		std::vector<std::string>* names_out)
	{
		//默认无输出（句柄与名称都清空）
		handles_out.clear();
		if (names_out != nullptr)
			names_out->clear();
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
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
				if (self().texture_load_file(贴图, 目录 + 项.diffuse_map))
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
				if (self().texture_load_file(贴图, 目录 + 项.normal_map))
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
			if (!self().material_load(材质, 描述))
			{
				logger.warn("渲染器:: MTL 材质 {} 创建失败，回退本次已建资源", 项.name);
				//回退材质
				for (Material_Handle 已建项 : 已建)
					self().material_unload(已建项);
				//回退纹理
				for (uint64_t 贴图 : 纹理)
					self().texture_unload(贴图);
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


	//校验材质实例数据（第四百四十八轮；方向七阶段 A）
	bool Renderer_Material::material_instance_validate(const Material_Instance_Data& data) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//模板句柄须为已登记材质
		if (data.模板 == 0 || data.模板 > impl->materials.size() || !impl->materials[data.模板 - 1].valid)
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


	//登记材质（图形管线 ＋ 纹理 ＋ 混合档）
	bool Renderer_Material::material_load(Material_Handle& handle_out, const Material_Data& data)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 材质登记失败：尚未初始化");
			return false;
		}
		//待登记记录
		Impl::Material_Record record{};
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
			std::vector<uint64_t>& table = impl->material_pipelines;
			if (table.size() <= index)
				table.resize(index + 1, 0);
			//该组合尚未登记则以默认场景着色器对登记一条（此后同组合材质共享之）
			if (table[index] == 0)
			{
				if (!impl->pipeline_load_ex(table[index], "场景.vert.spv", "场景.frag.spv",
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
		for (std::size_t index = 0; index < impl->materials.size(); ++index)
		{
			//若该槽已空
			if (!impl->materials[index].valid)
			{
				impl->materials[index] = record;
				handle = static_cast<Material_Handle>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			impl->materials.push_back(record);
			handle = static_cast<Material_Handle>(impl->materials.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 材质已登记（句柄 {}，管线 {}，纹理 {}）", handle, record.pipeline, record.texture);
		return true;
	}

	//卸载材质
	void Renderer_Material::material_unload(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零或越界即视为无需卸载（幂等）
		if (handle == 0 || handle > impl->materials.size())
			return;
		//取目标记录
		Impl::Material_Record& record = impl->materials[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//复位记录（标记槽为空；具名管线为门面共享资源，不随材质卸载释放）
		record = Impl::Material_Record{};
		logger.info("渲染器:: 材质已卸载（句柄 {}）", handle);
	}

	//设置后续绘制所用材质
	void Renderer_Material::material_bind_set(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零则回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖
		if (handle == 0)
		{
			self().pipeline_bind_set(0);
			self().texture_bind_set(0);
			self().material_pbr_clear();
			self().material_tint_clear();
			self().material_emissive_clear();
			impl->material_normal_strength = 0.0f;
			impl->material_uv_scale[0] = 1.0f;
			impl->material_uv_scale[1] = 1.0f;
			impl->material_uv_offset[0] = 0.0f;
			impl->material_uv_offset[1] = 0.0f;
			impl->material_uv_rotation = 0.0f;
			impl->material_alpha_cutoff = 0.0f;
			impl->material_clear_coat = 0.0f;
			impl->material_clear_coat_roughness = 0.15f;
			//清除逐物体卡通覆盖（第四百零八轮）
			impl->material_toon_active = false;
			impl->material_toon_steps = 0.0f;
			impl->material_toon_rim = 0.0f;
			impl->material_toon_specular = 0.0f;
			//清除逐物体暗部色调（第四百零九轮；回落 (1,1,1) 不色偏）
			impl->material_toon_tint[0] = 1.0f;
			impl->material_toon_tint[1] = 1.0f;
			impl->material_toon_tint[2] = 1.0f;
			return;
		}
		//句柄无效则回落默认并告警
		if (handle > impl->materials.size() || !impl->materials[handle - 1].valid)
		{
			logger.warn("渲染器:: 材质句柄 {} 无效，回落默认管线与默认纹理", handle);
			self().pipeline_bind_set(0);
			self().texture_bind_set(0);
			self().material_pbr_clear();
			self().material_tint_clear();
			self().material_emissive_clear();
			impl->material_normal_strength = 0.0f;
			impl->material_uv_scale[0] = 1.0f;
			impl->material_uv_scale[1] = 1.0f;
			impl->material_uv_offset[0] = 0.0f;
			impl->material_uv_offset[1] = 0.0f;
			impl->material_uv_rotation = 0.0f;
			impl->material_alpha_cutoff = 0.0f;
			impl->material_clear_coat = 0.0f;
			impl->material_clear_coat_roughness = 0.15f;
			//清除逐物体卡通覆盖（第四百零八轮）
			impl->material_toon_active = false;
			impl->material_toon_steps = 0.0f;
			impl->material_toon_rim = 0.0f;
			impl->material_toon_specular = 0.0f;
			//清除逐物体暗部色调（第四百零九轮；回落 (1,1,1) 不色偏）
			impl->material_toon_tint[0] = 1.0f;
			impl->material_toon_tint[1] = 1.0f;
			impl->material_toon_tint[2] = 1.0f;
			return;
		}
		//取目标记录
		const Impl::Material_Record& record = impl->materials[handle - 1];
		//绑定材质管线与纹理（管线句柄未登记 / 纹理句柄无效时各自内部回落默认）
		self().pipeline_bind_set(record.pipeline);
		self().texture_bind_set(record.texture);
		/*
		PBR 贴图化（第一百二十四轮）
		法线 / 粗糙度贴图无效或未配置时回落内置默认白纹理（采样为 (1,1,1)，画面不变）；
		法线强度经推送常量 offset 80 的 x 分量下发给场景片元。
		*/
		impl->material_normal_strength = record.normal_strength;
		{
			//默认白纹理（兜底视图与采样器）
			VkImageView white_view = VK_NULL_HANDLE;
			VkSampler white_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, white_view, white_sampler);
			/*
			材质贴图采样器（第一百七十轮）
			纹理模块全部纹理共用同一采样器，故五个贴图只取用这一个采样器。
			*/
			VkSampler material_sampler = white_sampler;
			//法线贴图（无效则用默认白纹理）
			VkImageView normal_view = white_view;
			if (record.normal_texture != 0)
				impl->texture.texture_bind_get(record.normal_texture, normal_view, material_sampler);
			//粗糙度贴图（无效则用默认白纹理）
			VkImageView roughness_view = white_view;
			if (record.roughness_texture != 0)
				impl->texture.texture_bind_get(record.roughness_texture, roughness_view, material_sampler);
			//自发光颜色贴图（第一百六十八轮；无效则用默认白纹理）
			VkImageView emissive_view = white_view;
			if (record.emissive_texture != 0)
				impl->texture.texture_bind_get(record.emissive_texture, emissive_view, material_sampler);
			//环境光遮蔽贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView ao_view = white_view;
			if (record.ao_texture != 0)
				impl->texture.texture_bind_get(record.ao_texture, ao_view, material_sampler);
			//金属度贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView metallic_view = white_view;
			if (record.metallic_texture != 0)
				impl->texture.texture_bind_get(record.metallic_texture, metallic_view, material_sampler);
			/*
			一次性写入全部材质贴图（第一百七十轮）
			任一视图取用失败则整次跳过，避免把空句柄写进描述符集。
			*/
			if (normal_view != VK_NULL_HANDLE && roughness_view != VK_NULL_HANDLE
				&& emissive_view != VK_NULL_HANDLE && ao_view != VK_NULL_HANDLE && metallic_view != VK_NULL_HANDLE)
			{
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.material_textures_bind(slot, material_sampler,
						normal_view, roughness_view, emissive_view, ao_view, metallic_view);
				//同步材质贴图状态（逐绘制项采样集按该快照写入）
				impl->material_views[0] = normal_view;
				impl->material_views[1] = roughness_view;
				impl->material_views[2] = emissive_view;
				impl->material_views[3] = ao_view;
				impl->material_views[4] = metallic_view;
			}
		}
		//按记录的 PBR 覆盖标志设置 / 清除逐物体材质（第六十八轮）
		if (record.pbr_override)
			impl->material_pbr_active = true;
		else
			impl->material_pbr_active = false;
		impl->material_pbr_roughness = record.pbr_override ? record.pbr_roughness : 1.0f;
		impl->material_pbr_metallic = record.pbr_override ? record.pbr_metallic : 0.0f;
		//按记录的卡通覆盖标志同步逐物体卡通参数（第四百零八轮；无覆盖时三项清零即回落全局）
		impl->material_toon_active = record.toon_override;
		impl->material_toon_steps = record.toon_override ? record.toon_steps : 0.0f;
		impl->material_toon_rim = record.toon_override ? record.toon_rim : 0.0f;
		impl->material_toon_specular = record.toon_override ? record.toon_specular : 0.0f;
		//逐物体暗部色调（第四百零九轮；无覆盖时回落 (1,1,1) 不色偏）
		for (std::size_t index = 0; index < 3; ++index)
			impl->material_toon_tint[index] = record.toon_override ? record.toon_shadow_tint[index] : 1.0f;
		//按记录同步逐物体色调（第六十九轮）
		impl->material_tint_active = record.tint_override;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = record.tint_override ? record.tint[index] : 1.0f;
		//同步逐物体自发光（第七十轮）
		impl->material_emissive = record.emissive;
		//同步贴图 UV 变换（第一百七十一轮；材质默认 (1,1)/(0,0) 即恒等）
		for (std::size_t index = 0; index < 2; ++index)
		{
			impl->material_uv_scale[index] = record.uv_scale[index];
			impl->material_uv_offset[index] = record.uv_offset[index];
		}
		//同步 UV 旋转与 Alpha 裁剪（第一百八十三轮；默认 0 即关闭）
		impl->material_uv_rotation = record.uv_rotation;
		impl->material_alpha_cutoff = record.alpha_cutoff;
		//同步清漆层（第二百三十三轮；默认 0 即关闭；强度钳 [0,1]、粗糙度钳 [0.03,1]）
		float coat = std::isfinite(record.clear_coat) ? record.clear_coat : 0.0f;
		impl->material_clear_coat = coat > 1.0f ? 1.0f : (coat > 0.0f ? coat : 0.0f);
		float coat_roughness = std::isfinite(record.clear_coat_roughness) ? record.clear_coat_roughness : 0.15f;
		impl->material_clear_coat_roughness = coat_roughness < 0.03f ? 0.03f : (coat_roughness > 1.0f ? 1.0f : coat_roughness);
	}

	/*
	设置逐物体 PBR 材质（第六十八轮）
	粗糙度合法域 (0.05, 4]、金属度合法域 [0, 1]；任一项越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（实例属性在 mesh_draw 时写入），与绑定类接口的吸附语义一致。
	*/
	bool Renderer_Material::material_pbr_set(float roughness, float metallic)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体材质失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(roughness > 0.05f && roughness <= 4.0f))
		{
			logger.warn("渲染器:: 逐物体材质粗糙度 {} 越界（合法域 (0.05, 4]），已忽略", roughness);
			return false;
		}
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体材质金属度 {} 越界（合法域 [0, 1]），已忽略", metallic);
			return false;
		}
		//写入吸附状态
		impl->material_pbr_active = true;
		impl->material_pbr_roughness = roughness;
		impl->material_pbr_metallic = metallic;
		return true;
	}

	//清除逐物体 PBR 材质（第六十八轮）
	void Renderer_Material::material_pbr_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落全局参数
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
	}

	//当前是否启用逐物体 PBR 材质覆盖（第六十八轮）
	bool Renderer_Material::material_pbr_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_pbr_active;
	}

	//当前逐物体材质粗糙度缩放（第六十八轮）
	float Renderer_Material::material_pbr_roughness(void) const
	{
		//未初始化返回默认 1
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->material_pbr_roughness;
	}

	//当前逐物体材质金属度（第六十八轮）
	float Renderer_Material::material_pbr_metallic(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_pbr_metallic;
	}

	/*
	设置逐物体清漆层（第二百三十九轮）
	强度合法域 (0, 1]、粗糙度合法域 [0.03, 1]；任一项越界 / 非数则整次拒绝（状态不变）。
	只影响**之后登记**的绘制项（推送块在 mesh_draw 时写入），与绑定类接口的吸附语义一致。
	*/
	bool Renderer_Material::material_clear_coat_set(float strength, float roughness)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体清漆层失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(strength > 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体清漆强度 {} 越界（合法域 (0, 1]），已忽略", strength);
			return false;
		}
		if (!(roughness >= 0.03f && roughness <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体清漆粗糙度 {} 越界（合法域 [0.03, 1]），已忽略", roughness);
			return false;
		}
		//写入吸附状态
		impl->material_clear_coat = strength;
		impl->material_clear_coat_roughness = roughness;
		return true;
	}

	//清除逐物体清漆层（第二百三十九轮）
	void Renderer_Material::material_clear_coat_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落清漆关闭
		impl->material_clear_coat = 0.0f;
		impl->material_clear_coat_roughness = 0.15f;
	}

	//当前是否启用逐物体清漆层（第二百三十九轮）
	bool Renderer_Material::material_clear_coat_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_clear_coat > 0.0f;
	}

	//当前逐物体清漆强度（第二百三十九轮）
	float Renderer_Material::material_clear_coat_strength(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_clear_coat;
	}

	//当前逐物体清漆粗糙度（第二百三十九轮）
	float Renderer_Material::material_clear_coat_roughness(void) const
	{
		//未初始化返回默认 0.15
		if (impl == nullptr || !impl->initialized)
			return 0.15f;
		return impl->material_clear_coat_roughness;
	}

	/*
	设置逐物体反照率色调（第六十九轮）
	四个分量须都在 [0, 1]；任一分量越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（推送常量在 mesh_draw 时写入），与绑定类接口的吸附语义一致；
	mesh_draw 显式给出的 color 形参优先于本状态。
	*/
	bool Renderer_Material::material_tint_set(float red, float green, float blue, float alpha)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体色调失败：尚未初始化");
			return false;
		}
		//各分量合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(red >= 0.0f && red <= 1.0f) || !(green >= 0.0f && green <= 1.0f)
			|| !(blue >= 0.0f && blue <= 1.0f) || !(alpha >= 0.0f && alpha <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体色调分量越界（{} / {} / {} / {}），已忽略", red, green, blue, alpha);
			return false;
		}
		//写入吸附状态
		impl->material_tint_active = true;
		impl->material_tint[0] = red;
		impl->material_tint[1] = green;
		impl->material_tint[2] = blue;
		impl->material_tint[3] = alpha;
		return true;
	}

	//清除逐物体反照率色调（第六十九轮）
	void Renderer_Material::material_tint_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落白色（画面与未设置时一致）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
	}

	//当前是否启用逐物体反照率色调（第六十九轮）
	bool Renderer_Material::material_tint_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_tint_active;
	}

	//读取当前逐物体色调（第六十九轮）
	void Renderer_Material::material_tint_get(float out[4]) const
	{
		//空指针直接忽略
		if (out == nullptr)
			return;
		//未初始化返回白色
		if (impl == nullptr || !impl->initialized)
		{
			for (std::size_t index = 0; index < 4; ++index)
				out[index] = 1.0f;
			return;
		}
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = impl->material_tint[index];
	}

	/*
	设置逐物体自发光强度（第七十轮）
	合法域 [0, 8]；越界 / 非数整次拒绝（状态不变）。自发光恒生效，无需覆盖标志。
	*/
	bool Renderer_Material::material_emissive_set(float emissive)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置自发光失败：尚未初始化");
			return false;
		}
		//合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(emissive >= 0.0f && emissive <= 8.0f))
		{
			logger.warn("渲染器:: 自发光 {} 越界（合法域 [0, 8]），已忽略", emissive);
			return false;
		}
		impl->material_emissive = emissive;
		return true;
	}

	//清除逐物体自发光（第七十轮）
	void Renderer_Material::material_emissive_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->material_emissive = 0.0f;
		impl->material_normal_strength = 0.0f;
	}

	//当前逐物体自发光强度（第七十轮）
	float Renderer_Material::material_emissive(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_emissive;
	}

}
