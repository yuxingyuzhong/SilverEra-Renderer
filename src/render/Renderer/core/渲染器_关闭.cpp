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
	namespace
	{
		//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；第一百零六轮）
		std::filesystem::path path_from_utf8(const std::string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}
	}

	//取门面自身

	//关闭（幂等）
	void Renderer_Core::shutdown(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return;
		/*
		先回收渲染工作线程（第一百七十七轮）
		并行任务均为同步调用（调用方阻塞到全部块完成），故 shutdown 时不存在在途任务；
		此处直接停止可保证后续任何模块销毁都不会被工作线程访问。
		*/
		impl->thread_pool.threads_stop();
		impl->render_threads = 0;
		//严格逆序销毁（各模块 destroy 幂等，可安全处理半初始化状态；§5.4 / §9）
		/*
		阴影资源（第五十二轮）
		须先于渲染目标与管线模块销毁：阴影管线为门面具名管线（着色器模块所有权在门面）。
		*/
		if (impl->initialized)
		{
			//归还阴影管线
			if (impl->shadow_pipeline != 0)
			{
				self().pipeline_unload(impl->shadow_pipeline);
				impl->shadow_pipeline = 0;
			}
			//归还阴影目标
			if (impl->shadow_target != 0)
			{
				self().render_target_destroy(impl->shadow_target);
				impl->shadow_target = 0;
			}
			//归还聚光阴影图集（第六百七十轮；须先于逐瓦片目标销毁）
			if (impl->spot_shadow_atlas_target != 0)
			{
				self().render_target_destroy(impl->spot_shadow_atlas_target);
				impl->spot_shadow_atlas_target = 0;
			}
			impl->spot_shadow_atlas = false;
			impl->spot_shadow_atlas_requested = false;
			//归还聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张）
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			{
				if (impl->spot_shadow_targets[tile] != 0)
				{
					self().render_target_destroy(impl->spot_shadow_targets[tile]);
					impl->spot_shadow_targets[tile] = 0;
				}
			}
			//阴影图已失效（第五十五轮）
			impl->shadow_pass_ready = false;
			impl->shadow_signature = 0;
			impl->shadow_pass_count = 0;
		}
		/*
		粒子 billboard 资源须先于帧 / 网格上下文销毁（第三百一十四轮）
		域对象可能已随门面析构释放（见 Renderer 析构），故按实现体登记判断。
		*/
		if (impl->particle_domain != nullptr)
			impl->particle_domain->shutdown();
		impl->frame.destroy();
		/*
		原生 UI 与输入先于依赖模块销毁
		ui_render 持有每帧槽缓冲（依赖内存模块）；ui / input 只持有主机侧状态，
		但文本输入需在平台视频子系统仍存活时关闭。
		域对象可能已随门面析构释放（见 Renderer 析构），故按实现体登记判断。
		*/
		if (impl->ui_domain != nullptr)
			impl->ui_domain->shutdown();
		//网格缓冲依赖内存模块，须先于 memory 销毁
		impl->mesh.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		impl->texture.destroy();
		//渲染目标依赖设备与内存，须先于两者销毁
		impl->render_target.destroy();
		impl->active_target = 0;
		//复位同帧后处理呈现路径与其资源句柄（全屏网格随网格模块销毁、后处理管线随管线模块销毁）
		impl->present_target = 0;
		impl->present_extra.clear();
		impl->post_chain.clear();
		impl->post_pipeline = 0;
		impl->fullscreen_mesh = 0;
		//复位 SSAA 状态（超采样目标已随渲染目标模块销毁，仅清句柄、倍率与失败标记）
		impl->ssaa_scene_target = 0;
		impl->ssaa_resolve_target = 0;
		impl->ssaa_extent_width = 0;
		impl->ssaa_extent_height = 0;
		impl->ssaa_target_factor = 0;
		impl->ssaa_pipeline = 0;
		impl->ssaa_factor = 1;
		impl->ssaa_extent_failed = false;
		//复位 MSAA 采样数（第九十六轮）
		impl->msaa_samples = 1;
		//复位 MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = true;
		//复位逐级泛光参数覆盖与描边色专设，并复位剔除观测
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			impl->post_stage_param_valid[stage] = false;
			impl->post_stage_color_valid[stage] = false;
				impl->post_stage_extra_valid[stage] = false;
		}
		impl->culled_items = 0;
		//剔除开关恢复默认（开启）
		impl->cull_enabled = true;
		//静态合批恢复默认（开启，第一百二十七轮）
		impl->static_batching = true;
		//逐物体 PBR 材质恢复默认（不覆盖全局，第六十八轮）
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
		//逐物体卡通参数恢复默认（不覆盖全局，第四百零八轮）
		impl->material_toon_active = false;
		impl->material_toon_steps = 0.0f;
		impl->material_toon_rim = 0.0f;
		impl->material_toon_specular = 0.0f;
		//逐物体暗部色调恢复默认（(1,1,1) 不色偏，第四百零九轮）
		impl->material_toon_tint[0] = 1.0f;
		impl->material_toon_tint[1] = 1.0f;
		impl->material_toon_tint[2] = 1.0f;
		//逐物体色调恢复默认（白色，第六十九轮）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
		//逐物体自发光恢复默认（0，第七十轮）
		impl->material_emissive = 0.0f;
		impl->material_normal_strength = 0.0f;
		//UV 旋转与 Alpha 裁剪恢复默认（0 = 不旋转 / 关闭；第一百八十三轮）
		impl->material_uv_rotation = 0.0f;
		impl->material_alpha_cutoff = 0.0f;
		//光照恢复默认（满照：朝 -Z、白色、强度 1、环境光 0）
		impl->light_direction[0] = 0.0f;
		impl->light_direction[1] = 0.0f;
		impl->light_direction[2] = -1.0f;
		impl->light_color[0] = 1.0f;
		impl->light_color[1] = 1.0f;
		impl->light_color[2] = 1.0f;
		impl->light_intensity = 1.0f;
		impl->light_ambient = 0.0f;
		//点光源与高光恢复默认（无点光源、方向光强度 1、高光关闭、光泽度 16）
		impl->point_light_count = 0;
		impl->specular_intensity = 0.0f;
		impl->specular_shininess = 16.0f;
		//复位距离衰减模式（第三十六轮：默认线性）
		impl->attenuation_mode = 0;
		//复位距离雾（第三十八轮：密度 0 = 关闭）
		impl->fog_color[0] = 0.05f;
		impl->fog_color[1] = 0.05f;
		impl->fog_color[2] = 0.10f;
		impl->fog_density = 0.0f;
		impl->fog_start = 1.0f;
		impl->fog_end = 10.0f;
		//复位雾模式（第一百四十三轮：默认线性区间）
		impl->fog_mode = 0;
		//复位雾高度衰减（第一百四十三轮：默认关闭）
		impl->fog_height_falloff = 0.0f;
		//复位环境光颜色（第四十轮：默认白）
		impl->ambient_color[0] = 1.0f;
		impl->ambient_color[1] = 1.0f;
		impl->ambient_color[2] = 1.0f;
		//半球环境光恢复默认（地面白、强度 0 = 关闭）
		impl->ambient_ground[0] = 1.0f;
		impl->ambient_ground[1] = 1.0f;
		impl->ambient_ground[2] = 1.0f;
		impl->ambient_ground[3] = 0.0f;
		//补光恢复默认（朝 -Z、白、强度 0 = 关闭）
		impl->fill_direction[0] = 0.0f;
		impl->fill_direction[1] = 0.0f;
		impl->fill_direction[2] = -1.0f;
		impl->fill_color[0] = 1.0f;
		impl->fill_color[1] = 1.0f;
		impl->fill_color[2] = 1.0f;
		impl->fill_intensity = 0.0f;
		//逐光高光缩放恢复默认（第五十轮：均为 1）
		impl->directional_specular = 1.0f;
		impl->fill_specular = 1.0f;
		impl->pending_push_constants.clear();
		//场景 UBO 依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->scene_ubos.size(); ++slot)
				//归还场景 UBO 与其内存
				impl->memory.buffer_destroy(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot]);
		impl->scene_ubos.clear();
		impl->scene_ubo_allocs.clear();
		//实例缓冲依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->instance_buffers.size(); ++slot)
				//归还实例缓冲与其内存
				impl->memory.buffer_destroy(impl->instance_buffers[slot], impl->instance_allocations[slot]);
		impl->instance_buffers.clear();
		impl->instance_allocations.clear();
		//间接命令缓冲依赖内存模块，须先于 memory 销毁（第二百五十二轮）
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->indirect_buffers.size(); ++slot)
				//归还间接命令缓冲与其内存
				impl->memory.buffer_destroy(impl->indirect_buffers[slot], impl->indirect_allocations[slot]);
		impl->indirect_buffers.clear();
		impl->indirect_allocations.clear();
		//光源记录缓冲依赖内存模块，须先于 memory 销毁（第二百六十六轮）
		impl->cluster_destroy();
		//图形 set0 簇数据缓冲与占位缓冲依赖内存模块，须先于 memory 销毁（第二百八十八轮）
		impl->cluster_graphics_destroy();
		//默认纹理句柄复位
		impl->default_texture = 0;
		//天空盒状态复位（第一百二十五轮；默认关闭、强度 0）
		impl->sky_enabled = false;
		impl->sky_texture = 0;
		impl->default_sky_texture = 0;
		impl->sky_pipeline = 0;
		impl->sky_ambient = 0.0f;
		impl->sky_specular = 0.0f;
		//二次元风格化状态复位（第一百七十四轮；默认关闭）
		impl->toon_steps = 0;
		impl->toon_rim = 0.0f;
		//暗部色调恢复默认（白色 = 乘性恒等；第一百七十八轮）
		for (std::size_t index = 0; index < 3; ++index)
			impl->toon_shadow_tint[index] = 1.0f;
		//卡通高光阈值复位（0 = 关闭；第一百七十九轮）
		impl->toon_specular = 0.0f;
		//反向外扩描边复位（宽度 0 = 关闭；第二百二十八轮）
		impl->outline_width = 0.0f;
		impl->outline_pipeline = 0;
		for (std::size_t index = 0; index < 3; ++index)
			impl->outline_color[index] = 0.0f;
		/*
		写回管线缓存（第一百零六轮；可选）
		在销毁管线缓存之前取出字节并写盘；失败仅告警，不影响关闭流程。
		*/
		if (impl->initialized && !impl->config.pipeline_cache_path.empty())
		{
			std::vector<uint8_t> cache_bytes;
			if (impl->pipeline.cache_get(cache_bytes))
			{
				std::ofstream cache_stream(path_from_utf8(impl->config.pipeline_cache_path),
					std::ios::binary | std::ios::trunc);
				if (cache_stream.is_open())
					cache_stream.write(reinterpret_cast<const char*>(cache_bytes.data()),
						static_cast<std::streamsize>(cache_bytes.size()));
			}
		}
		//先释放门面层计算资源（缓冲与管线模块），再销毁计算上下文（第二百四十三轮）
		for (Renderer_Impl_Holder::Impl::Compute_Buffer_Record& record : impl->compute_buffers)
		{
			if (record.buffer != VK_NULL_HANDLE)
				impl->memory.buffer_destroy(record.buffer, record.allocation);
		}
		impl->compute_buffers.clear();
		for (Renderer_Impl_Holder::Impl::Compute_Pipeline_Record& record : impl->compute_pipelines)
		{
			impl->compute.pipeline_release(record.context_handle);
			if (record.module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.module);
		}
		impl->compute_pipelines.clear();
		impl->gpu_cull_pipeline = 0;
		//实例压缩管线句柄同样失效（第二百四十九轮）
		impl->gpu_compact_pipeline = 0;
		impl->last_gpu_cull_ms = 0.0;
		//Hi-Z 资源依赖内存模块与计算上下文，须先于两者销毁（第二百五十三 / 二百五十四轮）
		impl->hiz.compute_set(nullptr);
		impl->hiz.destroy();
		impl->hiz_enabled = false;
		impl->hiz_depth_copy_pipeline = 0;
		impl->hiz_downsample_pipeline = 0;
		//计算管线同样依赖设备与内存，先于两者销毁（第二百四十一轮）
		impl->compute.destroy();
		//计算着色器模块须先于着色器上下文复位
		if (impl->compute_health_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->compute_health_module);
			impl->compute_health_module = VK_NULL_HANDLE;
		}
		impl->compute_health_handle = 0;
		//管线依赖设备与内存，须先于两者销毁
		impl->pipeline.destroy();
		//着色器模块须先于着色器上下文复位
		if (impl->vert_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->vert_module);
			impl->vert_module = VK_NULL_HANDLE;
		}
		if (impl->frag_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->frag_module);
			impl->frag_module = VK_NULL_HANDLE;
		}
		//具名管线的着色器模块（须先于着色器上下文复位）
		for (auto& record : impl->extra_pipelines)
		{
			//回收该管线的顶点着色器模块
			if (record.vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.vert_module);
			//回收该管线的片元着色器模块
			if (record.frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.frag_module);
		}
		impl->extra_pipelines.clear();
		//材质记录与按档缓存的共享具名管线清空（具名管线已随 extra_pipelines 释放）
		impl->materials.clear();
		impl->material_pipelines.clear();
		impl->bound_pipeline_request = 0;
		impl->shader.destroy();
		//交换链销毁连带销毁深度图像，须排在管线之后
		impl->swapchain.destroy();
		impl->memory.destroy();
		/*
		先平台后设备
		平台持有「借用」的实例句柄，销毁表面（vkDestroySurfaceKHR）要求实例仍在途；
		设备销毁会连带 vkDestroyInstance，故必须排在平台之后。
		*/
		//解除事件泵到 UI 通道的桥接（平台销毁后不应再有回调指向 UI 通道）
		impl->platform.event_forward_clear();
		impl->platform.destroy();
		impl->device.destroy();
		//清空待绘制项与其推送常量存储
		impl->pending_items.clear();
		impl->pending_instances.clear();
		//复位状态
		impl->frame_count = 0;
		impl->initialized = false;
	}
}
