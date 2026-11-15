#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//帧内部辅助类型与函数
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	//读取全屏网格原生缓冲并还原内部句柄类型
	static bool mesh_frame_native_get(Render_Resource& resource, Mesh_Handle handle,
		VkBuffer& buffer, VkDeviceSize& index_offset, uint32_t& index_count)
	{
		uint64_t buffer_native = 0;
		uint64_t offset_native = 0;
		if (!resource.mesh_native_get(handle, buffer_native, offset_native, index_count))
			return false;
		buffer = reinterpret_cast<VkBuffer>(buffer_native);
		index_offset = offset_native;
		return true;
	}

	//帧阶段：分派渲染路径（SSAA / 多 pass 后处理 / 离屏 / 交换链）并交付回读
	bool Render_Device::frame_stage_dispatch(Renderer* owner, Device_State& state, const Frame_Dispatch_Input& input)
	{
		//入参解包（保持原主链写法不变）
		const uint32_t width = input.width;
		const uint32_t height = input.height;
		const uint32_t slot = input.slot;
		const bool rebuilt = input.rebuilt;
		const bool post_frame = input.post_frame;
		const bool post_chain_mode = input.post_chain_mode;
		const bool ssaa_frame = input.ssaa_frame;
		const VkDeviceSize post_instance_offset = input.post_instance_offset;
		const VkDeviceSize ssaa_instance_offset = input.ssaa_instance_offset;
		const float* view_proj = input.view_proj;
		const Frame_Ui_Callback& ui_callback = input.ui_callback;
		uint8_t* const readback_pixels = input.readback_pixels;
		const uint32_t readback_capacity = input.readback_capacity;
		uint32_t* const readback_width = input.readback_width;
		uint32_t* const readback_height = input.readback_height;
		//交给帧模块走取图像 / 录制 / 提交 / 呈现（可选像素回读）
		float clear_color[4] = {};
		owner->scene().clear_color_get(clear_color);
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
			std::vector<Frame_Render_Target>& targets = state.chain_targets_scratch;
			targets.assign(2, Frame_Render_Target{});
			//全屏四边形网格缓冲信息
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//取两个目标的附件与全屏网格
			bool ready = owner->resource().render_target_frame_full_get(state.ssaa_scene_target, targets[0])
				&& owner->resource().render_target_frame_full_get(state.ssaa_resolve_target, targets[1])
				&& mesh_frame_native_get(owner->resource(),state.fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//并行录制格式（第二百轮；SSAA 目标与交换链同格式）
			targets[0].color_format = state.swapchain.format_get();
			targets[0].depth_format = state.swapchain.depth_format_get();
			//取共享采样器（用于写后处理采样集）
			uint64_t default_view = 0;
			uint64_t shared_sampler = 0;
			if (ready && !owner->resource().default_texture_native_get(default_view, shared_sampler))
			{
				logger.error("渲染器:: SSAA 失败：共享采样器不可用");
				ready = false;
			}
			//若前置就绪
			if (ready)
			{
				//把两个目标写入各帧槽后处理采样集（签名未变则跳过，避免更新在途描述符集）
				VkImageView post_views[2] = { targets[0].color_view, targets[1].color_view };
				if (!state.post_sets_bind(post_views, 2, reinterpret_cast<VkSampler>(shared_sampler)))
				{
					logger.error("渲染器:: SSAA 失败：后处理采样集绑定失败");
					ready = false;
				}
				//两级管线：0 = 降采样（N 倍 → 输出），1 = 默认后处理（输出 → 交换链）
				uint64_t stage_pipelines[2] = { state.ssaa_pipeline, state.post_pipeline };
				//后处理绘制项复用缓冲（第一百八十二轮）
				std::vector<Frame_Render_Item>& post_items = state.post_items_scratch;
				post_items.clear();
				bool items_ready = true;
				for (uint32_t index = 0; index < 2; ++index)
				{
					//本级图形管线（失效即整帧回落）
					VkPipeline stage_pipeline = reinterpret_cast<VkPipeline>(owner->resource().pipeline_native_get(stage_pipelines[index]));
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项（全屏四边形 ＋ 本级管线 ＋ set0 场景 UBO / set1 对应源采样集）
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = reinterpret_cast<VkPipelineLayout>(owner->resource().pipeline_layout_native());
					post_item.scene_set = reinterpret_cast<VkDescriptorSet>(owner->resource().pipeline_scene_set_native(slot));
					post_item.sampler_set = reinterpret_cast<VkDescriptorSet>(owner->resource().pipeline_post_set_native(slot, index));
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = state.instance_buffers[slot];
					post_item.instance_offset = ssaa_instance_offset;
					post_item.instance_count = 1;
					//该级管线实际采样数（决定宿附件是否为多重采样）
					post_item.sample_count = owner->resource().pipeline_sample_count(stage_pipelines[index]);
					post_items.push_back(post_item);
				}
				//若两级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ 降采样 ＋ 默认后处理 ＋ UI ＋ 呈现
					if (!state.frame.render_chain_present(state.swapchain, targets.data(), 2,
						state.pending_items.data(), static_cast<uint32_t>(state.pending_items.size()),
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
			std::vector<Frame_Render_Target>& targets = state.chain_targets_scratch;
			//按目标组数量重置元素（容量保留，避免逐帧堆分配；第一百九十四轮）
			targets.assign(1 + state.present_extra.size(), Frame_Render_Target{});
			//全屏四边形网格的共用缓冲 / 索引区偏移 / 索引数
			VkBuffer mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize mesh_index_offset = 0;
			uint32_t mesh_index_count = 0;
			//场景目标与全屏网格均就绪才可走该路径
			bool ready = owner->resource().render_target_frame_full_get(state.present_target, targets[0])
				&& mesh_frame_native_get(owner->resource(),state.fullscreen_mesh, mesh_buffer, mesh_index_offset, mesh_index_count);
			//并行录制格式（第二百轮；场景目标与交换链同格式）
			targets[0].color_format = state.swapchain.format_get();
			targets[0].depth_format = state.swapchain.depth_format_get();
			/*
			MSAA 深度解析目标（第一百九十轮）
			开启深度解析时取场景目标的多重采样深度图像，帧模块据此在场景 pass 结束时
			把深度解析到 targets[0].depth_image（后处理阶段采样那张）；
			关闭或目标无多重采样深度时留空（帧模块跳过解析，保持既有行为）。
			*/
			if (ready && state.msaa_depth_resolve)
				targets[0].msaa_depth_image = reinterpret_cast<VkImage>(owner->resource().render_target_msaa_depth_image_native(state.present_target));
			//逐个取额外中间目标附件
			for (std::size_t each = 1; ready && each < targets.size(); ++each)
				ready = owner->resource().render_target_frame_full_get(state.present_extra[each - 1], targets[each]);
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
			uint64_t default_view = 0;
			uint64_t shared_sampler = 0;
			if (ready && !owner->resource().default_texture_native_get(default_view, shared_sampler))
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
				if (!state.post_sets_bind(post_views, post_view_count,
					reinterpret_cast<VkSampler>(shared_sampler), targets[0].depth_view))
				{
					logger.error("渲染器:: 后处理失败：采样集绑定失败");
					ready = false;
				}
				//各级后处理管线句柄序列（链路径取链；单级路径取默认后处理管线；复用缓冲）
				std::vector<uint64_t>& stage_pipelines = state.post_stage_pipelines_scratch;
				stage_pipelines.clear();
				//链路径取注册链
				if (post_chain_mode)
					stage_pipelines = state.post_chain;
				//单级路径取懒登记的默认后处理管线
				else
					stage_pipelines.push_back(state.post_pipeline);
				//组装各级后处理绘制项（全屏四边形 ＋ 各级管线 ＋ set0 场景 UBO / set1 对应源采样集）
				//后处理绘制项复用缓冲（第一百八十二轮；与 SSAA 路径共用同一成员，两条路径互斥）
				std::vector<Frame_Render_Item>& post_items = state.post_items_scratch;
				post_items.clear();
				//任一级管线缺失即整帧回落
				bool items_ready = true;
				for (std::size_t index = 0; index < stage_pipelines.size(); ++index)
				{
					//本级图形管线（链设置后被卸载则为空）
					VkPipeline stage_pipeline = reinterpret_cast<VkPipeline>(owner->resource().pipeline_native_get(stage_pipelines[index]));
					//若本级管线已失效
					if (stage_pipeline == VK_NULL_HANDLE)
					{
						items_ready = false;
						break;
					}
					//本级绘制项
					Frame_Render_Item post_item{};
					post_item.pipeline = stage_pipeline;
					post_item.layout = reinterpret_cast<VkPipelineLayout>(owner->resource().pipeline_layout_native());
					post_item.scene_set = reinterpret_cast<VkDescriptorSet>(owner->resource().pipeline_scene_set_native(slot));
					//本级源采样集（下标 = 级序 % 目标数，与帧模块的目标轮转一致）
					post_item.sampler_set = reinterpret_cast<VkDescriptorSet>(owner->resource().pipeline_post_set_native(slot,
						static_cast<uint32_t>(index % targets.size())));
					post_item.descriptor_set_count = 2;
					post_item.vertex_buffer = mesh_buffer;
					post_item.index_buffer = mesh_buffer;
					post_item.index_offset = mesh_index_offset;
					post_item.index_count = mesh_index_count;
					post_item.instance_buffer = state.instance_buffers[slot];
					post_item.instance_offset = post_instance_offset;
					post_item.instance_count = 1;
					//该级管线实际采样数（决定宿附件是否为多重采样；第九十八轮）
					post_item.sample_count = owner->resource().pipeline_sample_count(stage_pipelines[index]);
					//泛光参数（帧模块随「源 / 宿尺寸」一并推送，offset 96）
					if (index < POST_CHAIN_MAX_STAGES && state.post_stage_param_valid[index])
					{
						//该级有专设参数则覆盖全局值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = state.post_stage_param[index][each];
					}
					else
					{
						//否则用全局泛光参数
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_params[each] = state.bloom_param[each];
					}
					//描边色（第一百八十一轮；未专设时为黑 = 压暗轮廓）
					if (index < POST_CHAIN_MAX_STAGES && state.post_stage_color_valid[index])
					{
						//该级有专设描边色则取专设值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_color[each] = state.post_stage_color[index][each];
					}
					else
					{
						//否则保持默认黑
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_color[each] = 0.0f;
					}
					//逐级额外四参（第四百零一轮；未专设时为 0，既有阶段不读取 → 画面不变）
					if (index < POST_CHAIN_MAX_STAGES && state.post_stage_extra_valid[index])
					{
						//该级有专设 extra 则取专设值
						for (std::size_t each = 0; each < 4; ++each)
							post_item.post_extra[each] = state.post_stage_extra[index][each];
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
					if (!state.frame.render_chain_present(state.swapchain, targets.data(),
						static_cast<uint32_t>(targets.size()),
						state.pending_items.data(), static_cast<uint32_t>(state.pending_items.size()),
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
		if (!offscreen && state.active_target != 0)
		{
			//取目标的颜色图像 / 颜色视图 / 深度视图与范围
			Frame_Render_Target target{};
			if (owner->resource().render_target_frame_full_get(state.active_target, target))
			{
				//并行录制格式（第二百轮；离屏目标与交换链同格式）
				target.color_format = state.swapchain.format_get();
				target.depth_format = state.swapchain.depth_format_get();
				offscreen = true;
				/*
				场景退场回调（第三百六十八轮；离屏目标亦生成 Hi-Z 深度历史）
				与交换链路径同口径：仅 Hi-Z 开启且资源就绪时挂接；尺寸不一致时回调内部静默跳过。
				*/
				Frame_Post_Scene_Callback post_scene{};
				//启用时挂接 Hi-Z 生成回调
				if (owner->compute().hiz_frame_ready())
				{
					post_scene.fn = Device_State::hiz_post_scene_entry;
					post_scene.user_data = &state;
				}
				//渲染到离屏目标（不取交换链图像、不呈现）
				if (!state.frame.render_to_target(target, state.pending_items.data(),
					static_cast<uint32_t>(state.pending_items.size()), clear_color,
					&ui_callback, readback_ptr, false, &post_scene))
				{
					logger.error("渲染器:: 离屏帧渲染失败");
					return false;
				}
			}
			//句柄无效则回落交换链路径
			else
				logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", state.active_target);
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
			if (owner->compute().hiz_frame_ready())
			{
				post_scene.fn = Device_State::hiz_post_scene_entry;
				post_scene.user_data = &state;
			}
			if (!state.frame.render(state.swapchain, state.pending_items.data(),
				static_cast<uint32_t>(state.pending_items.size()), rebuild_needed, clear_color,
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
		state.last_draw_items = state.pending_items.size();
		uint64_t last_instances = 0;
		for (const Frame_Render_Item& item : state.pending_items)
			last_instances += item.instance_count;
		state.last_instances = last_instances;
		//合批收益（第一百三十五轮）：累计 + 本帧转存并清零
		state.merged_items += state.frame_merged_items;
		state.last_merged_items = state.frame_merged_items;
		state.frame_merged_items = 0;
		state.last_culled_items = state.frame_culled_items;
		state.frame_culled_items = 0;
		//实例级剔除同样按帧结算（第一百五十三轮）
		state.last_culled_instances = state.frame_culled_instances;
		state.frame_culled_instances = 0;
		//剔除耗时按帧结算（第二百四十七轮）
		state.last_cull_ms = state.frame_cull_ms;
		state.cull_total_ms += state.frame_cull_ms;
		state.frame_cull_ms = 0.0;
		state.pending_items.clear();
		state.pending_instances.clear();
		state.pending_push_constants.clear();
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
				if (!state.swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（下一帧再试）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!owner->resource().pipeline_rebuild(state.swapchain.format_get(), state.swapchain.depth_format_get(), state.msaa_samples))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
			}
		}
		return true;
	}
}
