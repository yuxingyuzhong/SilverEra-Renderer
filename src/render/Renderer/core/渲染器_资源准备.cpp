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

namespace engine
{
	//重建交换链并把旧资源压入帧延迟销毁队列
	bool Device_State::swapchain_rebuild_deferred(uint32_t width, uint32_t height)
	{
		//旧资源（重建成功后移交延迟队列）
		Swapchain_Old_Resources old_resources{};
		//若重建失败则不移交任何资源（交换链已回滚为旧链，仍可用）
		if (!swapchain.rebuild(width, height, old_resources))
			return false;
		//组装延迟条目并压入帧模块（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放旧资源）
		Deferred_Entry entry{};
		entry.resources = std::move(old_resources);
		frame.deferred_push(std::move(entry), frame.in_flight_count_get() + 1);
		/*
		Hi-Z 资源跟随交换链尺寸（第二百五十三轮；计算域承接状态）
		已构建时按新尺寸重建；失败则整体销毁并降级为关闭（不阻断重建后的出帧）。
		*/
		//尺寸变化后由计算域重建历史与金字塔，失败时销毁并降级
		if (owner != nullptr)
		{
			VkExtent2D extent = swapchain.extent_get();
			owner->compute().hiz_swapchain_rebuild(extent.width, extent.height,
				static_cast<uint32_t>(swapchain.depth_format_get()));
		}
		return true;
	}

	/*
	确保 Hi-Z 资源就绪（第二百五十三轮）
	能力不足（计算不可用 / R32 浮点存储图像不可用）时返回假；尺寸与当前交换链一致时为空操作。
	*/
	bool Device_State::hiz_ensure(void)
	{
		if (owner == nullptr)
			return false;
		VkExtent2D extent = swapchain.extent_get();
		return owner->compute().hiz_resources_ensure(extent.width, extent.height,
			static_cast<uint32_t>(swapchain.depth_format_get()));
	}

	/*
	确保光源记录 SSBO 就绪（第二百六十六轮）
	计算路径可用时按帧槽各建一个主机可见存储缓冲；已就绪时为空操作，构建失败回滚为无资源。
	*/
	bool Device_State::cluster_ensure(void)
	{
		return owner != nullptr && owner->compute().cluster_resources_ensure();
	}

	//销毁光源记录 SSBO（第二百六十六轮；幂等）
	void Device_State::cluster_destroy(void)
	{
		if (owner != nullptr)
			owner->compute().cluster_resources_destroy();
	}

	//销毁图形 set0 的簇数据缓冲与占位缓冲（第二百八十八轮；幂等）
	void Device_State::cluster_graphics_destroy(void)
	{
		if (owner != nullptr)
			owner->compute().cluster_graphics_resources_destroy();
	}

	/*
	确保簇数据占位缓冲就绪（第二百八十八轮）
	64 字节主机可见存储缓冲；Cluster 关闭时 set0 binding 1..4 统一指向它。
	*/
	bool Device_State::cluster_placeholder_ensure(void)
	{
		return owner != nullptr && owner->compute().cluster_placeholder_resource_ensure();
	}

