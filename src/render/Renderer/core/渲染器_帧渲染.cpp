#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//帧内部辅助类型与函数
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
	bool Render_Device::frame_render(uint8_t* readback_pixels, uint32_t readback_capacity,
		uint32_t* readback_width, uint32_t* readback_height)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		//回读尺寸出参先复位（未兑现时保持零，便于调用方判据）
		if (readback_width != nullptr)
			*readback_width = 0;
		if (readback_height != nullptr)
			*readback_height = 0;

		//泵事件 + 本帧输入锁存与原生 UI 交互
		frame_stage_input_events(owner, *state_ptr);
		//UI 阶段统一回调（原生 UI 先绘制，外部 UI 后绘制）
		Frame_Ui_Callback ui_callback{};
		ui_callback.fn = &ui_draw_entry_adapter;
		ui_callback.user_data = &(*owner).ui();
		/*
		② 若窗口请求退出则交调用方收敛
		③ 若窗口最小化则挂起本帧（避免 0×0 交换链；K7）
		两种挂起情形都不消费待绘制项，故一并清空，避免连续挂起期间无限累积。
		*/
		if (state_ptr->platform.close_requested() || state_ptr->platform.minimized())
		{
			state_ptr->pending_items.clear();
			state_ptr->pending_instances.clear();
			return true;
		}

		//取当前窗口客户区尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		state_ptr->platform.window_size_get(width, height);

		//本帧是否已重建交换链（K8：每帧最多一次）
		bool rebuilt = false;
		//④ 若窗口尺寸已变化则重建交换链
		if (state_ptr->platform.resized_consume())
		{
			//尺寸为零则不重建
			if (width != 0 && height != 0)
			{
				//重建交换链并把旧资源压入帧延迟队列；失败仅告警，本帧继续
				if (!state_ptr->swapchain_rebuild_deferred(width, height))
					logger.warn("渲染器:: 交换链重建失败（本帧继续）");
				//格式若因重建而改变则重建图形管线（格式未变时为空操作）
				if (!owner->resource().pipeline_rebuild(static_cast<uint32_t>(state_ptr->scene_color_format()),
					state_ptr->swapchain.depth_format_get(), state_ptr->msaa_samples))
					logger.warn("渲染器:: 图形管线随交换链格式重建失败");
				rebuilt = true;
			}
		}

		//交换链尺寸可能已变化，刷新相机宽高比
		{
			VkExtent2D extent = state_ptr->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				owner->scene().camera_aspect_refresh(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		//光源空间矩阵与级联拟合
		frame_stage_shadow_matrices(owner, *state_ptr);
		//聚光灯阴影矩阵与本帧有效标志
		const bool spot_shadow_light_valid = frame_stage_spot_shadow_matrices(owner, *state_ptr);
		//阴影 pass（输入签名不变时复用上一帧阴影图）
		if (!frame_stage_shadow_pass(owner, *state_ptr, spot_shadow_light_valid))
			return false;

		/*
		⑤ 取本帧帧槽序号（与帧模块内部一致），把相机视图 × 投影写入该槽 UBO 并绑定 set0；
		再为待绘制项填装 set0 / set1 描述符集（descriptor_set_count = 2）。
		推送常量恒为列主序 16 浮点模型矩阵（mesh_draw 已写入对应的 pending_transforms 存储）。
		*/
		uint32_t slot = state_ptr->frame.slot_index_get();
		/*
		等待本帧槽前一次提交完成
		必须在写本帧槽 UBO 之前等待，否则可能覆盖 GPU 仍在读取的上一轮 UBO（真实竞态）；
		帧模块内部还会再等一次，届时栅栏已发出，vkWaitForFences 立即返回，无副作用。
		*/
		if (!state_ptr->frame.slot_wait())
		{
			logger.error("渲染器:: 等待本帧帧槽失败");
			return false;
		}
		//计算域完成 Cluster 光源记录与簇数据上传；光源记录失败为帧硬失败
		if (!owner->compute().cluster_frame_update(slot, state_ptr->frame_count))
			return false;
		float view_proj[16] = {};
		owner->scene().camera_view_proj(view_proj);
		//推进文字子系统帧时钟并归还已淘汰且无在途引用的网格（第四十八轮）
		owner->ui().text_frame_advance(state_ptr->config.frame_overlap);
		//场景 UBO 组装与上传
		if (!frame_stage_scene_ubo(owner, *state_ptr, slot, view_proj, spot_shadow_light_valid))
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
		/*
		HDR 默认路径（第 757 轮）
		hdr_targets 开启且本帧未显式指定呈现 / 渲染目标时，内部以 16F 场景目标接管场景渲染，
		再由内置色调映射级呈现到交换链；目标建立失败或用户显式指定目标时保持原路径。
		*/
		state_ptr->effective_present_target = state_ptr->present_target;
		if (state_ptr->hdr_targets && state_ptr->present_target == 0 && state_ptr->active_target == 0)
		{
			//按当前客户区尺寸与 MSAA 采样数确保 16F 场景目标（第 766 轮起支持多重采样）
			if (state_ptr->hdr_scene_ensure(width, height, state_ptr->msaa_samples))
			{
				state_ptr->effective_present_target = state_ptr->hdr_scene_target;
				//自动泛光：按需填充四级链与三个中间目标（未开启 / 用户已有配置时不介入）
				state_ptr->hdr_bloom_frame_apply();
				/*
				双轨分轨参数下发（第 774 轮）
				算子 5 的分轨位置 / 过渡带经全局泛光参数槽传入末级片元（offset 96）；
				默认值即 773 的固常量（0.5 / 0.1），关闭该算子时此槽仍作泛光默认参数使用。
				*/
				if (state_ptr->hdr_tone_operator == 5)
					owner->device().bloom_param_set(state_ptr->hdr_dual_tone_split,
						state_ptr->hdr_dual_tone_band, 1.0f);
			}
		}
		bool post_frame = false;
		//本帧是否走后处理链路径（false 且 post_frame → 既有单级默认路径）
		bool post_chain_mode = false;
		//后处理绘制项在本帧实例缓冲内的字节偏移
		VkDeviceSize post_instance_offset = 0;
		//若已设定同帧后处理呈现目标（含 HDR 默认路径派生的 16F 场景目标）
		if (state_ptr->effective_present_target != 0)
		{
			//本帧实例存储须仍有空位（后处理全屏四边形再占一个实例；满则回落原路径，避免实例缓冲越界写）
			if (state_ptr->pending_instances.size() >= state_ptr->instance_capacity)
				logger.warn("渲染器:: 本帧实例存储已满，同帧后处理呈现回落原路径");
			else
			{
				//本帧是否走链路径
				bool chain_mode = !state_ptr->post_chain.empty();
				/*
				链级数 ≥ 2 但中间目标不足（第 770 轮修正）
				此前此处只打日志、整段跳过后处理，画面会直通交换链（HDR 默认路径下等于丢掉色调映射）；
				现按日志口径真正回落「单级默认后处理」：本帧改走内置默认后处理管线，用户链本身不被改写。
				*/
				if (chain_mode && state_ptr->post_chain.size() >= 2 && state_ptr->present_extra.empty())
				{
					logger.warn("渲染器:: 后处理链级数 ≥ 2 但中间目标不足 2 个，本帧回落单级默认后处理");
					chain_mode = false;
				}
				//确保后处理资源就绪（链 → 全屏网格；单级 → 全屏网格 ＋ 默认后处理管线）
				bool resources_ready = chain_mode ? state_ptr->post_mesh_ensure() : state_ptr->post_resources_ensure();
				//若资源就绪
				if (resources_ready)
				{
					//追加单位矩阵（列主序；材质四浮点为零 = 不覆盖全局 PBR）并记录其字节偏移
					post_instance_offset = static_cast<VkDeviceSize>(state_ptr->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					state_ptr->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = state_ptr->pending_instances.back();
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
		//若启用 SSAA 且未走后处理 / 离屏 / HDR 默认路径
		if (state_ptr->ssaa_factor > 1 && state_ptr->effective_present_target == 0 && state_ptr->active_target == 0)
		{
			//确保降采样管线 / 全屏网格 / 默认后处理管线 / 超采样目标就绪
			if (state_ptr->ssaa_pipeline_ensure() && state_ptr->post_mesh_ensure() && state_ptr->post_pipeline_ensure()
				&& state_ptr->ssaa_targets_ensure(width, height, state_ptr->ssaa_factor))
			{
				//本帧实例存储须仍有空位（降采样全屏四边形再占一个实例）
				if (state_ptr->pending_instances.size() < state_ptr->instance_capacity)
				{
					ssaa_instance_offset = static_cast<VkDeviceSize>(state_ptr->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					state_ptr->pending_instances.emplace_back();
					std::array<float, INSTANCE_FLOAT_COUNT>& identity = state_ptr->pending_instances.back();
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
		if (owner->resource().sky_enabled())
		{
			//本帧实例存储须仍有空位，且全屏网格与天空管线就绪
			if (state_ptr->pending_instances.size() < state_ptr->instance_capacity
				&& state_ptr->post_mesh_ensure() && state_ptr->sky_pipeline_ensure())
			{
				sky_instance_offset = static_cast<VkDeviceSize>(state_ptr->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
				state_ptr->pending_instances.emplace_back();
				std::array<float, INSTANCE_FLOAT_COUNT>& identity = state_ptr->pending_instances.back();
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
		if (!frame_stage_instance_buffers(owner, *state_ptr, slot))
			return false;

		/*
		静态合批（第一百二十七轮；第一百二十九轮抽为主 pass / 阴影 pass 共用工具）
		仅合并相邻等价项，绘制顺序与实例顺序不变，画面逐字节一致。
		*/
		if (state_ptr->static_batching)
			state_ptr->frame_merged_items += batch_adjacent_items(state_ptr->pending_items, state_ptr->batch_scratch);

		frame_stage_descriptor_sets(owner, *state_ptr, slot);
		//追加天空盒绘制项
		frame_stage_sky_item(owner, *state_ptr, slot, view_proj, sky_instance_offset, sky_frame);

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
		if (!frame_stage_dispatch(owner, *state_ptr, dispatch))
			return false;

		//⑥ 帧计数自增
		++state_ptr->frame_count;
		return true;
	}

}
