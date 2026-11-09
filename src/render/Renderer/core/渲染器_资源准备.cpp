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
	//重建交换链并把旧资源压入帧延迟销毁队列
	bool Renderer_Impl_Holder::Impl::swapchain_rebuild_deferred(uint32_t width, uint32_t height)
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
		Hi-Z 资源跟随交换链尺寸（第二百五十三轮）
		已构建时按新尺寸重建；失败则整体销毁并降级为关闭（不阻断重建后的出帧）。
		*/
		if (hiz.valid())
		{
			VkExtent2D extent = swapchain.extent_get();
			//尺寸变化后重建历史与金字塔
			if (!hiz.extent_set(extent.width, extent.height, swapchain.depth_format_get()))
			{
				logger.warn("渲染器:: Hi-Z 资源尺寸重建失败（已降级为关闭）");
				hiz.destroy();
				hiz_enabled = false;
			}
		}
		return true;
	}

	/*
	确保 Hi-Z 资源就绪（第二百五十三轮）
	能力不足（计算不可用 / R32 浮点存储图像不可用）时返回假；尺寸与当前交换链一致时为空操作。
	*/
	bool Renderer_Impl_Holder::Impl::hiz_ensure(void)
	{
		//能力不足
		if (!hiz_capable || !compute.supported())
			return false;
		//关联计算上下文（第二百五十四轮：生成经其描述符集与调度）
		hiz.compute_set(&compute);
		//当前交换链范围（为 0 表示尚未就绪）
		VkExtent2D extent = swapchain.extent_get();
		if (extent.width == 0 || extent.height == 0)
			return false;
		//已就绪且尺寸一致
		if (hiz.valid() && hiz.width_get() == extent.width && hiz.height_get() == extent.height
			&& hiz.depth_format_get() == swapchain.depth_format_get())
			return true;
		//已构建但尺寸 / 格式变化：按新参数重建
		if (hiz.valid())
			return hiz.extent_set(extent.width, extent.height, swapchain.depth_format_get());
		//首次构建（每帧槽一份历史 ＋ 一份金字塔）
		return hiz.build(device.device_get(), memory, config.frame_overlap, extent.width, extent.height,
			swapchain.depth_format_get());
	}

	/*
	确保光源记录 SSBO 就绪（第二百六十六轮）
	计算路径可用时按帧槽各建一个主机可见存储缓冲；已就绪时为空操作，构建失败回滚为无资源。
	*/
	bool Renderer_Impl_Holder::Impl::cluster_ensure(void)
	{
		//能力不足
		if (!cluster_capable || !compute.supported())
			return false;
		//已就绪（逐槽缓冲均已建立）
		if (light_buffers.size() == config.frame_overlap)
		{
			//逐槽检查是否有空句柄
			bool ready = true;
			for (std::size_t slot = 0; slot < light_buffers.size(); ++slot)
				if (light_buffers[slot] == VK_NULL_HANDLE)
					ready = false;
			if (ready)
				return true;
		}
		//清理半初始化或旧资源
		cluster_destroy();
		light_buffers.assign(config.frame_overlap, VK_NULL_HANDLE);
		light_allocations.assign(config.frame_overlap, VK_NULL_HANDLE);
		for (uint32_t slot = 0; slot < config.frame_overlap; ++slot)
		{
			//该帧槽光源记录缓冲（容量 × 80 字节；计算着色器只读）
			if (!memory.buffer_create(light_buffers[slot], light_allocations[slot],
				static_cast<VkDeviceSize>(LIGHT_RECORD_CAPACITY) * LIGHT_RECORD_BYTES,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
			{
				logger.warn("渲染器:: 光源记录缓冲创建失败（帧槽 {}）", slot);
				cluster_destroy();
				return false;
			}
		}
		return true;
	}

	//销毁光源记录 SSBO（第二百六十六轮；幂等）
	void Renderer_Impl_Holder::Impl::cluster_destroy(void)
	{
		//依赖内存模块，须先于 memory 销毁
		if (memory.valid())
			for (std::size_t slot = 0; slot < light_buffers.size(); ++slot)
				//归还有效缓冲与其内存
				if (light_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(light_buffers[slot], light_allocations[slot]);
		light_buffers.clear();
		light_allocations.clear();
		last_cluster_light_count = 0;
	}

	//销毁图形 set0 的簇数据缓冲与占位缓冲（第二百八十八轮；幂等）
	void Renderer_Impl_Holder::Impl::cluster_graphics_destroy(void)
	{
		//依赖内存模块，须先于 memory 销毁
		if (memory.valid())
		{
			//逐槽归还计数 / 偏移 / 列表缓冲
			for (std::size_t slot = 0; slot < cluster_counts_buffers.size(); ++slot)
				if (cluster_counts_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_counts_buffers[slot], cluster_counts_allocations[slot]);
			for (std::size_t slot = 0; slot < cluster_offsets_buffers.size(); ++slot)
				if (cluster_offsets_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_offsets_buffers[slot], cluster_offsets_allocations[slot]);
			for (std::size_t slot = 0; slot < cluster_list_buffers.size(); ++slot)
				if (cluster_list_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_list_buffers[slot], cluster_list_allocations[slot]);
			//归还占位缓冲
			if (cluster_placeholder_buffer != VK_NULL_HANDLE)
				memory.buffer_destroy(cluster_placeholder_buffer, cluster_placeholder_allocation);
		}
		cluster_counts_buffers.clear();
		cluster_counts_allocations.clear();
		cluster_offsets_buffers.clear();
		cluster_offsets_allocations.clear();
		cluster_list_buffers.clear();
		cluster_list_allocations.clear();
		cluster_placeholder_buffer = VK_NULL_HANDLE;
		cluster_placeholder_allocation = VK_NULL_HANDLE;
		cluster_counts_capacity = 0;
		cluster_list_capacity = 0;
		last_cluster_gpu_bytes = 0;
	}

	/*
	确保簇数据占位缓冲就绪（第二百八十八轮）
	64 字节主机可见存储缓冲；Cluster 关闭时 set0 binding 1..4 统一指向它。
	*/
	bool Renderer_Impl_Holder::Impl::cluster_placeholder_ensure(void)
	{
		//已就绪
		if (cluster_placeholder_buffer != VK_NULL_HANDLE)
			return true;
		//内存模块缺失
		if (!memory.valid())
			return false;
		//创建占位缓冲（不足一页，任意长度均满足描述符完整性要求）
		return memory.buffer_create(cluster_placeholder_buffer, cluster_placeholder_allocation,
			64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
	}

	/*
	确保图形 set0 的簇数据缓冲就绪（第二百八十八轮）
	容量足够且逐槽句柄齐备时为空操作；否则等尽在途帧后按新容量重建，并重写全部帧槽的 binding 1..4。
	*/
	bool Renderer_Impl_Holder::Impl::cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity)
	{
		//能力不足或入参非法
		if (!cluster_capable || !compute.supported() || cluster_total == 0 || list_capacity == 0)
			return false;
		//已就绪：槽数与容量足够且逐槽句柄齐备
		bool ready = cluster_counts_buffers.size() == config.frame_overlap
			&& cluster_counts_capacity >= cluster_total && cluster_list_capacity >= list_capacity;
		if (ready)
			for (std::size_t slot = 0; slot < cluster_counts_buffers.size(); ++slot)
				if (cluster_counts_buffers[slot] == VK_NULL_HANDLE
					|| cluster_offsets_buffers[slot] == VK_NULL_HANDLE
					|| cluster_list_buffers[slot] == VK_NULL_HANDLE)
					ready = false;
		//已就绪则无需重建
		if (ready)
			return true;
		//重建前等尽在途帧（旧缓冲可能仍被在途帧读取）
		if (!frame.frames_wait())
			return false;
		//回收旧缓冲（占位缓冲保留）
		if (memory.valid())
		{
			for (std::size_t slot = 0; slot < cluster_counts_buffers.size(); ++slot)
				if (cluster_counts_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_counts_buffers[slot], cluster_counts_allocations[slot]);
			for (std::size_t slot = 0; slot < cluster_offsets_buffers.size(); ++slot)
				if (cluster_offsets_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_offsets_buffers[slot], cluster_offsets_allocations[slot]);
			for (std::size_t slot = 0; slot < cluster_list_buffers.size(); ++slot)
				if (cluster_list_buffers[slot] != VK_NULL_HANDLE)
					memory.buffer_destroy(cluster_list_buffers[slot], cluster_list_allocations[slot]);
		}
		cluster_counts_buffers.assign(config.frame_overlap, VK_NULL_HANDLE);
		cluster_counts_allocations.assign(config.frame_overlap, VK_NULL_HANDLE);
		cluster_offsets_buffers.assign(config.frame_overlap, VK_NULL_HANDLE);
		cluster_offsets_allocations.assign(config.frame_overlap, VK_NULL_HANDLE);
		cluster_list_buffers.assign(config.frame_overlap, VK_NULL_HANDLE);
		cluster_list_allocations.assign(config.frame_overlap, VK_NULL_HANDLE);
		//逐槽创建计数 / 偏移 / 列表缓冲（主机可见存储缓冲）
		for (uint32_t slot = 0; slot < config.frame_overlap; ++slot)
			if (!memory.buffer_create(cluster_counts_buffers[slot], cluster_counts_allocations[slot],
				static_cast<VkDeviceSize>(cluster_total) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
				|| !memory.buffer_create(cluster_offsets_buffers[slot], cluster_offsets_allocations[slot],
				static_cast<VkDeviceSize>(cluster_total) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)
				|| !memory.buffer_create(cluster_list_buffers[slot], cluster_list_allocations[slot],
				static_cast<VkDeviceSize>(list_capacity) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
			{
				logger.warn("渲染器:: 簇数据缓冲创建失败（帧槽 {}）", slot);
				cluster_counts_capacity = 0;
				cluster_list_capacity = 0;
				return false;
			}
		//记录新容量
		cluster_counts_capacity = cluster_total;
		cluster_list_capacity = list_capacity;
		//重写全部帧槽的簇数据绑定（binding 1 = 光源记录缓冲，由 cluster_ensure 负责）
		for (uint32_t slot = 0; slot < config.frame_overlap; ++slot)
			if (!pipeline.scene_cluster_bind(slot, light_buffers[slot],
				cluster_counts_buffers[slot], cluster_offsets_buffers[slot], cluster_list_buffers[slot]))
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
	bool Renderer_Impl_Holder::Impl::post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler, VkImageView depth_view)
	{
		//数量钳到每帧槽后处理集上限
		if (count > PRESENT_TARGET_MAX)
			count = PRESENT_TARGET_MAX;
		//取方向光阴影图视图（未创建时为空，后续不写 binding 4；第三百九十九轮）
		VkImage shadow_image = VK_NULL_HANDLE;
		VkImageView shadow_view = VK_NULL_HANDLE;
		VkImageView shadow_depth_view = VK_NULL_HANDLE;
		VkExtent2D shadow_extent{};
		render_target.target_get(shadow_target, shadow_image, shadow_view, shadow_depth_view, shadow_extent);
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
				pipeline.post_texture_bind(slot, sampler, views[index], index);
				pipeline.post_scene_bind(slot, sampler, views[0], index);
				//深度视图非空时一并写入（景深等深度采样阶段使用）
				if (depth_view != VK_NULL_HANDLE)
					pipeline.post_depth_bind(slot, sampler, depth_view, index);
				//阴影视图非空时一并写入（体积光等阴影判定阶段使用；第三百九十九轮）
				if (shadow_view != VK_NULL_HANDLE)
					pipeline.post_shadow_bind(slot, sampler, shadow_view, index);
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
	bool Renderer_Impl_Holder::Impl::post_mesh_ensure(void)
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
			if (!mesh.mesh_create(created, data))
			{
				logger.error("渲染器:: 后处理全屏网格创建失败");
				return false;
			}
			fullscreen_mesh = created;
		}
		return true;
	}

	//确保内置单级后处理具名管线就绪（惰性登记；仅既有单级默认路径使用）
	bool Renderer_Impl_Holder::Impl::post_pipeline_ensure(void)
	{
		//后处理具名管线尚未登记
		if (post_pipeline == 0)
		{
			//加载后处理顶点 / 片元着色器模块（顺带解析顶点输入位置）
			VkShaderModule vert_module = VK_NULL_HANDLE;
			VkShaderModule frag_module = VK_NULL_HANDLE;
			uint32_t vert_input_locations = 0;
			if (!shader.module_load(vert_module, "后处理.vert.spv", &vert_input_locations))
			{
				logger.error("渲染器:: 后处理顶点着色器加载失败");
				return false;
			}
			if (!shader.module_load(frag_module, "后处理.frag.spv"))
			{
				logger.error("渲染器:: 后处理片元着色器加载失败");
				shader.module_destroy(vert_module);
				return false;
			}
			//登记具名管线（不透明档；与默认管线共享同一布局与描述符集布局）
			uint64_t registered = 0;
			//内置后处理管线固定单采样（第九十八轮：全屏 pass 无需多重采样）
			if (!pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque,
				VK_FORMAT_UNDEFINED, false, 1, vert_input_locations))
			{
				logger.error("渲染器:: 后处理管线登记失败");
				shader.module_destroy(vert_module);
				shader.module_destroy(frag_module);
				return false;
			}
			//记录句柄与其着色器模块（模块所有权在本实现体，随 extra_pipelines 一并销毁）
			post_pipeline = registered;
			extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
			logger.info("渲染器:: 后处理管线已登记（句柄 {}）", registered);
		}
		return true;
	}

	//确保既有单级默认后处理所需资源就绪（默认后处理管线 ＋ 全屏网格）
	bool Renderer_Impl_Holder::Impl::post_resources_ensure(void)
	{
		//先确保默认后处理管线，再确保全屏网格
		if (!post_pipeline_ensure())
			return false;
		return post_mesh_ensure();
	}

	//确保 SSAA 降采样具名管线就绪（惰性登记）
	bool Renderer_Impl_Holder::Impl::ssaa_pipeline_ensure(void)
	{
		//降采样具名管线尚未登记
		if (ssaa_pipeline == 0)
		{
			//加载后处理顶点 / 降采样片元着色器模块（顺带解析顶点输入位置）
			VkShaderModule vert_module = VK_NULL_HANDLE;
			VkShaderModule frag_module = VK_NULL_HANDLE;
			uint32_t vert_input_locations = 0;
			if (!shader.module_load(vert_module, "后处理.vert.spv", &vert_input_locations))
			{
				logger.error("渲染器:: SSAA 降采样顶点着色器加载失败");
				return false;
			}
			if (!shader.module_load(frag_module, "降采样.frag.spv"))
			{
				logger.error("渲染器:: SSAA 降采样片元着色器加载失败");
				shader.module_destroy(vert_module);
				return false;
			}
			//登记具名管线（不透明档；与默认管线共享同一布局与描述符集布局）
			uint64_t registered = 0;
			//SSAA 降采样为全屏 pass，固定单采样（第九十八轮）
			if (!pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque,
				VK_FORMAT_UNDEFINED, false, 1, vert_input_locations))
			{
				logger.error("渲染器:: SSAA 降采样管线登记失败");
				shader.module_destroy(vert_module);
				shader.module_destroy(frag_module);
				return false;
			}
			//记录句柄与其着色器模块（模块所有权在本实现体，随 extra_pipelines 一并销毁）
			ssaa_pipeline = registered;
			extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
			logger.info("渲染器:: SSAA 降采样管线已登记（句柄 {}）", registered);
		}
		return true;
	}

	//确保天空盒具名管线就绪（惰性登记）
	bool Renderer_Impl_Holder::Impl::sky_pipeline_ensure(void)
	{
		//天空盒具名管线尚未登记
		if (sky_pipeline == 0)
		{
			//加载天空盒顶点 / 片元着色器模块（顺带解析顶点输入位置）
			VkShaderModule vert_module = VK_NULL_HANDLE;
			VkShaderModule frag_module = VK_NULL_HANDLE;
			uint32_t vert_input_locations = 0;
			if (!shader.module_load(vert_module, "天空盒.vert.spv", &vert_input_locations))
			{
				logger.error("渲染器:: 天空盒顶点着色器加载失败");
				return false;
			}
			if (!shader.module_load(frag_module, "天空盒.frag.spv"))
			{
				logger.error("渲染器:: 天空盒片元着色器加载失败");
				shader.module_destroy(vert_module);
				return false;
			}
			//登记具名管线（采样数跟随主场景配置；关闭背面剔除；深度只读 ＋ LESS_OR_EQUAL）
			uint64_t registered = 0;
			if (!pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque,
				VK_FORMAT_UNDEFINED, true, 0, vert_input_locations, false, true))
			{
				logger.error("渲染器:: 天空盒管线登记失败");
				shader.module_destroy(vert_module);
				shader.module_destroy(frag_module);
				return false;
			}
			//记录句柄与其着色器模块（模块所有权在本实现体，随 extra_pipelines 一并销毁）
			sky_pipeline = registered;
			extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
			logger.info("渲染器:: 天空盒管线已登记（句柄 {}）", registered);
		}
		return true;
	}

	//销毁 SSAA 超采样目标（关闭时调用）
	void Renderer_Impl_Holder::Impl::ssaa_targets_destroy(void)
	{
		//逐个销毁已建立的目标（句柄无效时内部忽略）
		if (ssaa_scene_target != 0)
			render_target.target_destroy(ssaa_scene_target);
		if (ssaa_resolve_target != 0)
			render_target.target_destroy(ssaa_resolve_target);
		//句柄与建立参数一并清零
		ssaa_scene_target = 0;
		ssaa_resolve_target = 0;
		ssaa_extent_width = 0;
		ssaa_extent_height = 0;
		ssaa_target_factor = 0;
	}

	//确保 SSAA 超采样目标就绪（场景目标 = 输出 × 倍率；解析目标 = 输出尺寸）
	bool Renderer_Impl_Holder::Impl::ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor)
	{
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
		if (!render_target.target_create(scene, width * factor, height * factor,
			swapchain.format_get(), swapchain.depth_format_get()))
		{
			logger.warn("渲染器:: SSAA 场景目标创建失败（{}×{} ×{}），本帧回落", width, height, factor);
			ssaa_extent_failed = true;
			return false;
		}
		if (!render_target.target_create(resolve, width, height,
			swapchain.format_get(), swapchain.depth_format_get()))
		{
			logger.warn("渲染器:: SSAA 解析目标创建失败（{}×{}），本帧回落", width, height);
			render_target.target_destroy(scene);
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

}
