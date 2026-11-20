#include "../局部命名空间使用.h"
//六域内部完整状态
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//设置后续绘制所用混合档（切档触发图形管线重建）
	void Render_Resource::blend_mode_set(Blend_Mode mode)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//同档重复设置为空操作
		if (state_ptr->blend_mode == mode)
			return;
		/*
		触发图形管线重建（内部先等设备空闲，确保旧管线不再被在途帧引用）
		重建成功才更新已记录档位；失败则记录错误并保持档位记录（此时图形管线可能已不可用，
		不改变帧循环时序：本帧仍会清屏与推进，仅绘制项跳过）。
		*/
		if (!state_ptr->pipeline.pipeline_default_blend_set(mode))
		{
			logger.error("渲染器:: 混合档切换失败（保持已记录档位 {}）", static_cast<int32_t>(state_ptr->blend_mode));
			return;
		}
		state_ptr->blend_mode = mode;
		logger.info("渲染器:: 混合档已切换为 {}", static_cast<int32_t>(mode));
	}

	//登记具名图形管线
	bool Render_Resource::pipeline_load(uint64_t& handle_out, const std::string& vert_shader,
		const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage)
	{
		//尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			handle_out = 0;
			logger.error("渲染器:: 管线登记失败：尚未初始化");
			return false;
		}
		//公共入口一律开启背面剔除
		return pipeline_load_ex(handle_out, vert_shader, frag_shader, blend_mode, alpha_to_coverage, false);
	}

	//登记扩展图形管线
	bool Render_Resource::pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
		const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
		bool depth_write, bool cull_front, bool depth_test, bool depth_format_override,
		uint32_t depth_format, uint32_t sample_count, uint32_t color_format,
		uint32_t tone_id_format, bool depth_lequal)
	{
		//默认无有效句柄
		handle_out = 0;
		//加载顶点着色器模块（顺带解析顶点输入位置，供管线裁剪属性）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		uint32_t vert_input_locations = 0;
		if (!state_ptr->shader.module_load(vert_module, vert_shader, &vert_input_locations))
		{
			logger.error("渲染器:: 管线登记失败：顶点着色器 {} 加载失败", vert_shader);
			return false;
		}
		//加载片元着色器模块
		VkShaderModule frag_module = VK_NULL_HANDLE;
		if (!state_ptr->shader.module_load(frag_module, frag_shader))
		{
			logger.error("渲染器:: 管线登记失败：片元着色器 {} 加载失败", frag_shader);
			//回收已加载的顶点着色器模块，避免泄漏
			state_ptr->shader.module_destroy(vert_module);
			return false;
		}
		//登记到管线模块（具名管线与默认场景管线共享同一管线布局）
		uint64_t handle = 0;
		if (!state_ptr->pipeline.pipeline_register(handle, vert_module, frag_module, blend_mode,
			static_cast<VkFormat>(color_format), cull_none, sample_count, vert_input_locations, depth_write,
			depth_lequal, alpha_to_coverage, cull_front, depth_test, depth_format_override,
			static_cast<VkFormat>(depth_format), static_cast<VkFormat>(tone_id_format)))
		{
			logger.error("渲染器:: 管线登记失败：图形管线创建失败");
			//回收两个着色器模块，避免泄漏
			state_ptr->shader.module_destroy(vert_module);
			state_ptr->shader.module_destroy(frag_module);
			return false;
		}
		//记录具名管线（着色器模块所有权在资源域）
		Extra_Pipeline_Record record{};
		record.handle = handle;
		record.vert_module = vert_module;
		record.frag_module = frag_module;
		state_ptr->extra_pipelines.push_back(record);
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 具名管线已登记（句柄 {}）", handle);
		return true;
	}

	/*
	登记场景管线变体（第 831 轮）
	场景着色器对 ＋ 不透明档，仅颜色 / 深度格式与采样数可改；随后标记为「场景管线变体」，
	使引用该管线的材质仍可把逐材质色调编号写入场景目标 alpha。
	*/
	bool Render_Resource::scene_pipeline_variant_load(uint64_t& handle_out, uint32_t color_format,
		uint32_t sample_count, uint32_t depth_format)
	{
		//默认无有效句柄
		handle_out = 0;
		//尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 场景管线变体登记失败：尚未初始化");
			return false;
		}
		//颜色格式缺省时跟随场景管线口径
		const uint32_t effective_color = color_format != 0 ? color_format
			: static_cast<uint32_t>(owner->device().scene_color_format());
		//深度格式缺省时跟随交换链
		const uint32_t effective_depth = depth_format != 0 ? depth_format
			: static_cast<uint32_t>(core->swapchain.depth_format_get());
		//按场景口径登记（背面剔除、深度测试与写入均开，混合档不透明）
		if (!pipeline_load_ex(handle_out, "场景.vert.spv", "场景.frag.spv", Blend_Mode::Opaque,
			false, false, true, false, true, true, effective_depth, sample_count, effective_color))
			return false;
		//标记为场景管线变体（失败则回收，避免留下「普通具名管线」）
		if (!(*state_ptr).pipeline.pipeline_mark_scene_variant(handle_out))
		{
			pipeline_unload(handle_out);
			handle_out = 0;
			logger.error("渲染器:: 场景管线变体标记失败");
			return false;
		}
		logger.info("渲染器:: 场景管线变体已登记（句柄 {}，颜色格式 {}，采样数 {}）",
			handle_out, effective_color, sample_count);
		return true;
	}

	//句柄是否为场景管线变体（第 831 轮）
	bool Render_Resource::pipeline_scene_variant(uint64_t handle) const
	{
		return (*state_ptr).pipeline.pipeline_scene_variant(handle);
	}

	//追加反向外扩描边项
	void Device_State::outline_item_push(const Frame_Render_Item& item)
	{
		//描边关闭、管线未登记或正在绘制文字则空操作
		if (owner->scene().outline_width() <= 0.0f || outline_pipeline == 0
			|| (owner != nullptr && owner->ui().text_drawing()))
			return;
		//推送块：x = 外扩宽度、yzw = 描边色（与后处理描边色共用 offset 112 槽位）
		outline_push[0] = owner->scene().outline_width();
		owner->scene().outline_color(outline_push.data() + 1);
		//复制主体项（实例区与索引区原样复用），换描边管线与描边推送块
		Frame_Render_Item outline_item = item;
		outline_item.pipeline = reinterpret_cast<VkPipeline>(owner->resource().pipeline_native_get(outline_pipeline));
		outline_item.push_constants = outline_push.data();
		outline_item.push_constant_offset = static_cast<uint32_t>(sizeof(float) * 28);
		outline_item.push_constant_size = static_cast<uint32_t>(sizeof(float) * outline_push.size());
		//描边项不投影阴影、不参与合批（管线与推送块均与主体项不同）
		outline_item.cast_shadow = false;
		outline_item.transparent = true;
		pending_items.push_back(outline_item);
	}

	//按句柄查询原生图形管线
	uint64_t Render_Resource::pipeline_native_get(uint64_t handle) const
	{
		return reinterpret_cast<uint64_t>(state_ptr->pipeline.pipeline_by_handle_get(handle));
	}

	bool Render_Resource::pipeline_rebuild(uint32_t color_format, uint32_t depth_format, uint32_t sample_count)
	{
		return state_ptr->pipeline.pipeline_rebuild(static_cast<VkFormat>(color_format),
			static_cast<VkFormat>(depth_format), sample_count);
	}

	bool Render_Resource::render_target_valid(uint64_t handle) const
	{
		return state_ptr->render_target.target_valid(handle);
	}

	//卸载具名图形管线
	void Render_Resource::pipeline_unload(uint64_t handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//句柄 0 为默认场景管线，禁止卸载
		if (handle == 0)
			return;
		/*
		先等全部在途帧完成
		图形管线可能仍被在途命令缓冲引用（其绑定状态已被记录进该命令缓冲），
		未等在途帧用尽即销毁会触发校验告警且属未定义行为。
		*/
		core->frame.frames_wait();
		//在资源域记录中查找目标
		for (auto it = state_ptr->extra_pipelines.begin(); it != state_ptr->extra_pipelines.end(); ++it)
		{
			//跳过非目标记录
			if (it->handle != handle)
				continue;
			//注销管线模块中的图形管线
			state_ptr->pipeline.pipeline_release(handle);
			//销毁该管线的两个着色器模块
			if (it->vert_module != VK_NULL_HANDLE)
				state_ptr->shader.module_destroy(it->vert_module);
			if (it->frag_module != VK_NULL_HANDLE)
				state_ptr->shader.module_destroy(it->frag_module);
			//移除资源域记录
			state_ptr->extra_pipelines.erase(it);
			//若卸载的是当前所选管线则回落默认场景管线
			if (state_ptr->bound_pipeline_request == handle)
			{
				state_ptr->bound_pipeline_request = 0;
				state_ptr->pipeline.pipeline_activate(0);
			}
			//若该管线在后处理链中则一并摘除（避免链残留失效句柄导致整帧回落）
			std::size_t remaining_stages = 0;
			const bool chain_removed = owner->device().pipeline_unloaded(handle, remaining_stages);
			//若确有摘除则告警
			if (chain_removed)
				logger.warn("渲染器:: 后处理链已摘除被卸载管线（句柄 {}，剩余 {} 级）；逐级参数已整体清除",
					handle, remaining_stages);
			logger.info("渲染器:: 具名管线已卸载（句柄 {}）", handle);
			return;
		}
	}

	//设置后续绘制所用图形管线
	void Render_Resource::pipeline_bind_set(uint64_t handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//目标句柄（0 = 默认场景管线）
		uint64_t target = handle;
		//若句柄非零且尚未登记则回落默认
		if (target != 0 && !state_ptr->pipeline.pipeline_has(target))
		{
			logger.warn("渲染器:: 管线句柄 {} 未登记，回落默认场景管线", handle);
			target = 0;
		}
		//激活目标管线（失败则保持原激活不变）
		if (!state_ptr->pipeline.pipeline_activate(target))
			return;
		state_ptr->bound_pipeline_request = target;
	}
}
