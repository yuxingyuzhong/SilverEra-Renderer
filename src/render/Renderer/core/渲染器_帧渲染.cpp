#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"
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
#include "渲染器_帧内部.h"

namespace engine
{
	namespace
	{
		/*
		UI 阶段绘制入口适配
		帧模块回调的形参为 VkCommandBuffer；界面域公共头保持 Vulkan-free（形参用 void*），故在此转接。
		*/
		void ui_draw_entry_adapter(VkCommandBuffer command_buffer, void* user_data)
		{
			Render_Ui::ui_draw_entry(command_buffer, user_data);
		}
	}

	//每帧推进（可选像素回读）
	bool Renderer_Core::frame_render(uint8_t* readback_pixels, uint32_t readback_capacity,
		uint32_t* readback_width, uint32_t* readback_height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//回读尺寸出参先复位（未兑现时保持零，便于调用方判据）
		if (readback_width != nullptr)
			*readback_width = 0;
		if (readback_height != nullptr)
			*readback_height = 0;

		//泵事件 + 本帧输入锁存与原生 UI 交互
		frame_stage_input_events(self_ptr, impl.get());
		//UI 阶段统一回调（原生 UI 先绘制，外部 UI 后绘制）
		Frame_Ui_Callback ui_callback{};
		ui_callback.fn = &ui_draw_entry_adapter;
		ui_callback.user_data = &self().ui();
		/*
		② 若窗口请求退出则交调用方收敛
		③ 若窗口最小化则挂起本帧（避免 0×0 交换链；K7）
		两种挂起情形都不消费待绘制项，故一并清空，避免连续挂起期间无限累积。
		*/
		if (impl->platform.close_requested() || impl->platform.minimized())
		{
			impl->pending_items.clear();
			impl->pending_instances.clear();
			return true;
		}

		//取当前窗口客户区尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		impl->platform.window_size_get(width, height);

		//本帧是否已重建交换链（K8：每帧最多一次）
		bool rebuilt = false;
		//④ 若窗口尺寸已变化则重建交换链
		if (impl->platform.resized_consume())
		{
			//尺寸为零则不重建
			if (width != 0 && height != 0)
			{
				//重建交换链并把旧资源压入帧延迟队列；失败仅告警，本帧继续
				if (!impl->swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（本帧继续）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(), impl->swapchain.depth_format_get(), impl->msaa_samples))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
				rebuilt = true;
			}
		}

