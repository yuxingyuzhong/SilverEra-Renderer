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
			//归还阴影管线（资源域可能已随门面析构释放，此时随管线模块统一销毁）
			if (impl->shadow_pipeline != 0)
			{
				if (impl->resource_domain != nullptr)
					impl->resource_domain->pipeline_unload(impl->shadow_pipeline);
				impl->shadow_pipeline = 0;
			}
			//归还阴影目标
			if (impl->shadow_target != 0)
			{
				if (impl->resource_domain != nullptr)
					impl->resource_domain->render_target_destroy(impl->shadow_target);
				impl->shadow_target = 0;
			}
			//归还聚光阴影图集（第六百七十轮；须先于逐瓦片目标销毁）
			if (impl->spot_shadow_atlas_target != 0)
			{
				if (impl->resource_domain != nullptr)
					impl->resource_domain->render_target_destroy(impl->spot_shadow_atlas_target);
				impl->spot_shadow_atlas_target = 0;
			}
			impl->spot_shadow_atlas = false;
			//图集请求仅在已初始化关闭时撤销（保持旧语义）
			if (impl->scene_domain != nullptr)
				impl->scene_domain->state().spot_shadow_atlas_requested = false;
			//归还聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张）
			for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			{
				if (impl->spot_shadow_targets[tile] != 0)
				{
					if (impl->resource_domain != nullptr)
						impl->resource_domain->render_target_destroy(impl->spot_shadow_targets[tile]);
					impl->spot_shadow_targets[tile] = 0;
				}
			}
			//阴影图已失效（第五十五轮）
			impl->shadow_pass_ready = false;
			impl->shadow_signature = 0;
			impl->shadow_pass_count = 0;
		}
		//场景域只复位主机侧场景状态；阴影资源已在上方按原顺序归还
		if (impl->scene_domain != nullptr)
			impl->scene_domain->shutdown();
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
		/*
		资源域（网格 / 纹理 / 材质 / 管线）
		网格 / 纹理依赖设备与内存，须先于两者销毁；域对象可能已随门面析构释放，故按实现体登记判断。
		*/
		if (impl->resource_domain != nullptr)
			impl->resource_domain->shutdown();
		//设备域只复位渲染目标选择、后处理链与参数，不销毁其他域资源
		if (impl->device_domain != nullptr)
			impl->device_domain->shutdown();
		//复位 MSAA 采样数（第九十六轮）
		impl->msaa_samples = 1;
		//复位 MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = true;
		//复位剔除观测
		impl->culled_items = 0;
		//剔除开关恢复默认（开启）
		impl->cull_enabled = true;
		//静态合批恢复默认（开启，第一百二十七轮）
		impl->static_batching = true;
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
		//描边管线句柄随资源域关闭失效（用户参数由场景域复位）
		impl->outline_pipeline = 0;
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
		/*
		计算域（缓冲与管线登记 / 计算上下文 / 健康检查模块）
		依赖内存与着色器模块，须先于二者销毁；域对象可能已随门面析构释放，故按实现体登记判断。
		*/
		if (impl->compute_domain != nullptr)
			impl->compute_domain->shutdown();
		//管线依赖设备与内存，须先于两者销毁
		impl->pipeline.destroy();
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
