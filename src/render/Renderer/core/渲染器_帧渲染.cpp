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

	/*
	场景退场回调（第二百五十四轮）
	把当帧场景深度经深度拷贝 compute 写入当前帧槽历史，并逐级生成金字塔；
	调用入口在帧模块的场景动态渲染通道之外，进入 / 退出时深度均为深度附件布局。
	*/
	void Renderer_Impl_Holder::Impl::hiz_post_scene_entry(VkCommandBuffer command_buffer, VkImage depth_image, VkImageView depth_view,
		VkExtent2D extent, void* user_data)
	{
		//用户数据为本实现体
		Renderer_Impl_Holder::Impl* self = static_cast<Renderer_Impl_Holder::Impl*>(user_data);
		//未启用、资源缺失或管线未就绪时跳过
		if (self == nullptr || !self->hiz_enabled || !self->hiz.valid()
			|| self->hiz_depth_copy_pipeline == 0 || self->hiz_downsample_pipeline == 0)
			return;
		//尺寸与资源不一致（交换链刚重建）时跳过本帧
		if (self->hiz.width_get() != extent.width || self->hiz.height_get() != extent.height)
			return;
		//当前帧槽
		const uint32_t slot = self->frame.slot_index_get();
		//录制深度拷贝与逐级降采样（失败仅告警，不影响出帧）
		if (!self->hiz.pyramid_generate(command_buffer, slot, depth_image, depth_view,
			self->hiz_depth_copy_pipeline, self->hiz_downsample_pipeline))
			logger.warn("渲染器:: Hi-Z 金字塔生成失败（帧槽 {}）", slot);
		else
			//记录最近生成完成的帧槽（第二百五十六轮：剔除读取该槽上一帧金字塔）
			self->last_hiz_slot = slot;
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
		frame_stage_input_events(impl.get());
		//UI 阶段统一回调（原生 UI 先绘制，外部 UI 后绘制）
		Frame_Ui_Callback ui_callback{};
		ui_callback.fn = &Renderer_Impl_Holder::Impl::ui_draw_entry;
		ui_callback.user_data = impl.get();
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
		if (impl->cluster_enabled && impl->cluster_ensure())
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
			impl->last_cluster_light_count = written;
		}
		else
			impl->last_cluster_light_count = 0;
		/*
		⑤b 簇数据整备（第二百七十三轮；仅 Cluster 开启时）
		每帧重建计数 / 偏移 / 列表并写入观测；关闭时观测清零、零分配（画面路径不受影响）。
		*/
		if (impl->cluster_enabled && impl->cluster_ensure())
		{
			//网格总簇数与光源记录数（列表容量上界 = 簇数 × 记录数）
			uint32_t cluster_total = cluster_grid_clusters(impl->cluster_grid);
			uint32_t light_records = self().light_record_count();
			uint32_t list_capacity = cluster_total * light_records;
			impl->cluster_counts_scratch.resize(cluster_total);
			impl->cluster_offsets_scratch.resize(cluster_total);
			impl->cluster_list_scratch.resize(list_capacity);
			uint32_t entries = 0;
			//整备（当前 ≤8 盏规模下 CPU 远快于 GPU 同步回读，帧内默认走 CPU；见 276 轮性能对照）
			if (!self().cluster_data_build(impl->cluster_counts_scratch.data(), impl->cluster_offsets_scratch.data(),
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
			impl->last_cluster_entries = 0;
			impl->last_cluster_build_ms = 0.0;
			impl->last_cluster_gpu_bytes = 0;
		}
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		//推进文字子系统帧时钟并归还已淘汰且无在途引用的网格（第四十八轮）
		impl->text.frame_begin(impl->config.frame_overlap);
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
		if (impl->present_target != 0)
		{
			//本帧实例存储须仍有空位（后处理全屏四边形再占一个实例；满则回落原路径，避免实例缓冲越界写）
			if (impl->pending_instances.size() >= impl->instance_capacity)
				logger.warn("渲染器:: 本帧实例存储已满，同帧后处理呈现回落原路径");
			//链长 ≥ 2 但目标组不足 2 个则回落单级默认路径
			else if (!impl->post_chain.empty() && impl->post_chain.size() >= 2 && impl->present_extra.empty())
				logger.warn("渲染器:: 后处理链级数 ≥ 2 但中间目标不足 2 个，本帧回落单级默认后处理");
			else
			{
				//本帧是否走链路径
				bool chain_mode = !impl->post_chain.empty();
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
		if (impl->ssaa_factor > 1 && impl->present_target == 0 && impl->active_target == 0)
		{
			//确保降采样管线 / 全屏网格 / 默认后处理管线 / 超采样目标就绪
			if (impl->ssaa_pipeline_ensure() && impl->post_mesh_ensure() && impl->post_pipeline_ensure()
				&& impl->ssaa_targets_ensure(width, height, impl->ssaa_factor))
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
		if (impl->sky_enabled)
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
		/*
		实例模型矩阵写入本帧槽实例缓冲
		本帧登记的全部实例矩阵按登记顺序连续存放，故一次写入即可；
		随后每个渲染项只携带「帧槽缓冲句柄 ＋ 字节偏移 ＋ 实例数」。
		*/
		if (!impl->pending_instances.empty())
		{
			//本帧实例属性总字节数（一个实例 = 20 浮点 = 80 字节）
			VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
			/*
			实例缓冲按需写入（第六十一轮）
			逐帧槽缓存上次写入的实例属性，按「一个实例 = 80 字节」为比较单元；
			连续变化实例合并为一段逐段写入；首次访问该帧槽整块写入本帧全部实例。
			写入内容与整块写逐字节一致，故画面不变。
			*/
			const std::size_t cache_slot_count = impl->instance_buffers.size();
			const std::size_t cache_slot_floats = static_cast<std::size_t>(impl->instance_capacity) * INSTANCE_FLOAT_COUNT;
			//若缓存规模与帧槽 / 容量不一致则重建
			if (impl->instance_block_cache.size() != cache_slot_count * cache_slot_floats)
			{
				impl->instance_block_cache.assign(cache_slot_count * cache_slot_floats, 0.0f);
				impl->instance_block_count.assign(cache_slot_count, 0);
				impl->instance_block_cached.assign(cache_slot_count, 0);
			}
			//本帧槽缓存段
			float* instance_cache = impl->instance_block_cache.data() + static_cast<std::size_t>(slot) * cache_slot_floats;
			//上次已写入的实例数
			const uint32_t cached_count = impl->instance_block_count[slot];
			//本帧实例数
			const std::size_t instance_count = impl->pending_instances.size();
			//本帧实际上传字节数
			std::size_t uploaded_bytes = 0;
			//若该帧槽尚未建立缓存
			if (impl->instance_block_cached[slot] == 0)
			{
				//整块写入
				if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
					impl->pending_instances.data(), instance_bytes, 0))
				{
					logger.error("渲染器:: 实例缓冲写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes = static_cast<std::size_t>(instance_bytes);
			}
			else
			{
				/*
				差异掩码（第一百七十七轮）
				逐实例只写自己的掩码槽，故可分块并行；后续连续段扫描仍单线程按掩码进行，
				扫描结果与逐实例比较完全一致（上传区间不变）。
				*/
				impl->instance_changed_mask.assign(instance_count, 0);
				//判定单实例是否变化（只读本帧实例与上轮缓存，写自己的掩码槽）
				auto mark_changed_instance = [this, cached_count, instance_cache](std::size_t begin, std::size_t end)
				{
					for (std::size_t index = begin; index < end; ++index)
					{
						//超出上次缓存长度的实例一律视为变化
						bool changed = index >= cached_count;
						//未超出则逐浮点比较
						for (std::size_t offset = 0; !changed && offset < INSTANCE_FLOAT_COUNT; ++offset)
							if (impl->pending_instances[index][offset] != instance_cache[index * INSTANCE_FLOAT_COUNT + offset])
								changed = true;
						impl->instance_changed_mask[index] = changed ? 1 : 0;
					}
				};
				//本帧生效的扫描阈值（默认 512；可由配置调整，第二百二十六轮）
				const std::size_t scan_parallel_min = impl->instance_scan_parallel_min;
				//实例数达到阈值且工作线程可用时并行判定，否则由调用线程整段执行
				if (impl->thread_pool.active() && instance_count >= scan_parallel_min)
				{
					//派发计数差值（第二百二十四轮：按用途归入差异扫描）
					const uint64_t dispatch_before = impl->thread_pool.dispatch_count_get();
					impl->thread_pool.ranges_run(instance_count, scan_parallel_min, mark_changed_instance);
					impl->scan_parallel_dispatches += impl->thread_pool.dispatch_count_get() - dispatch_before;
				}
				else
					mark_changed_instance(0, instance_count);
				//当前变化段的起始实例与是否处于段内
				std::size_t run_start = 0;
				bool in_run = false;
				//按掩码扫描并合并连续变化区间
				for (std::size_t index = 0; index < instance_count; ++index)
				{
					//该实例本帧是否变化
					const bool changed = impl->instance_changed_mask[index] != 0;
					//若变化段遇到未变化实例则收束并写入
					if (!changed && in_run)
					{
						const std::size_t segment_bytes = (index - run_start) * INSTANCE_STRIDE_BYTES;
						if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
							impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
						{
							logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
							return false;
						}
						uploaded_bytes += segment_bytes;
						in_run = false;
					}
					//若未在段内且该实例变化则开启新段
					if (changed && !in_run)
					{
						run_start = index;
						in_run = true;
					}
				}
				//收尾：最后一段延伸到本帧实例末尾
				if (in_run)
				{
					const std::size_t segment_bytes = (instance_count - run_start) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[slot], impl->instance_allocations[slot],
						impl->pending_instances.data() + run_start, segment_bytes, run_start * INSTANCE_STRIDE_BYTES))
					{
						logger.error("渲染器:: 实例缓冲分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
				}
			}
			/*
			更新该帧槽缓存（与已写入内容一致；第二百一十九轮改为按块并行）
			每个实例只写自己的 20 浮点槽，块间区间不重叠，故可分块并行；
			结果与单线程逐元素拷贝完全一致，仅降低大批量实例时的主机耗时。
			*/
			auto refresh_instance_cache = [instance_cache](const std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& pending,
				std::size_t begin, std::size_t end)
			{
				for (std::size_t index = begin; index < end; ++index)
				{
					for (std::size_t offset = 0; offset < INSTANCE_FLOAT_COUNT; ++offset)
						instance_cache[index * INSTANCE_FLOAT_COUNT + offset] = pending[index][offset];
				}
			};
			//本帧生效的扫描 / 回写阈值（默认 512；可由配置调整，第二百二十六轮）
			const std::size_t scan_parallel_min = impl->instance_scan_parallel_min;
			//实例数达到阈值且工作线程可用时并行回写，否则由调用线程整段执行
			if (impl->thread_pool.active() && instance_count >= scan_parallel_min)
			{
				//派发计数差值（第二百二十四轮：按用途归入缓存回写）
				const uint64_t dispatch_before = impl->thread_pool.dispatch_count_get();
				impl->thread_pool.ranges_run(instance_count, scan_parallel_min,
					[this, &refresh_instance_cache](std::size_t begin, std::size_t end)
					{
						refresh_instance_cache(impl->pending_instances, begin, end);
					});
				impl->cache_parallel_dispatches += impl->thread_pool.dispatch_count_get() - dispatch_before;
			}
			else
				refresh_instance_cache(impl->pending_instances, 0, instance_count);
			impl->instance_block_count[slot] = static_cast<uint32_t>(instance_count);
			impl->instance_block_cached[slot] = 1;
			//累计上传字节（观测用）
			impl->instance_upload_bytes += uploaded_bytes;
		}

		/*
		静态合批（第一百二十七轮；第一百二十九轮抽为主 pass / 阴影 pass 共用工具）
		仅合并相邻等价项，绘制顺序与实例顺序不变，画面逐字节一致。
		*/
		if (impl->static_batching)
			impl->frame_merged_items += batch_adjacent_items(impl->pending_items, impl->batch_scratch);

		frame_stage_descriptor_sets(impl.get(), slot);
		/*
		追加天空盒绘制项（第一百二十五轮）
		复用后处理全屏网格与场景帧槽描述符集；推送常量 offset 0 为逆视图投影矩阵，
		片元据此把 NDC 反推为世界方向并采样 binding 5 的立方体贴图。
		*/
		if (sky_frame)
		{
			VkPipeline sky_pipeline_object = impl->pipeline.pipeline_by_handle_get(impl->sky_pipeline);
			VkBuffer sky_mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize sky_mesh_index_offset = 0;
			uint32_t sky_mesh_index_count = 0;
			//若管线与全屏网格齐备
			if (sky_pipeline_object != VK_NULL_HANDLE
				&& impl->mesh.mesh_record_get(impl->fullscreen_mesh, sky_mesh_buffer,
					sky_mesh_index_offset, sky_mesh_index_count))
			{
				//推送常量（16 浮点逆视图投影矩阵）
				impl->pending_push_constants.emplace_back();
				std::array<float, 32>& sky_push = impl->pending_push_constants.back();
				sky_push.fill(0.0f);
				glm::mat4 inverse_view_proj = glm::inverse(glm::make_mat4(view_proj));
				const float* inverse_data = glm::value_ptr(inverse_view_proj);
				for (std::size_t index = 0; index < 16; ++index)
					sky_push[index] = inverse_data[index];
				//天空项（全屏四边形 ＋ 天空管线 ＋ 单位实例；不参与阴影投影）
				Frame_Render_Item sky_item{};
				sky_item.pipeline = sky_pipeline_object;
				sky_item.layout = impl->pipeline.layout_get();
				sky_item.scene_set = impl->pipeline.scene_set_get(slot);
				sky_item.sampler_set = impl->pipeline.sampler_set_get(slot);
				sky_item.descriptor_set_count = 2;
				sky_item.vertex_buffer = sky_mesh_buffer;
				sky_item.index_buffer = sky_mesh_buffer;
				sky_item.index_offset = sky_mesh_index_offset;
				sky_item.index_count = sky_mesh_index_count;
				sky_item.instance_buffer = impl->instance_buffers[slot];
				sky_item.instance_offset = sky_instance_offset;
				sky_item.instance_count = 1;
				sky_item.push_constants = sky_push.data();
				sky_item.push_constant_size = static_cast<uint32_t>(sizeof(float) * 16);
				sky_item.cast_shadow = false;
				sky_item.sample_count = impl->pipeline.pipeline_sample_count(impl->sky_pipeline);
				impl->pending_items.push_back(sky_item);
			}
		}

		//交给帧模块走取图像 / 录制 / 提交 / 呈现（可选像素回读）
		const float* clear_color = impl->clear_color;
		bool rebuild_needed = false;
		//像素回读请求（仅当调用方给出目标缓冲时组装）
		Frame_Readback_Request readback_request{};
		Frame_Readback_Request* readback_ptr = nullptr;
		//若调用方请求回读
		if (readback_pixels != nullptr)
		{
			readback_request.pixels = readback_pixels;
			readback_request.capacity = readback_capacity;
			readback_ptr = &readback_request;
		}

		/*
		分派渲染路径
		① 同帧多 pass 后处理（present_target 有效且资源就绪）：渲染到目标并在同帧内后处理呈现到交换链
		   （post_chain_mode → N 级后处理链；否则 → 既有单级默认后处理）；
		② 离屏路径（active_target 有效）：把场景渲染进目标、不呈现到窗口；
		③ 交换链路径（默认）：取图像 → 渲染 → 呈现。
		三路共用同一批帧槽与帧计数轮转，且互斥（后处理优先于离屏，离屏优先于交换链）。
		*/
		bool offscreen = false;
		/*
		SSAA 路径：场景 → N 倍目标，降采样 → 解析目标，再默认后处理 → 交换链
		两级后处理共用同一全屏几何与场景 UBO；目标按 targets[级序 % 2] 轮转，
		故第 0 级取 N 倍场景目标、第 1 级取解析目标（与帧模块轮转规则一致）。
		*/
		if (ssaa_frame)
		{
			//两个目标：场景目标（N 倍）与解析目标（输出尺寸）；复用缓冲避免逐帧堆分配（第一百九十四轮）
			std::vector<Frame_Render_Target>& targets = impl->chain_targets_scratch;
			targets.assign(2, Frame_Render_Target{});
			//全屏四边形网格缓冲信息
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//取两个目标的附件与全屏网格
			bool ready = impl->render_target.target_get(impl->ssaa_scene_target, targets[0].color_image,
				targets[0].color_view, targets[0].depth_view, targets[0].extent)
				&& impl->render_target.target_get(impl->ssaa_resolve_target, targets[1].color_image,
					targets[1].color_view, targets[1].depth_view, targets[1].extent)
				&& impl->render_target.target_msaa_get(impl->ssaa_scene_target, targets[0].msaa_color_image,
					targets[0].msaa_color_view, targets[0].msaa_depth_view, targets[0].sample_count)
				&& impl->render_target.target_msaa_get(impl->ssaa_resolve_target, targets[1].msaa_color_image,
					targets[1].msaa_color_view, targets[1].msaa_depth_view, targets[1].sample_count)
				&& impl->mesh.mesh_record_get(impl->fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//并行录制格式（第二百轮；SSAA 目标与交换链同格式）
			targets[0].color_format = impl->swapchain.format_get();
			targets[0].depth_format = impl->swapchain.depth_format_get();
			//取共享采样器（用于写后处理采样集）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler shared_sampler = VK_NULL_HANDLE;
			if (ready && !impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
			{
				logger.error("渲染器:: SSAA 失败：共享采样器不可用");
				ready = false;
			}
			//若前置就绪
			if (ready)
			{
				//把两个目标写入各帧槽后处理采样集（签名未变则跳过，避免更新在途描述符集）
				VkImageView post_views[2] = { targets[0].color_view, targets[1].color_view };
				if (!impl->post_sets_bind(post_views, 2, shared_sampler))
				{
					logger.error("渲染器:: SSAA 失败：后处理采样集绑定失败");
					ready = false;
				}
				//两级管线：0 = 降采样（N 倍 → 输出），1 = 默认后处理（输出 → 交换链）
				uint64_t stage_pipelines[2] = { impl->ssaa_pipeline, impl->post_pipeline };
				//后处理绘制项复用缓冲（第一百八十二轮）
				std::vector<Frame_Render_Item>& post_items = impl->post_items_scratch;
				post_items.clear();
				bool items_ready = true;
				for (uint32_t index = 0; index < 2; ++index)
				{
					//本级图形管线（失效即整帧回落）
					VkPipeline stage_pipeline = impl->pipeline.pipeline_by_handle_get(stage_pipelines[index]);
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项（全屏四边形 ＋ 本级管线 ＋ set0 场景 UBO / set1 对应源采样集）
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = impl->pipeline.layout_get();
					post_item.scene_set = impl->pipeline.scene_set_get(slot);
					post_item.sampler_set = impl->pipeline.post_set_get(slot, index);
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = impl->instance_buffers[slot];
					post_item.instance_offset = ssaa_instance_offset;
					post_item.instance_count = 1;
					//该级管线实际采样数（决定宿附件是否为多重采样）
					post_item.sample_count = impl->pipeline.pipeline_sample_count(stage_pipelines[index]);
					post_items.push_back(post_item);
				}
				//若两级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ 降采样 ＋ 默认后处理 ＋ UI ＋ 呈现
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(), 2,
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr, &ui_callback))
					{
						logger.error("渲染器:: SSAA 帧渲染失败");
						return false;
					}
				}
				//含失效管线则回落原路径
				else
					logger.warn("渲染器:: SSAA 管线失效，本帧回落原路径");
			}
			//前置不完整则回落原路径
			else
				logger.warn("渲染器:: SSAA 前置不完整，本帧回落原路径");
		}
		//若走同帧多 pass 后处理路径
		if (!offscreen && post_frame)
		{
			/*
			中间目标组
			首项为场景目标（present_target），其余为额外中间目标（present_extra）；
			链上第 i 级取 targets[i % 目标数]，故链长 ≥ 2 时目标数必须 ≥ 2（上方已保证）。
			*/
			std::vector<Frame_Render_Target>& targets = impl->chain_targets_scratch;
			//按目标组数量重置元素（容量保留，避免逐帧堆分配；第一百九十四轮）
			targets.assign(1 + impl->present_extra.size(), Frame_Render_Target{});
			//全屏四边形网格的共用缓冲 / 索引区偏移 / 索引数
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//场景目标与全屏网格均就绪才可走该路径
			bool ready = impl->render_target.target_get(impl->present_target, targets[0].color_image,
				targets[0].color_view, targets[0].depth_view, targets[0].extent)
				&& impl->render_target.target_msaa_get(impl->present_target, targets[0].msaa_color_image,
					targets[0].msaa_color_view, targets[0].msaa_depth_view, targets[0].sample_count)
				&& impl->mesh.mesh_record_get(impl->fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//并行录制格式（第二百轮；场景目标与交换链同格式）
			targets[0].color_format = impl->swapchain.format_get();
			targets[0].depth_format = impl->swapchain.depth_format_get();
			/*
			MSAA 深度解析目标（第一百九十轮）
			开启深度解析时取场景目标的多重采样深度图像，帧模块据此在场景 pass 结束时
			把深度解析到 targets[0].depth_image（后处理阶段采样那张）；
			关闭或目标无多重采样深度时留空（帧模块跳过解析，保持既有行为）。
			*/
			if (ready && impl->msaa_depth_resolve)
				impl->render_target.target_msaa_depth_image_get(impl->present_target, targets[0].msaa_depth_image);
			//逐个取额外中间目标附件
			for (std::size_t each = 1; ready && each < targets.size(); ++each)
				ready = impl->render_target.target_get(impl->present_extra[each - 1], targets[each].color_image,
					targets[each].color_view, targets[each].depth_view, targets[each].extent)
					&& impl->render_target.target_msaa_get(impl->present_extra[each - 1], targets[each].msaa_color_image,
						targets[each].msaa_color_view, targets[each].msaa_depth_view, targets[each].sample_count);
			/*
			目标尺寸预检（第十七轮放宽）
			后处理片元按「源 / 宿尺寸」推送做像素映射，故目标**不再要求等于交换链范围**；
			仅要求每个目标尺寸非零（零尺寸无法建立视口）。
			*/
			if (ready)
			{
				//逐目标检查尺寸非零
				for (std::size_t each = 0; each < targets.size(); ++each)
				{
					//若该目标尺寸为零
					if (targets[each].extent.width == 0 || targets[each].extent.height == 0)
					{
						logger.warn("渲染器:: 第 {} 个后处理目标尺寸为零，本帧回落原路径", each);
						ready = false;
						break;
					}
				}
			}
			//取共享采样器（把「各级源」写入后处理采样集）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler shared_sampler = VK_NULL_HANDLE;
			if (ready && !impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
			{
				logger.error("渲染器:: 后处理失败：共享采样器不可用");
				ready = false;
			}
			//若前置就绪
			if (ready)
			{
				/*
				把各目标图像写入各帧槽后处理采样器集（下标 = 目标组内序号）
				binding 0 = 该级源；binding 1 恒写目标组首项（场景目标），供双源片元（泛光叠加）采样原画面；
				签名未变时为空操作（避免逐帧更新在途描述符集）。
				视图数组用固定容量栈数组（第一百八十七轮；目标数上限 PRESENT_TARGET_MAX）。
				*/
				VkImageView post_views[PRESENT_TARGET_MAX] = {};
				const uint32_t post_view_count = static_cast<uint32_t>(targets.size());
				for (uint32_t index = 0; index < post_view_count; ++index)
					post_views[index] = targets[index].color_view;
				//取场景目标的深度图像（第一百二十三轮；帧模块据此做采样前后的布局转换）
				impl->render_target.target_depth_get(impl->present_target, targets[0].depth_image, targets[0].depth_view);
				if (!impl->post_sets_bind(post_views, post_view_count,
					shared_sampler, targets[0].depth_view))
				{
					logger.error("渲染器:: 后处理失败：采样集绑定失败");
					ready = false;
				}
				//各级后处理管线句柄序列（链路径取链；单级路径取默认后处理管线；复用缓冲）
				std::vector<uint64_t>& stage_pipelines = impl->post_stage_pipelines_scratch;
				stage_pipelines.clear();
				//链路径取注册链
				if (post_chain_mode)
					stage_pipelines = impl->post_chain;
				//单级路径取懒登记的默认后处理管线
				else
					stage_pipelines.push_back(impl->post_pipeline);
				//组装各级后处理绘制项（全屏四边形 ＋ 各级管线 ＋ set0 场景 UBO / set1 对应源采样集）
				//后处理绘制项复用缓冲（第一百八十二轮；与 SSAA 路径共用同一成员，两条路径互斥）
				std::vector<Frame_Render_Item>& post_items = impl->post_items_scratch;
				post_items.clear();
				//任一级管线缺失即整帧回落
				bool items_ready = true;
				for (std::size_t index = 0; index < stage_pipelines.size(); ++index)
				{
					//本级图形管线（链设置后被卸载则为空）
					VkPipeline stage_pipeline = impl->pipeline.pipeline_by_handle_get(stage_pipelines[index]);
					//若本级管线已失效
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = impl->pipeline.layout_get();
					post_item.scene_set = impl->pipeline.scene_set_get(slot);
					//本级源采样集（下标 = 级序 % 目标数，与帧模块的目标轮转一致）
					post_item.sampler_set = impl->pipeline.post_set_get(slot,
						static_cast<uint32_t>(index % targets.size()));
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = impl->instance_buffers[slot];
					post_item.instance_offset = post_instance_offset;
					post_item.instance_count = 1;
					//该级管线实际采样数（决定宿附件是否为多重采样；第九十八轮）
					post_item.sample_count = impl->pipeline.pipeline_sample_count(stage_pipelines[index]);
					//泛光参数（帧模块随「源 / 宿尺寸」一并推送，offset 96）
					if (index < POST_CHAIN_MAX_STAGES && impl->post_stage_param_valid[index])
					{
						//该级有专设参数则覆盖全局值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = impl->post_stage_param[index][each];
					}
					else
					{
						//否则用全局泛光参数
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = impl->bloom_param[each];
					}
					//描边色（第一百八十一轮；未专设时为黑 = 压暗轮廓）
					if (index < POST_CHAIN_MAX_STAGES && impl->post_stage_color_valid[index])
					{
						//该级有专设描边色则取专设值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_color[each] = impl->post_stage_color[index][each];
					}
					else
					{
						//否则保持默认黑
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_color[each] = 0.0f;
					}
					//逐级额外四参（第四百零一轮；未专设时为 0，既有阶段不读取 → 画面不变）
					if (index < POST_CHAIN_MAX_STAGES && impl->post_stage_extra_valid[index])
					{
						//该级有专设 extra 则取专设值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_extra[each] = impl->post_stage_extra[index][each];
					}
					else
					{
						//否则保持全零
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_extra[each] = 0.0f;
					}
					post_items.push_back(post_item);
				}
				//若各级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ N 级后处理 pass（目标轮转）＋ UI ＋ 呈现（单级路径即本函数 N = 1 的特例）
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(),
						static_cast<uint32_t>(targets.size()),
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr, &ui_callback))
					{
						logger.error("渲染器:: 同帧后处理链渲染失败");
						return false;
					}
				}
				//含失效管线则回落原路径
				else
					logger.warn("渲染器:: 后处理链含已失效管线，回落原路径");
			}
			//前置不完整则回落原路径
			else
				logger.warn("渲染器:: 同帧多 pass 后处理前置不完整（目标或全屏网格或采样器），回落原路径");
		}
		//若已设定本帧渲染目标（离屏路径）
		if (!offscreen && impl->active_target != 0)
		{
			//取目标的颜色图像 / 颜色视图 / 深度视图与范围
			Frame_Render_Target target{};
			if (impl->render_target.target_get(impl->active_target, target.color_image, target.color_view,
				target.depth_view, target.extent)
				&& impl->render_target.target_depth_get(impl->active_target, target.depth_image, target.depth_view)
				&& impl->render_target.target_msaa_get(impl->active_target, target.msaa_color_image,
					target.msaa_color_view, target.msaa_depth_view, target.sample_count))
			{
				//并行录制格式（第二百轮；离屏目标与交换链同格式）
				target.color_format = impl->swapchain.format_get();
				target.depth_format = impl->swapchain.depth_format_get();
				offscreen = true;
				/*
				场景退场回调（第三百六十八轮；离屏目标亦生成 Hi-Z 深度历史）
				与交换链路径同口径：仅 Hi-Z 开启且资源就绪时挂接；尺寸不一致时回调内部静默跳过。
				*/
				Frame_Post_Scene_Callback post_scene{};
				//启用时挂接 Hi-Z 生成回调
				if (impl->hiz_enabled && impl->hiz.valid())
				{
					post_scene.fn = Renderer_Impl_Holder::Impl::hiz_post_scene_entry;
					post_scene.user_data = impl.get();
				}
				//渲染到离屏目标（不取交换链图像、不呈现）
				if (!impl->frame.render_to_target(target, impl->pending_items.data(),
					static_cast<uint32_t>(impl->pending_items.size()), clear_color,
					&ui_callback, readback_ptr, false, &post_scene))
				{
					logger.error("渲染器:: 离屏帧渲染失败");
					return false;
				}
			}
			//句柄无效则回落交换链路径
			else
				logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", impl->active_target);
		}
		//若走交换链路径
		if (!offscreen)
		{
			/*
			场景退场回调（第二百五十四轮；仅 Hi-Z 开启时挂接）
			MSAA 多重采样深度暂不支持（帧模块内按采样数跳过），260 轮再补。
			*/
			Frame_Post_Scene_Callback post_scene{};
			//启用时挂接 Hi-Z 生成回调
			if (impl->hiz_enabled && impl->hiz.valid())
			{
				post_scene.fn = Renderer_Impl_Holder::Impl::hiz_post_scene_entry;
				post_scene.user_data = impl.get();
			}
			if (!impl->frame.render(impl->swapchain, impl->pending_items.data(),
				static_cast<uint32_t>(impl->pending_items.size()), rebuild_needed, clear_color,
				&ui_callback, readback_ptr, &post_scene))
			{
				logger.error("渲染器:: 帧渲染失败");
				return false;
			}
		}
		//消费完毕，清空待绘制项与其推送常量存储（下一帧由 mesh_draw 重新登记）
		/*
		记录最近一帧的即时观测（第一百三十二轮）
		取用时机在合批之后、清空之前：故 last_draw_items 为「实际交给录制的项数」，
		last_instances 为其实例数之和，与累计口径同源。
		*/
		impl->last_draw_items = impl->pending_items.size();
		uint64_t last_instances = 0;
		for (const Frame_Render_Item& item : impl->pending_items)
			last_instances += item.instance_count;
		impl->last_instances = last_instances;
		//合批收益（第一百三十五轮）：累计 + 本帧转存并清零
		impl->merged_items += impl->frame_merged_items;
		impl->last_merged_items = impl->frame_merged_items;
		impl->frame_merged_items = 0;
		impl->last_culled_items = impl->frame_culled_items;
		impl->frame_culled_items = 0;
		//实例级剔除同样按帧结算（第一百五十三轮）
		impl->last_culled_instances = impl->frame_culled_instances;
		impl->frame_culled_instances = 0;
		//剔除耗时按帧结算（第二百四十七轮）
		impl->last_cull_ms = impl->frame_cull_ms;
		impl->cull_total_ms += impl->frame_cull_ms;
		impl->frame_cull_ms = 0.0;
		impl->pending_items.clear();
		impl->pending_instances.clear();
		impl->pending_push_constants.clear();
		/*
		回读结果交付
		若本帧请求回读但帧模块未产出像素（如窗口最小化 / 关闭请求而跳过渲染，
		或交换链过期提前返回），视为本次回读未兑现，报错返回假并保持尺寸出参为零。
		*/
		if (readback_ptr != nullptr)
		{
			//若帧模块未回读任何像素
			if (readback_request.bytes == 0)
			{
				logger.error("渲染器:: 本帧未产出像素，像素回读未兑现");
				return false;
			}
			//回填回读尺寸
			if (readback_width != nullptr)
				*readback_width = readback_request.width;
			if (readback_height != nullptr)
				*readback_height = readback_request.height;
		}
		//若帧链路要求重建且本帧尚未重建
		if (rebuild_needed && !rebuilt)
		{
			//尺寸有效时重建交换链
			if (width != 0 && height != 0)
			{
				//重建交换链并把旧资源压入帧延迟队列（失败仅告警，下一帧再试）
				if (!impl->swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（下一帧再试）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(), impl->swapchain.depth_format_get(), impl->msaa_samples))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
			}
		}

		//⑥ 帧计数自增
		++impl->frame_count;
		return true;
	}

}