		//交换链尺寸可能已变化，刷新相机宽高比
		{
			VkExtent2D extent = impl->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				impl->camera.aspect_set(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		//光源空间矩阵与级联拟合
		frame_stage_shadow_matrices(impl.get());
		//聚光灯阴影矩阵与本帧有效标志
		const bool spot_shadow_light_valid = frame_stage_spot_shadow_matrices(impl.get());
		//阴影 pass（输入签名不变时复用上一帧阴影图）
		if (!frame_stage_shadow_pass(impl.get(), spot_shadow_light_valid))
			return false;

		/*
		⑤ 取本帧帧槽序号（与帧模块内部一致），把相机视图 × 投影写入该槽 UBO 并绑定 set0；
		再为待绘制项填装 set0 / set1 描述符集（descriptor_set_count = 2）。
		推送常量恒为列主序 16 浮点模型矩阵（mesh_draw 已写入对应的 pending_transforms 存储）。
		*/
		uint32_t slot = impl->frame.slot_index_get();
		/*
		等待本帧槽前一次提交完成
		必须在写本帧槽 UBO 之前等待，否则可能覆盖 GPU 仍在读取的上一轮 UBO（真实竞态）；
		帧模块内部还会再等一次，届时栅栏已发出，vkWaitForFences 立即返回，无副作用。
		*/
		if (!impl->frame.slot_wait())
		{
			logger.error("渲染器:: 等待本帧帧槽失败");
			return false;
		}
		/*
		⑤a 光源记录上传（第二百六十六轮；仅 Cluster 开启时）
		该帧槽上一轮已提交完成，可安全覆盖前 count × 80 字节；记录数为 267 起分簇的输入。
		*/
		if (impl->compute_domain->state().cluster_enabled && impl->cluster_ensure())
		{
			//本帧记录数（含主方向光与补光；按容量截断）
			uint32_t record_count = self().light_record_count();
			if (record_count > LIGHT_RECORD_CAPACITY)
				record_count = LIGHT_RECORD_CAPACITY;
			//组装缓冲按记录数调整后导出快照
			impl->light_records_scratch.resize(record_count == 0 ? 1u : record_count);
			uint32_t written = self().light_records_get(impl->light_records_scratch.data(), record_count);
			//写入该帧槽缓冲（仅写入有效记录区间）
			if (written != 0
				&& !impl->memory.buffer_write(impl->light_buffers[slot], impl->light_allocations[slot],
					impl->light_records_scratch.data(),
					static_cast<VkDeviceSize>(written) * LIGHT_RECORD_BYTES, 0))
			{
				logger.error("渲染器:: 光源记录上传失败（帧槽 {}）", slot);
				return false;
			}
			impl->compute_domain->state().last_cluster_light_count = written;
		}
		else
			impl->compute_domain->state().last_cluster_light_count = 0;
		/*
		⑤b 簇数据整备（第二百七十三轮；仅 Cluster 开启时）
		每帧重建计数 / 偏移 / 列表并写入观测；关闭时观测清零、零分配（画面路径不受影响）。
		*/
		if (impl->compute_domain->state().cluster_enabled && impl->cluster_ensure())
		{
			//网格总簇数与光源记录数（列表容量上界 = 簇数 × 记录数）
			uint32_t cluster_total = cluster_grid_clusters(impl->compute_domain->state().cluster_grid);
			uint32_t light_records = self().light_record_count();
			uint32_t list_capacity = cluster_total * light_records;
			impl->cluster_counts_scratch.resize(cluster_total);
			impl->cluster_offsets_scratch.resize(cluster_total);
			impl->cluster_list_scratch.resize(list_capacity);
			uint32_t entries = 0;
			//整备（当前 ≤8 盏规模下 CPU 远快于 GPU 同步回读，帧内默认走 CPU；见 276 轮性能对照）
			if (!self().compute().cluster_data_build(impl->cluster_counts_scratch.data(), impl->cluster_offsets_scratch.data(),
				impl->cluster_list_scratch.data(), cluster_total, list_capacity, &entries, false))
				logger.warn("渲染器:: 簇数据整备失败（帧 {}）", impl->frame_count);
			else
			{
				/*
				图形 set0 簇数据上传（第二百八十八轮）
				计数与偏移按簇数整块上传、列表按条目数上传；缓冲容量不足时由 ensure 先等尽在途帧再重建。
				*/
				if (!impl->cluster_graphics_ensure(cluster_total, list_capacity))
					logger.warn("渲染器:: 簇数据缓冲就绪失败（帧 {}）", impl->frame_count);
				else if (!impl->memory.buffer_write(impl->cluster_counts_buffers[slot],
					impl->cluster_counts_allocations[slot], impl->cluster_counts_scratch.data(),
					static_cast<VkDeviceSize>(cluster_total) * 4, 0)
					|| !impl->memory.buffer_write(impl->cluster_offsets_buffers[slot],
					impl->cluster_offsets_allocations[slot], impl->cluster_offsets_scratch.data(),
					static_cast<VkDeviceSize>(cluster_total) * 4, 0)
					|| !impl->memory.buffer_write(impl->cluster_list_buffers[slot],
					impl->cluster_list_allocations[slot], impl->cluster_list_scratch.data(),
					static_cast<VkDeviceSize>(entries) * 4, 0))
					logger.warn("渲染器:: 簇数据上传失败（帧 {}）", impl->frame_count);
				else
					impl->last_cluster_gpu_bytes = cluster_total * 8 + entries * 4;
			}
		}
		else
		{
			//关闭时观测清零
			impl->compute_domain->state().last_cluster_entries = 0;
			impl->compute_domain->state().last_cluster_build_ms = 0.0;
			impl->last_cluster_gpu_bytes = 0;
		}
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		//推进文字子系统帧时钟并归还已淘汰且无在途引用的网格（第四十八轮）
		self().ui().state().text.frame_begin(impl->config.frame_overlap);
		//场景 UBO 组装与上传
		if (!frame_stage_scene_ubo(impl.get(), slot, view_proj, spot_shadow_light_valid))
			return false;
		/*
		本帧槽 set0 无需重写
		各帧槽的场景 UBO 缓冲句柄自 init 起恒定不变，set0 已在 init 时一次性写入；
		每帧只更新 UBO 内容（上一段的 buffer_write），不重写描述符集。
		*/
		/*
		同帧多 pass 后处理准备
		若本帧设定「渲染到目标并同帧后处理呈现」，先确保后处理资源（全屏网格；单级路径另需默认后处理管线）就绪，
		并把「单位模型矩阵」追加到本帧实例存储末尾（后处理全屏四边形以此为实例缓冲来源）。
		追加须在下方「实例缓冲写入」之前完成，故此处先行。
		路径：post_chain 非空 → 链路径（须 post_mesh_ensure；链长 ≥ 2 还须第二目标）；
		      post_chain 为空 → 既有单级默认路径（post_resources_ensure）。
		*/
		bool post_frame = false;
		//本帧是否走后处理链路径（false 且 post_frame → 既有单级默认路径）
		bool post_chain_mode = false;
		//后处理绘制项在本帧实例缓冲内的字节偏移
		VkDeviceSize post_instance_offset = 0;
		//若已设定同帧后处理呈现目标
		if (impl->device_domain->state().present_target != 0)
		{
			//本帧实例存储须仍有空位（后处理全屏四边形再占一个实例；满则回落原路径，避免实例缓冲越界写）
			if (impl->pending_instances.size() >= impl->instance_capacity)
				logger.warn("渲染器:: 本帧实例存储已满，同帧后处理呈现回落原路径");
			//链长 ≥ 2 但目标组不足 2 个则回落单级默认路径
			else if (!impl->device_domain->state().post_chain.empty() && impl->device_domain->state().post_chain.size() >= 2 && impl->device_domain->state().present_extra.empty())
				logger.warn("渲染器:: 后处理链级数 ≥ 2 但中间目标不足 2 个，本帧回落单级默认后处理");
			else
			{
				//本帧是否走链路径
				bool chain_mode = !impl->device_domain->state().post_chain.empty();
				//确保后处理资源就绪（链 → 全屏网格；单级 → 全屏网格 ＋ 默认后处理管线）
				bool resources_ready = chain_mode ? impl->post_mesh_ensure() : impl->post_resources_ensure();
				//若资源就绪
				if (resources_ready)
				{
					//追加单位矩阵（列主序；材质四浮点为零 = 不覆盖全局 PBR）并记录其字节偏移
					post_instance_offset = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					impl->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = impl->pending_instances.back();
					identity.fill(0.0f);
					identity[0] = 1.0f;
					identity[5] = 1.0f;
					identity[10] = 1.0f;
					identity[15] = 1.0f;
					post_frame = true;
					post_chain_mode = chain_mode;
				}
				//资源未就绪则本帧回落原路径
				else
					logger.warn("渲染器:: 后处理资源未就绪，本帧回落原路径");
			}
		}
		/*
		SSAA 超采样准备（倍率 > 1）
		与后处理 / 离屏路径互斥（优先级最低）：两者任一启用时本帧不走 SSAA。
		就绪后追加一个单位实例供降采样全屏四边形使用；两 pass 的实际录制在下方分派段完成。
		*/
		bool ssaa_frame = false;
		//降采样 pass 在本帧实例缓冲内的字节偏移
		VkDeviceSize ssaa_instance_offset = 0;
		//若启用 SSAA 且未走后处理 / 离屏路径
		if (impl->device_domain->state().ssaa_factor > 1 && impl->device_domain->state().present_target == 0 && impl->device_domain->state().active_target == 0)
		{
			//确保降采样管线 / 全屏网格 / 默认后处理管线 / 超采样目标就绪
			if (impl->ssaa_pipeline_ensure() && impl->post_mesh_ensure() && impl->post_pipeline_ensure()
				&& impl->ssaa_targets_ensure(width, height, impl->device_domain->state().ssaa_factor))
			{
				//本帧实例存储须仍有空位（降采样全屏四边形再占一个实例）
				if (impl->pending_instances.size() < impl->instance_capacity)
				{
					ssaa_instance_offset = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					impl->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = impl->pending_instances.back();
					identity.fill(0.0f);
					identity[0] = 1.0f;
					identity[5] = 1.0f;
					identity[10] = 1.0f;
					identity[15] = 1.0f;
					ssaa_frame = true;
				}
				//实例存储已满则回落
				else
					logger.warn("渲染器:: 本帧实例存储已满，SSAA 回落原路径");
			}
			//资源未就绪则回落原路径
			else
				logger.warn("渲染器:: SSAA 资源未就绪，本帧回落原路径");
		}
		/*
		天空盒准备（第一百二十五轮）
		启用且全屏网格与天空管线就绪时，追加一个单位实例供全屏天空项使用；
		该实例与后处理实例同为追加到本帧实例存储末尾的单位矩阵，故须在下方「实例缓冲写入」之前完成。
		*/
		bool sky_frame = false;
		//天空项在本帧实例缓冲内的字节偏移
		VkDeviceSize sky_instance_offset = 0;
		if (impl->resource_domain->state().sky_enabled)
		{
			//本帧实例存储须仍有空位，且全屏网格与天空管线就绪
			if (impl->pending_instances.size() < impl->instance_capacity
				&& impl->post_mesh_ensure() && impl->sky_pipeline_ensure())
			{
				sky_instance_offset = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
				impl->pending_instances.emplace_back();
				std::array<float, INSTANCE_FLOAT_COUNT>& identity = impl->pending_instances.back();
				identity.fill(0.0f);
				identity[0] = 1.0f;
				identity[5] = 1.0f;
				identity[10] = 1.0f;
				identity[15] = 1.0f;
				sky_frame = true;
			}
			//实例存储已满或资源未就绪则跳过本帧天空背景
			else
				logger.warn("渲染器:: 天空盒资源未就绪，本帧跳过天空背景");
		}
		//实例模型矩阵写入本帧槽实例缓冲
		if (!frame_stage_instance_buffers(impl.get(), slot))
			return false;

		/*
		静态合批（第一百二十七轮；第一百二十九轮抽为主 pass / 阴影 pass 共用工具）
		仅合并相邻等价项，绘制顺序与实例顺序不变，画面逐字节一致。
		*/
		if (impl->static_batching)
			impl->frame_merged_items += batch_adjacent_items(impl->pending_items, impl->batch_scratch);

		frame_stage_descriptor_sets(impl.get(), slot);
		//追加天空盒绘制项
		frame_stage_sky_item(impl.get(), slot, view_proj, sky_instance_offset, sky_frame);

		//分派渲染路径（SSAA / 多 pass 后处理 / 离屏 / 交换链）并交付回读
		Frame_Dispatch_Input dispatch{};
		dispatch.width = width;
		dispatch.height = height;
		dispatch.slot = slot;
		dispatch.rebuilt = rebuilt;
		dispatch.post_frame = post_frame;
		dispatch.post_chain_mode = post_chain_mode;
		dispatch.ssaa_frame = ssaa_frame;
		dispatch.post_instance_offset = post_instance_offset;
		dispatch.ssaa_instance_offset = ssaa_instance_offset;
		dispatch.view_proj = view_proj;
		dispatch.ui_callback = ui_callback;
		dispatch.readback_pixels = readback_pixels;
		dispatch.readback_capacity = readback_capacity;
		dispatch.readback_width = readback_width;
		dispatch.readback_height = readback_height;
		if (!frame_stage_dispatch(impl.get(), dispatch))
			return false;

		//⑥ 帧计数自增
		++impl->frame_count;
		return true;
	}

}
