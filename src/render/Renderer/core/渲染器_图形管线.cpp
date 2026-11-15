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

	void Render_Resource::blend_mode_set(Blend_Mode mode)
	{
		//实现体（通用状态尚未完全拆域）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
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

	bool Render_Resource::pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage)
	{
		//实现体（通用状态尚未完全拆域）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
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

	void Renderer_Impl_Holder::Impl::outline_item_push(const Frame_Render_Item& item)
	{
		//描边关闭、管线未登记或正在绘制文字则空操作
		if (scene_domain->state().outline_width <= 0.0f || outline_pipeline == 0
			|| (ui_domain != nullptr && ui_domain->state().in_text_draw))
			return;
		//推送块：x = 外扩宽度、yzw = 描边色（与后处理描边色共用 offset 112 槽位）
		outline_push[0] = scene_domain->state().outline_width;
		outline_push[1] = scene_domain->state().outline_color[0];
		outline_push[2] = scene_domain->state().outline_color[1];
		outline_push[3] = scene_domain->state().outline_color[2];
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

	void Render_Resource::pipeline_unload(uint64_t handle)
	{
		//实现体（通用状态尚未完全拆域）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
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
			if (state().bound_pipeline_request == handle)
			{
				state().bound_pipeline_request = 0;
				impl->pipeline.pipeline_activate(0);
			}
			//若该管线在后处理链中则一并摘除（避免链残留失效句柄导致整帧回落）
			std::size_t chain_before = impl->device_domain->state().post_chain.size();
			impl->device_domain->state().post_chain.erase(std::remove(impl->device_domain->state().post_chain.begin(), impl->device_domain->state().post_chain.end(), handle), impl->device_domain->state().post_chain.end());
			//若确有摘除则告警
			if (impl->device_domain->state().post_chain.size() != chain_before)
			{
				/*
				级序已压缩（第二十五轮修复）
				逐级专设参数按旧下标存放，压缩后会错位到别的级，故整体清除（宁可回到全局参数，不可错配）。
				*/
				for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
				{
					impl->device_domain->state().post_stage_param_valid[stage] = false;
					impl->device_domain->state().post_stage_color_valid[stage] = false;
				impl->device_domain->state().post_stage_extra_valid[stage] = false;
				}
				logger.warn("渲染器:: 后处理链已摘除被卸载管线（句柄 {}，剩余 {} 级）；逐级参数已整体清除",
					handle, impl->device_domain->state().post_chain.size());
			}
			logger.info("渲染器:: 具名管线已卸载（句柄 {}）", handle);
			return;
		}
	}

	void Render_Resource::pipeline_bind_set(uint64_t handle)
	{
		//实现体（通用状态尚未完全拆域）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
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
		state().bound_pipeline_request = target;
	}
}
