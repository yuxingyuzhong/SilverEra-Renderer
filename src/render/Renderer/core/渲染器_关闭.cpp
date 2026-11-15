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
	void Render_Device::shutdown(void)
	{
		//若设备域状态未分配
		if (state_ptr == nullptr)
			return;
		/*
		先回收渲染工作线程（第一百七十七轮）
		并行任务均为同步调用（调用方阻塞到全部块完成），故 shutdown 时不存在在途任务；
		此处直接停止可保证后续任何模块销毁都不会被工作线程访问。
		*/
		state_ptr->thread_pool.threads_stop();
		state_ptr->render_threads = 0;
		//严格逆序销毁（各模块 destroy 幂等，可安全处理半初始化状态；§5.4 / §9）
		/*
		阴影资源（第五十二轮）
		须先于渲染目标与管线模块销毁：阴影管线为门面具名管线（着色器模块所有权在门面）。
		*/
		if (state_ptr->initialized)
		{
			//归还阴影管线（资源域可能已随门面析构释放，此时随管线模块统一销毁）
			if (state_ptr->shadow_pipeline != 0)
			{
				if (state_ptr->owner != nullptr)
					owner->resource().pipeline_unload(state_ptr->shadow_pipeline);
				state_ptr->shadow_pipeline = 0;
			}
			//归还阴影目标
			if (state_ptr->shadow_target != 0)
			{
				if (state_ptr->owner != nullptr)
					owner->resource().render_target_destroy(state_ptr->shadow_target);
				state_ptr->shadow_target = 0;
			}
			//归还聚光阴影图集（第六百七十轮；须先于逐瓦片目标销毁）
			if (state_ptr->spot_shadow_atlas_target != 0)
			{
				if (state_ptr->owner != nullptr)
					owner->resource().render_target_destroy(state_ptr->spot_shadow_atlas_target);
				state_ptr->spot_shadow_atlas_target = 0;
			}
			state_ptr->spot_shadow_atlas = false;
			//图集请求仅在已初始化关闭时撤销（保持旧语义）
			if (state_ptr->owner != nullptr)
				owner->scene().spot_shadow_atlas_request_clear();
			//归还聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张）
			for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			{
				if (state_ptr->spot_shadow_targets[tile] != 0)
				{
					if (state_ptr->owner != nullptr)
						owner->resource().render_target_destroy(state_ptr->spot_shadow_targets[tile]);
					state_ptr->spot_shadow_targets[tile] = 0;
				}
			}
			//阴影图已失效（第五十五轮）
			state_ptr->shadow_pass_ready = false;
			state_ptr->shadow_signature = 0;
			state_ptr->shadow_pass_count = 0;
		}
		//场景域只复位主机侧场景状态；阴影资源已在上方按原顺序归还
		if (state_ptr->owner != nullptr)
			owner->scene().shutdown();
		/*
		粒子 billboard 资源须先于帧 / 网格上下文销毁（第三百一十四轮）
		域对象可能已随门面析构释放（见 Renderer 析构），故按设备域状态中的门面指针判断。
		*/
		if (state_ptr->owner != nullptr)
			owner->particle().shutdown();
		state_ptr->frame.destroy();
		/*
		原生 UI 与输入先于依赖模块销毁
		ui_render 持有每帧槽缓冲（依赖内存模块）；ui / input 只持有主机侧状态，
		但文本输入需在平台视频子系统仍存活时关闭。
		域对象可能已随门面析构释放（见 Renderer 析构），故按设备域状态中的门面指针判断。
		*/
		if (state_ptr->owner != nullptr)
			owner->ui().shutdown();
		/*
		资源域（网格 / 纹理 / 材质 / 管线）
		网格 / 纹理依赖设备与内存，须先于两者销毁；域对象可能已随门面析构释放，故按设备域状态中的门面指针判断。
		*/
		if (state_ptr->owner != nullptr)
			owner->resource().shutdown();
		//设备域只复位渲染目标选择、后处理链与参数，不销毁其他域资源
		output_reset();
		//复位 MSAA 采样数（第九十六轮）
		state_ptr->msaa_samples = 1;
		//复位 MSAA 深度解析开关（第一百九十轮；默认开启）
		state_ptr->msaa_depth_resolve = true;
		//复位剔除观测
		state_ptr->culled_items = 0;
		//剔除开关恢复默认（开启）
		state_ptr->cull_enabled = true;
		//静态合批恢复默认（开启，第一百二十七轮）
		state_ptr->static_batching = true;
		state_ptr->pending_push_constants.clear();
		//场景 UBO 依赖内存模块，须先于 memory 销毁
		if (state_ptr->memory.valid())
			for (size_t slot = 0; slot < state_ptr->scene_ubos.size(); ++slot)
				//归还场景 UBO 与其内存
				state_ptr->memory.buffer_destroy(state_ptr->scene_ubos[slot], state_ptr->scene_ubo_allocs[slot]);
		state_ptr->scene_ubos.clear();
		state_ptr->scene_ubo_allocs.clear();
		//实例缓冲依赖内存模块，须先于 memory 销毁
		if (state_ptr->memory.valid())
			for (size_t slot = 0; slot < state_ptr->instance_buffers.size(); ++slot)
				//归还实例缓冲与其内存
				state_ptr->memory.buffer_destroy(state_ptr->instance_buffers[slot], state_ptr->instance_allocations[slot]);
		state_ptr->instance_buffers.clear();
		state_ptr->instance_allocations.clear();
		//间接命令缓冲依赖内存模块，须先于 memory 销毁（第二百五十二轮）
		if (state_ptr->memory.valid())
			for (size_t slot = 0; slot < state_ptr->indirect_buffers.size(); ++slot)
				//归还间接命令缓冲与其内存
				state_ptr->memory.buffer_destroy(state_ptr->indirect_buffers[slot], state_ptr->indirect_allocations[slot]);
		state_ptr->indirect_buffers.clear();
		state_ptr->indirect_allocations.clear();
		//光源记录缓冲依赖内存模块，须先于 memory 销毁（第二百六十六轮）
		state_ptr->cluster_destroy();
		//图形 set0 簇数据缓冲与占位缓冲依赖内存模块，须先于 memory 销毁（第二百八十八轮）
		state_ptr->cluster_graphics_destroy();
		//描边管线句柄随资源域关闭失效（用户参数由场景域复位）
		state_ptr->outline_pipeline = 0;
		/*
		写回管线缓存（第一百零六轮；可选）
		在销毁管线缓存之前取出字节并写盘；失败仅告警，不影响关闭流程。
		*/
		if (state_ptr->initialized && !state_ptr->config.pipeline_cache_path.empty())
		{
			std::vector<uint8_t> cache_bytes;
			if (owner->resource().pipeline_cache_get(cache_bytes))
			{
				std::ofstream cache_stream(path_from_utf8(state_ptr->config.pipeline_cache_path),
					std::ios::binary | std::ios::trunc);
				if (cache_stream.is_open())
					cache_stream.write(reinterpret_cast<const char*>(cache_bytes.data()),
						static_cast<std::streamsize>(cache_bytes.size()));
			}
		}
		/*
		计算域（缓冲与管线登记 / 计算上下文 / 健康检查模块）
		依赖内存与着色器模块，须先于二者销毁；域对象可能已随门面析构释放，故按设备域状态中的门面指针判断。
		*/
		if (state_ptr->owner != nullptr)
			owner->compute().shutdown();
		//管线与具名管线着色器依赖设备，须先于两者销毁
		owner->resource().pipeline_shutdown();
		//交换链销毁连带销毁深度图像，须排在管线之后
		state_ptr->swapchain.destroy();
		state_ptr->memory.destroy();
		/*
		先平台后设备
		平台持有「借用」的实例句柄，销毁表面（vkDestroySurfaceKHR）要求实例仍在途；
		设备销毁会连带 vkDestroyInstance，故必须排在平台之后。
		*/
		//解除事件泵到 UI 通道的桥接（平台销毁后不应再有回调指向 UI 通道）
		state_ptr->platform.event_forward_clear();
		state_ptr->platform.destroy();
		state_ptr->device.destroy();
		//清空待绘制项与其推送常量存储
		state_ptr->pending_items.clear();
		state_ptr->pending_instances.clear();
		//复位状态
		state_ptr->frame_count = 0;
		state_ptr->initialized = false;
	}
}