	/*
	确保图形 set0 的簇数据缓冲就绪（第二百八十八轮）
	容量足够且逐槽句柄齐备时为空操作；否则等尽在途帧后按新容量重建，并重写全部帧槽的 binding 1..4。
	*/
	bool Device_State::cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity)
	{
		return owner != nullptr && owner->compute().cluster_graphics_resources_ensure(cluster_total, list_capacity);
	}


	void Render_Compute::hiz_swapchain_rebuild(uint32_t width, uint32_t height, uint32_t depth_format)
	{
		if (!(*state_ptr).hiz.valid())
			return;
		if (!(*state_ptr).hiz.extent_set(width, height, static_cast<VkFormat>(depth_format)))
		{
			logger.warn("渲染器:: Hi-Z 资源尺寸重建失败（已降级为关闭）");
			(*state_ptr).hiz.destroy();
			(*state_ptr).hiz_enabled = false;
		}
	}

	bool Render_Compute::hiz_resources_ensure(uint32_t width, uint32_t height, uint32_t depth_format)
	{
		//计算域尚未登记（半初始化）
		if (owner == nullptr)
			return false;
		//计算域 Hi-Z 与计算上下文
		auto& hiz = (*state_ptr).hiz;
		auto& compute = (*state_ptr).compute;
		//能力不足
		if (!(*state_ptr).hiz_capable || !compute.supported())
			return false;
		//关联计算上下文（第二百五十四轮：生成经其描述符集与调度）
		hiz.compute_set(&compute);
		//当前交换链范围（为 0 表示尚未就绪）
		VkExtent2D extent{ width, height };
		if (extent.width == 0 || extent.height == 0)
			return false;
		//已就绪且尺寸一致
		if (hiz.valid() && hiz.width_get() == extent.width
			&& hiz.height_get() == extent.height
			&& hiz.depth_format_get() == static_cast<VkFormat>(depth_format))
			return true;
		//已构建但尺寸 / 格式变化：按新参数重建
		if (hiz.valid())
			return hiz.extent_set(extent.width, extent.height, static_cast<VkFormat>(depth_format));
		//首次构建（每帧槽一份历史 ＋ 一份金字塔）
		return hiz.build(core->device.device_get(), core->memory, core->config.frame_overlap, extent.width, extent.height,
			static_cast<VkFormat>(depth_format));
	}

	bool Render_Compute::cluster_resources_ensure(void)
	{
		//计算域尚未登记（半初始化）
		if (owner == nullptr)
			return false;
		//能力不足
		if (!(*state_ptr).cluster_capable
			|| !(*state_ptr).compute.supported())
			return false;
		//已就绪（逐槽缓冲均已建立）
		if ((*state_ptr).light_buffers.size() == core->config.frame_overlap)
		{
			//逐槽检查是否有空句柄
			bool ready = true;
			for (std::size_t slot = 0; slot < (*state_ptr).light_buffers.size(); ++slot)
				if ((*state_ptr).light_buffers[slot] == VK_NULL_HANDLE)
					ready = false;
			if (ready)
				return true;
		}
		//清理半初始化或旧资源
		cluster_resources_destroy();
		(*state_ptr).light_buffers.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).light_allocations.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		for (uint32_t slot = 0; slot < core->config.frame_overlap; ++slot)
		{
			//该帧槽光源记录缓冲（容量 × 80 字节；计算着色器只读）
			if (!core->memory.buffer_create((*state_ptr).light_buffers[slot], (*state_ptr).light_allocations[slot],
				static_cast<VkDeviceSize>(LIGHT_RECORD_CAPACITY) * LIGHT_RECORD_BYTES,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
			{
				logger.warn("渲染器:: 光源记录缓冲创建失败（帧槽 {}）", slot);
				cluster_resources_destroy();
				return false;
			}
		}
		return true;
	}

	void Render_Compute::cluster_resources_destroy(void)
	{
		//依赖内存模块，须先于 memory 销毁
		if (core->memory.valid())
			for (std::size_t slot = 0; slot < (*state_ptr).light_buffers.size(); ++slot)
				//归还有效缓冲与其内存
				if ((*state_ptr).light_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).light_buffers[slot], (*state_ptr).light_allocations[slot]);
		(*state_ptr).light_buffers.clear();
		(*state_ptr).light_allocations.clear();
		//最近一帧上传记录数（计算域已登记时复位）
		if (owner != nullptr)
			(*state_ptr).last_cluster_light_count = 0;
	}

	void Render_Compute::cluster_graphics_resources_destroy(void)
	{
		//依赖内存模块，须先于 memory 销毁
		if (core->memory.valid())
		{
			//逐槽归还计数 / 偏移 / 列表缓冲
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_counts_buffers.size(); ++slot)
				if ((*state_ptr).cluster_counts_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_counts_buffers[slot], (*state_ptr).cluster_counts_allocations[slot]);
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_offsets_buffers.size(); ++slot)
				if ((*state_ptr).cluster_offsets_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_offsets_buffers[slot], (*state_ptr).cluster_offsets_allocations[slot]);
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_list_buffers.size(); ++slot)
				if ((*state_ptr).cluster_list_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_list_buffers[slot], (*state_ptr).cluster_list_allocations[slot]);
			//归还占位缓冲
			if ((*state_ptr).cluster_placeholder_buffer != VK_NULL_HANDLE)
				core->memory.buffer_destroy((*state_ptr).cluster_placeholder_buffer, (*state_ptr).cluster_placeholder_allocation);
		}
		(*state_ptr).cluster_counts_buffers.clear();
		(*state_ptr).cluster_counts_allocations.clear();
		(*state_ptr).cluster_offsets_buffers.clear();
		(*state_ptr).cluster_offsets_allocations.clear();
		(*state_ptr).cluster_list_buffers.clear();
		(*state_ptr).cluster_list_allocations.clear();
		(*state_ptr).cluster_placeholder_buffer = VK_NULL_HANDLE;
		(*state_ptr).cluster_placeholder_allocation = VK_NULL_HANDLE;
		(*state_ptr).cluster_counts_capacity = 0;
		(*state_ptr).cluster_list_capacity = 0;
		(*state_ptr).last_cluster_gpu_bytes = 0;
	}

	bool Render_Compute::cluster_placeholder_resource_ensure(void)
	{
		//已就绪
		if ((*state_ptr).cluster_placeholder_buffer != VK_NULL_HANDLE)
			return true;
		//内存模块缺失
		if (!core->memory.valid())
			return false;
		//创建占位缓冲（不足一页，任意长度均满足描述符完整性要求）
		return core->memory.buffer_create((*state_ptr).cluster_placeholder_buffer, (*state_ptr).cluster_placeholder_allocation,
			64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
	}

	uint64_t Render_Compute::cluster_placeholder_native(void) const
	{
		return reinterpret_cast<uint64_t>((*state_ptr).cluster_placeholder_buffer);
	}

	void Render_Compute::options_apply(bool gpu_cull_enabled, const uint32_t cluster_grid[3])
	{
		(*state_ptr).gpu_cull_enabled = gpu_cull_enabled;
		(*state_ptr).cluster_capable = (*state_ptr).compute.supported();
		(*state_ptr).cluster_grid = cluster_grid_clamp(Cluster_Grid{ cluster_grid[0], cluster_grid[1], cluster_grid[2] });
	}

	bool Render_Compute::cluster_graphics_resources_ensure(uint32_t cluster_total, uint32_t list_capacity)
	{
		//计算域尚未登记（半初始化）
		if (owner == nullptr)
			return false;
		//能力不足或入参非法
		if (!(*state_ptr).cluster_capable
			|| !(*state_ptr).compute.supported()
			|| cluster_total == 0 || list_capacity == 0)
			return false;
		//已就绪：槽数与容量足够且逐槽句柄齐备
		bool ready = (*state_ptr).cluster_counts_buffers.size() == core->config.frame_overlap
			&& (*state_ptr).cluster_counts_capacity >= cluster_total && (*state_ptr).cluster_list_capacity >= list_capacity;
		if (ready)
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_counts_buffers.size(); ++slot)
				if ((*state_ptr).cluster_counts_buffers[slot] == VK_NULL_HANDLE
					|| (*state_ptr).cluster_offsets_buffers[slot] == VK_NULL_HANDLE
					|| (*state_ptr).cluster_list_buffers[slot] == VK_NULL_HANDLE)
					ready = false;
		//已就绪则无需重建
		if (ready)
			return true;
		//重建前等尽在途帧（旧缓冲可能仍被在途帧读取）
		if (!core->frame.frames_wait())
			return false;
		//回收旧缓冲（占位缓冲保留）
		if (core->memory.valid())
		{
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_counts_buffers.size(); ++slot)
				if ((*state_ptr).cluster_counts_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_counts_buffers[slot], (*state_ptr).cluster_counts_allocations[slot]);
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_offsets_buffers.size(); ++slot)
				if ((*state_ptr).cluster_offsets_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_offsets_buffers[slot], (*state_ptr).cluster_offsets_allocations[slot]);
			for (std::size_t slot = 0; slot < (*state_ptr).cluster_list_buffers.size(); ++slot)
				if ((*state_ptr).cluster_list_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).cluster_list_buffers[slot], (*state_ptr).cluster_list_allocations[slot]);
		}
		(*state_ptr).cluster_counts_buffers.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).cluster_counts_allocations.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).cluster_offsets_buffers.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).cluster_offsets_allocations.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).cluster_list_buffers.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		(*state_ptr).cluster_list_allocations.assign(core->config.frame_overlap, VK_NULL_HANDLE);
		//逐槽创建计数 / 偏移 / 列表缓冲（主机可见存储缓冲）
		for (uint32_t slot = 0; slot < core->config.frame_overlap; ++slot)
			if (!core->memory.buffer_create((*state_ptr).cluster_counts_buffers[slot], (*state_ptr).cluster_counts_allocations[slot],
				static_cast<VkDeviceSize>(cluster_total) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
				|| !core->memory.buffer_create((*state_ptr).cluster_offsets_buffers[slot], (*state_ptr).cluster_offsets_allocations[slot],
				static_cast<VkDeviceSize>(cluster_total) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
				|| !core->memory.buffer_create((*state_ptr).cluster_list_buffers[slot], (*state_ptr).cluster_list_allocations[slot],
				static_cast<VkDeviceSize>(list_capacity) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
			{
				logger.warn("渲染器:: 簇数据缓冲创建失败（帧槽 {}）", slot);
				(*state_ptr).cluster_counts_capacity = 0;
				(*state_ptr).cluster_list_capacity = 0;
				return false;
			}
		//记录新容量
		(*state_ptr).cluster_counts_capacity = cluster_total;
		(*state_ptr).cluster_list_capacity = list_capacity;
		//重写全部帧槽的簇数据绑定（binding 1 = 光源记录缓冲，由 cluster_ensure 负责）
		for (uint32_t slot = 0; slot < core->config.frame_overlap; ++slot)
			if (!owner->resource().scene_cluster_bind(slot, reinterpret_cast<uint64_t>((*state_ptr).light_buffers[slot]),
				reinterpret_cast<uint64_t>((*state_ptr).cluster_counts_buffers[slot]), reinterpret_cast<uint64_t>((*state_ptr).cluster_offsets_buffers[slot]),
				reinterpret_cast<uint64_t>((*state_ptr).cluster_list_buffers[slot])))
			{
				logger.error("渲染器:: 簇数据绑定写入失败（帧槽 {}）", slot);
				return false;
			}
		return true;
	}

	/*
	把后处理采样集绑定到给定目标视图组（第九十七轮；第一百二十三轮增加场景深度）
	签名（采样器 ＋ 视图组与数量 ＋ 深度视图）未变则直接返回；变化时先等尽在途帧，再逐帧槽写入。
	binding 0 = 该级源（views[index]）、binding 1 = 场景目标（views[0]）、
	binding 2 = 场景深度（depth_view 非空时才写，供景深类片元采样）、
	binding 4 = 方向光阴影图（第三百九十九轮；视图由本函数自查目标池，未创建时不写）。
	*/
	bool Device_State::post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler, VkImageView depth_view)
	{
		//数量钳到每帧槽后处理集上限
		if (count > PRESENT_TARGET_MAX)
			count = PRESENT_TARGET_MAX;
		//取方向光阴影图视图（未创建时为空，后续不写 binding 4；第三百九十九轮）
		Render_Target_Native_Info shadow_info{};
		owner->resource().render_target_native_get(shadow_target, shadow_info);
		VkImageView shadow_view = reinterpret_cast<VkImageView>(shadow_info.color_view);
		//签名比较：采样器、数量、深度、阴影与逐视图一致则空操作
		bool same = post_bind_cached && post_bound_sampler == sampler && post_bound_view_count == count
			&& post_bound_depth == depth_view && post_bound_shadow == shadow_view;
		for (uint32_t index = 0; same && index < count; ++index)
			if (post_bound_views[index] != views[index])
				same = false;
		if (same)
			return true;
		//等尽在途帧，确保目标帧槽描述符集不被任何命令缓冲引用
		if (!frame.frames_wait())
			return false;
		//逐帧槽写入
		for (uint32_t slot = 0; slot < config.frame_overlap; ++slot)
			for (uint32_t index = 0; index < count; ++index)
			{
				owner->resource().post_stage_bind(slot, index, reinterpret_cast<uint64_t>(sampler),
					reinterpret_cast<uint64_t>(views[index]), reinterpret_cast<uint64_t>(views[0]),
					reinterpret_cast<uint64_t>(depth_view), reinterpret_cast<uint64_t>(shadow_view));
			}
		//更新签名缓存
		post_bound_sampler = sampler;
		post_bound_view_count = count;
		post_bound_depth = depth_view;
		post_bound_shadow = shadow_view;
		for (uint32_t index = 0; index < count; ++index)
			post_bound_views[index] = views[index];
		post_bind_cached = true;
		return true;
	}

	//确保后处理全屏四边形网格就绪（惰性创建；单级路径与链路径共用）
	bool Device_State::post_mesh_ensure(void)
	{
		//全屏四边形网格尚未创建
		if (fullscreen_mesh == 0)
		{
			//全屏四边形（NDC 范围 -1..1；绕序取 (0,2,1)/(0,3,2) 以匹配「不做 Y 翻转」的顶点输出）
			Mesh_Data data{};
			data.vertices = {
				{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
				{ { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
				{ { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
				{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
			};
			data.indices = { 0, 2, 1, 0, 3, 2 };
			//网格句柄（创建失败则整体失败）
			Mesh_Handle created = 0;
			if (!owner->resource().mesh_load(created, data))
			{
				logger.error("渲染器:: 后处理全屏网格创建失败");
				return false;
			}
			fullscreen_mesh = created;
		}
		return true;
	}

	//确保内置单级后处理具名管线就绪（惰性登记；仅既有单级默认路径使用）
	bool Device_State::post_pipeline_ensure(void)
	{
		if (post_pipeline == 0)
		{
			uint64_t registered = 0;
			/*
			HDR 默认路径以色调映射收尾（第 757 轮）
			hdr_targets 开启时场景写 16F 目标，末级须做色调映射把线性高光压回显示域；
			关闭时保持既有直通片元，画面逐字节不变。
			算子由 hdr_tone_operator 选择（第 759 轮；0 = ACES、1 = 胶片、2 = 指数、3 = 扩展 Reinhard）。
			*/
			const char* fragment = "后处理.frag.spv";
			if (hdr_targets)
			{
				switch (hdr_tone_operator)
				{
				case 1:
					fragment = "胶片色调映射.frag.spv";
					break;
				case 2:
					fragment = "指数色调映射.frag.spv";
					break;
				case 3:
					fragment = "Reinhard扩展色调映射.frag.spv";
					break;
				case 4:
					fragment = "AgX色调映射.frag.spv";
					break;
				default:
					fragment = "色调映射.frag.spv";
					break;
				}
			}
			if (!owner->resource().pipeline_load_ex(registered, "后处理.vert.spv", fragment,
				Blend_Mode::Opaque, false, false, true, false, true, false, 0, 1))
			{
				logger.error("渲染器:: 后处理管线登记失败");
				return false;
			}
			post_pipeline = registered;
		}
		return true;
	}

	//确保既有单级默认后处理所需资源就绪（默认后处理管线 ＋ 全屏网格）
	bool Device_State::post_resources_ensure(void)
	{
		//先确保默认后处理管线，再确保全屏网格
		if (!post_pipeline_ensure())
			return false;
		return post_mesh_ensure();
	}

	//确保 SSAA 降采样具名管线就绪（惰性登记）
	bool Device_State::ssaa_pipeline_ensure(void)
	{
		if (ssaa_pipeline == 0)
		{
			uint64_t registered = 0;
			if (!owner->resource().pipeline_load_ex(registered, "后处理.vert.spv", "降采样.frag.spv",
				Blend_Mode::Opaque, false, false, true, false, true, false, 0, 1))
			{
				logger.error("渲染器:: SSAA 降采样管线登记失败");
				return false;
			}
			ssaa_pipeline = registered;
		}
		return true;
	}

	//确保天空盒具名管线就绪（惰性登记）
	bool Device_State::sky_pipeline_ensure(void)
	{
		return owner->resource().sky_pipeline_resource_ensure();
	}

	//销毁 SSAA 超采样目标（关闭时调用）
	void Device_State::ssaa_targets_destroy(void)
	{
		//逐个销毁已建立的目标（句柄无效时内部忽略）
		if (ssaa_scene_target != 0)
			owner->resource().render_target_release(ssaa_scene_target);
		if (ssaa_resolve_target != 0)
			owner->resource().render_target_release(ssaa_resolve_target);
		//句柄与建立参数一并清零
		ssaa_scene_target = 0;
		ssaa_resolve_target = 0;
		ssaa_extent_width = 0;
		ssaa_extent_height = 0;
		ssaa_target_factor = 0;
	}

	//确保 SSAA 超采样目标就绪（场景目标 = 输出 × 倍率；解析目标 = 输出尺寸）
	bool Device_State::ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor)	{
		//尺寸无效或该尺寸已失败过则直接返回（避免逐帧重复尝试）
		if (width == 0 || height == 0)
			return false;
		if (ssaa_extent_failed)
			return false;
		//尺寸与倍率均未变化且目标齐备则复用
		if (ssaa_scene_target != 0 && ssaa_resolve_target != 0
			&& ssaa_extent_width == width && ssaa_extent_height == height && ssaa_target_factor == factor)
			return true;
		//参数变化：等尽在途帧后销毁旧目标（避免释放仍在使用的附件）
		if (ssaa_scene_target != 0 || ssaa_resolve_target != 0)
		{
			frame.frames_wait();
			ssaa_targets_destroy();
		}
		//按交换链格式创建场景目标（N 倍）与解析目标（输出尺寸）
		uint64_t scene = 0;
		uint64_t resolve = 0;
		if (!owner->resource().render_target_create_format(scene, width * factor, height * factor,
			static_cast<uint32_t>(swapchain.format_get()), static_cast<uint32_t>(swapchain.depth_format_get())))
		{
			logger.warn("渲染器:: SSAA 场景目标创建失败（{}×{} ×{}），本帧回落", width, height, factor);
			ssaa_extent_failed = true;
			return false;
		}
		if (!owner->resource().render_target_create_format(resolve, width, height,
			static_cast<uint32_t>(swapchain.format_get()), static_cast<uint32_t>(swapchain.depth_format_get())))
		{
			logger.warn("渲染器:: SSAA 解析目标创建失败（{}×{}），本帧回落", width, height);
			owner->resource().render_target_release(scene);
			ssaa_extent_failed = true;
			return false;
		}
		//记录句柄与建立参数
		ssaa_scene_target = scene;
		ssaa_resolve_target = resolve;
		ssaa_extent_width = width;
		ssaa_extent_height = height;
		ssaa_target_factor = factor;
		logger.info("渲染器:: SSAA 目标已建立（{}×{} ×{}）", width, height, factor);
		return true;
	}

	/*
	确保 HDR 默认路径的 16F 场景目标就绪（第 757 轮）
	hdr_targets 开启且本帧无显式目标时，场景写入该目标、再由色调映射级呈现到交换链；
	目标随交换链尺寸重建，建立失败时本帧回落原路径（成功后同尺寸复用）。
	*/
	bool Device_State::hdr_scene_ensure(uint32_t width, uint32_t height)
	{
		//尺寸无效或该尺寸已失败过则直接返回（避免逐帧重复尝试）
		if (width == 0 || height == 0)
			return false;
		if (hdr_scene_failed)
			return false;
		//尺寸未变化且目标齐备则复用
		if (hdr_scene_target != 0 && hdr_scene_width == width && hdr_scene_height == height)
			return true;
		//尺寸变化：等尽在途帧后销毁旧目标（避免释放仍在使用的附件）
		if (hdr_scene_target != 0)
		{
			frame.frames_wait();
			hdr_scene_destroy();
		}
		//按 16F 创建场景目标（显式格式，不受 hdr_targets 开关二次影响）
		uint64_t target = 0;
		if (!owner->resource().render_target_create_format(target, width, height,
			static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
			static_cast<uint32_t>(swapchain.depth_format_get())))
		{
			logger.warn("渲染器:: HDR 场景目标创建失败（{}×{}），本帧回落原路径", width, height);
			hdr_scene_failed = true;
			return false;
		}
		hdr_scene_target = target;
		hdr_scene_width = width;
		hdr_scene_height = height;
		logger.info("渲染器:: HDR 场景目标已建立（{}×{}，16F）", width, height);
		return true;
	}

	//销毁 HDR 默认路径的 16F 场景目标
	void Device_State::hdr_scene_destroy(void)
	{
		//句柄有效则归还资源域（内部按延迟销毁队列处理）
		if (hdr_scene_target != 0)
			owner->resource().render_target_release(hdr_scene_target);
		hdr_scene_target = 0;
		hdr_scene_width = 0;
		hdr_scene_height = 0;
	}

	/*
	确保 HDR 自动泛光的中间目标与管线就绪（第 761 轮）
	三个 16F 中间目标：提取输出 / 模糊输出 / 叠加输出；三条管线（颜色格式同为 16F）：
	亮部提取、模糊、泛光叠加。末级色调映射复用默认后处理管线（写交换链）。
	*/
	bool Device_State::hdr_bloom_ensure(uint32_t width, uint32_t height)
	{
		//尺寸无效或该尺寸已失败过则直接返回
		if (width == 0 || height == 0)
			return false;
		if (hdr_bloom_failed)
			return false;
		//尺寸未变化且资源齐备则复用
		if (hdr_bloom_targets[0] != 0 && hdr_bloom_targets[1] != 0 && hdr_bloom_targets[2] != 0
			&& hdr_bloom_pipelines[0] != 0 && hdr_bloom_pipelines[1] != 0 && hdr_bloom_pipelines[2] != 0
			&& hdr_bloom_width == width && hdr_bloom_height == height)
			return true;
		//尺寸变化：等尽在途帧后销毁旧目标（管线与尺寸无关，保留）
		if (hdr_bloom_targets[0] != 0 || hdr_bloom_targets[1] != 0 || hdr_bloom_targets[2] != 0)
		{
			frame.frames_wait();
			for (std::size_t index = 0; index < 3; ++index)
			{
				if (hdr_bloom_targets[index] != 0)
					owner->resource().render_target_release(hdr_bloom_targets[index]);
				hdr_bloom_targets[index] = 0;
			}
			hdr_bloom_width = 0;
			hdr_bloom_height = 0;
		}
		//按 16F 创建三个中间目标
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (!owner->resource().render_target_create_format(hdr_bloom_targets[index], width, height,
				static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
				static_cast<uint32_t>(swapchain.depth_format_get())))
			{
				logger.warn("渲染器:: HDR 泛光中间目标创建失败（{}×{}，第 {} 个），本帧回落不泛光", width, height,
					static_cast<uint32_t>(index));
				hdr_bloom_failed = true;
				return false;
			}
		}
		//惰性登记三条 16F 后处理管线（颜色格式 = 16F，与中间目标一致）
		const char* fragments[3] = { "亮部提取.frag.spv", "模糊.frag.spv", "泛光叠加.frag.spv" };
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (hdr_bloom_pipelines[index] != 0)
				continue;
			uint64_t registered = 0;
			if (!owner->resource().pipeline_load_ex(registered, "后处理.vert.spv", fragments[index],
				Blend_Mode::Opaque, false, false, true, false, true, false, 0, 0,
				static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT)))
			{
				logger.warn("渲染器:: HDR 泛光管线登记失败（{}），本帧回落不泛光", fragments[index]);
				hdr_bloom_failed = true;
				return false;
			}
			hdr_bloom_pipelines[index] = registered;
		}
		hdr_bloom_width = width;
		hdr_bloom_height = height;
		logger.info("渲染器:: HDR 泛光资源已建立（{}×{}，16F）", width, height);
		return true;
	}

	//销毁 HDR 自动泛光的中间目标（管线随资源域统一回收）
	void Device_State::hdr_bloom_destroy(void)
	{
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (hdr_bloom_targets[index] != 0)
				owner->resource().render_target_release(hdr_bloom_targets[index]);
			hdr_bloom_targets[index] = 0;
		}
		hdr_bloom_width = 0;
		hdr_bloom_height = 0;
		hdr_bloom_chain_active = false;
	}

	/*
	本帧应用 HDR 自动泛光（第 761 轮）
	先还原上一帧由本路径填充的链与额外目标（避免污染用户可见状态），
	再在满足条件时填充「提取 → 模糊 → 叠加 → 色调映射」四级链与三个中间目标。
	*/
	void Device_State::hdr_bloom_frame_apply(void)
	{
		//还原上一帧自动填充的内容（仅当链仍是自动链时清除）
		if (hdr_bloom_chain_active)
		{
			bool same = post_chain.size() == 4
				&& post_chain[0] == hdr_bloom_pipelines[0]
				&& post_chain[1] == hdr_bloom_pipelines[1]
				&& post_chain[2] == hdr_bloom_pipelines[2]
				&& post_chain[3] == post_pipeline;
			if (same)
			{
				post_chain.clear();
				present_extra.clear();
			}
			hdr_bloom_chain_active = false;
		}
		//未开启或已失败则不介入
		if (!hdr_auto_bloom || hdr_bloom_failed)
			return;
		//用户已设链或额外目标时不介入（保留用户配置）
		if (!post_chain.empty() || !present_extra.empty())
			return;
		//目标与三条管线就绪
		if (!hdr_bloom_ensure(hdr_scene_width, hdr_scene_height))
			return;
		//末级色调映射管线（与默认路径共用；按算子选择片元）
		if (!post_pipeline_ensure())
			return;
		//四级链
		post_chain.clear();
		post_chain.push_back(hdr_bloom_pipelines[0]);
		post_chain.push_back(hdr_bloom_pipelines[1]);
		post_chain.push_back(hdr_bloom_pipelines[2]);
		post_chain.push_back(post_pipeline);
		//三个中间目标（场景目标已在 effective_present_target）
		present_extra.clear();
		present_extra.push_back(hdr_bloom_targets[0]);
		present_extra.push_back(hdr_bloom_targets[1]);
		present_extra.push_back(hdr_bloom_targets[2]);
		//模糊半径 4（此时链长为 4，级序 1 合法）
		owner->device().post_stage_param_set(1, 0.6f, 0.1f, 4.0f);
		hdr_bloom_chain_active = true;
	}

}
