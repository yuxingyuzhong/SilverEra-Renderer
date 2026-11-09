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
	//是否支持 GPU 时间戳
	bool Renderer_Compute::gpu_timestamp_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转调帧模块能力标志
		return impl->frame.gpu_timestamp_supported_get();
	}

	//是否支持像素回读
	bool Renderer_Compute::readback_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//交换链图像具备传输源用法才可回读
		return impl->swapchain.color_transfer_src_get();
	}

	//是否支持计算路径（第二百四十一轮）
	bool Renderer_Compute::compute_supported(void) const
	{
		//若尚未初始化或无健康检查管线
		if (impl == nullptr || !impl->initialized || impl->compute_health_handle == 0)
			return false;
		return impl->compute.supported();
	}

	//计算链路健康检查（第二百四十一轮）
	bool Renderer_Compute::compute_health_check(uint32_t magic, uint32_t& value_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//不支持计算路径
		if (!self().compute_supported())
			return false;
		return impl->compute.self_check_run(magic, value_out);
	}

	//按句柄取计算缓冲的原生 Vulkan 句柄（第三百四十一轮）
	VkBuffer Renderer_Impl_Holder::Impl::compute_buffer_native(uint64_t handle) const
	{
		//句柄无效
		if (handle == 0)
			return VK_NULL_HANDLE;
		//线性查找（缓冲数量有限）
		for (const Compute_Buffer_Record& record : compute_buffers)
			if (record.handle == handle)
				return record.buffer;
		return VK_NULL_HANDLE;
	}

	/*
	计算存储缓冲创建（第三百四十二轮；可追加 Vulkan 用法位）
	与 compute_buffer_load 同口径的主机可见 STORAGE 缓冲，额外支持追加用法（如 INDIRECT_BUFFER），
	供「GPU 侧间接命令缓冲」使用：计算着色器写、后续绘制命令读。
	*/
	bool Renderer_Impl_Holder::Impl::compute_buffer_create_usage(Compute_Buffer_Handle& handle_out, uint32_t size,
		const void* data, VkBufferUsageFlags extra_usage)
	{
		//失败路径一律复位句柄
		handle_out = 0;
		//尺寸非法
		if (size == 0 || size > COMPUTE_BUFFER_MAX_BYTES)
			return false;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		//主机可见存储缓冲（恒可作拷贝源）＋ 追加用法
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | extra_usage;
		if (!memory.buffer_create(buffer, allocation, size, usage, true, false))
			return false;
		//写入初值（可选）
		if (data != nullptr && !memory.buffer_write(buffer, allocation, data, size, 0))
		{
			memory.buffer_destroy(buffer, allocation);
			return false;
		}
		Compute_Buffer_Record record;
		record.handle = next_compute_buffer_handle++;
		record.buffer = buffer;
		record.allocation = allocation;
		record.size = size;
		record.host_visible = true;
		handle_out = record.handle;
		compute_buffers.push_back(record);
		return true;
	}

	//计算存储缓冲加载（第二百四十三轮；第二百四十四轮补设备本地）
	bool Renderer_Compute::compute_buffer_load(Compute_Buffer_Handle& handle_out, uint32_t size, const void* data, bool device_local)
	{
		//失败路径一律复位句柄（与材质句柄约定一致）
		handle_out = 0;
		//未初始化或不支持计算，或尺寸非法
		if (impl == nullptr || !impl->initialized || !self().compute_supported()
			|| size == 0 || size > COMPUTE_BUFFER_MAX_BYTES)
			return false;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		//存储缓冲用法：恒可作拷贝源（回读），设备本地另需拷贝目标（上传）
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (device_local)
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		//主机可见（默认）或设备本地
		if (!impl->memory.buffer_create(buffer, allocation, size, usage, !device_local, false))
		{
			logger.error("渲染器:: 计算存储缓冲创建失败（{} 字节）", size);
			return false;
		}
		//写入初值（可选）：主机可见直接映射，设备本地经暂存上传
		if (data != nullptr)
		{
			bool seeded = device_local
				? impl->memory.upload_buffer(buffer, data, size, 0)
				: impl->memory.buffer_write(buffer, allocation, data, size, 0);
			//初值写入失败即回收缓冲
			if (!seeded)
			{
				impl->memory.buffer_destroy(buffer, allocation);
				return false;
			}
		}
		Renderer_Impl_Holder::Impl::Compute_Buffer_Record record;
		record.handle = impl->next_compute_buffer_handle++;
		record.buffer = buffer;
		record.allocation = allocation;
		record.size = size;
		record.host_visible = !device_local;
		handle_out = record.handle;
		impl->compute_buffers.push_back(record);
		return true;
	}

	//计算存储缓冲卸载（第二百四十三轮）
	void Renderer_Compute::compute_buffer_unload(Compute_Buffer_Handle handle)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		for (std::size_t index = 0; index < impl->compute_buffers.size(); ++index)
		{
			//命中即移交延迟队列并移出列表（避免在途命令缓冲仍引用该缓冲）
			if (impl->compute_buffers[index].handle == handle)
			{
				Deferred_Entry entry{};
				Deferred_Memory_Resource resource{};
				resource.buffer = impl->compute_buffers[index].buffer;
				resource.buffer_allocation = impl->compute_buffers[index].allocation;
				entry.memory_resources.push_back(resource);
				//延迟 = 在途帧数 + 1（与网格 / 纹理卸载同口径）
				impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
				impl->compute_buffers.erase(impl->compute_buffers.begin() + static_cast<std::ptrdiff_t>(index));
				return;
			}
		}
	}

	//计算存储缓冲写入一段（第二百四十三轮）
	bool Renderer_Compute::compute_buffer_write(Compute_Buffer_Handle handle, const void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或数据为空
		if (impl == nullptr || !impl->initialized || data == nullptr || size == 0)
			return false;
		for (Renderer_Impl_Holder::Impl::Compute_Buffer_Record& record : impl->compute_buffers)
		{
			//命中句柄并做越界检查
			if (record.handle == handle)
			{
				if (static_cast<uint64_t>(offset) + size > record.size)
					return false;
				//设备本地缓冲经暂存上传，主机可见缓冲直接映射
				if (!record.host_visible)
					return impl->memory.upload_buffer(record.buffer, data, size, offset);
				return impl->memory.buffer_write(record.buffer, record.allocation, data, size, offset);
			}
		}
		return false;
	}

	//计算存储缓冲读回一段（第二百四十三轮）
	bool Renderer_Compute::compute_buffer_read(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或输出为空
		if (impl == nullptr || !impl->initialized || data == nullptr || size == 0)
			return false;
		for (Renderer_Impl_Holder::Impl::Compute_Buffer_Record& record : impl->compute_buffers)
		{
			//命中句柄并做越界检查
			if (record.handle == handle)
			{
				if (static_cast<uint64_t>(offset) + size > record.size)
					return false;
				return impl->memory.buffer_read(record.buffer, record.allocation, data, size, offset);
			}
		}
		return false;
	}

	//计算存储缓冲 GPU 回读（第二百四十四轮）
	bool Renderer_Compute::compute_readback(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset)
	{
		//未初始化或输出为空
		if (impl == nullptr || !impl->initialized || data == nullptr || size == 0)
			return false;
		//查找目标缓冲并做越界检查
		Renderer_Impl_Holder::Impl::Compute_Buffer_Record* target = nullptr;
		for (Renderer_Impl_Holder::Impl::Compute_Buffer_Record& record : impl->compute_buffers)
		{
			//命中句柄
			if (record.handle == handle)
			{
				target = &record;
				break;
			}
		}
		//句柄无效或越界
		if (target == nullptr || static_cast<uint64_t>(offset) + size > target->size)
			return false;
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_allocation = VK_NULL_HANDLE;
		//暂存缓冲（主机可见，仅拷贝目标）
		if (!impl->memory.buffer_create(staging, staging_allocation, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, false))
		{
			logger.error("渲染器:: 计算回读暂存缓冲创建失败（{} 字节）", size);
			return false;
		}
		VkBuffer source = target->buffer;
		//一次性提交：计算→传输屏障 → 拷到暂存 → 传输→主机屏障
		bool copied = impl->compute.single_submit([&](VkCommandBuffer command_buffer) -> bool
		{
			if (!impl->compute.barrier(command_buffer, Compute_Barrier::ComputeToTransfer))
				return false;
			VkBufferCopy region{};
			region.srcOffset = offset;
			region.dstOffset = 0;
			region.size = size;
			vkCmdCopyBuffer(command_buffer, source, staging, 1, &region);
			return impl->compute.barrier(command_buffer, Compute_Barrier::TransferToHost);
		});
		//拷贝成功才映射读回
		bool read = copied && impl->memory.buffer_read(staging, staging_allocation, data, size, 0);
		impl->memory.buffer_destroy(staging, staging_allocation);
		return read;
	}

	//计算统计快照（第二百四十五轮）
	Compute_Stats Renderer_Compute::compute_stats_get(void) const
	{
		//未初始化：计数为零、计时 −1
		if (impl == nullptr || !impl->initialized)
		{
			Compute_Stats empty;
			return empty;
		}
		return impl->compute.stats_get();
	}

	//是否支持计算侧时间戳（第二百四十五轮）
	bool Renderer_Compute::compute_timestamp_supported(void) const
	{
		//未初始化或不支持计算路径
		if (impl == nullptr || !impl->initialized || !self().compute_supported())
			return false;
		return impl->compute.timestamp_supported();
	}

	//GPU 能力与降级矩阵（第二百四十六轮）
	Compute_Capabilities Renderer_Compute::compute_capabilities_get(void) const
	{
		//未初始化：各项为假
		if (impl == nullptr || !impl->initialized)
			return Compute_Capabilities{};
		return impl->compute_capabilities;
	}

	//视锥剔除原语（第二百四十八轮）
	bool Renderer_Compute::gpu_cull_mask(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], uint32_t* mask_out)
	{
		//未初始化或不支持计算路径
		if (impl == nullptr || !impl->initialized || !self().compute_supported())
			return false;
		//入参非法
		if (transforms == nullptr || mask_out == nullptr || count == 0
			|| local_min == nullptr || local_max == nullptr)
			return false;
		//当前视图 × 投影 → 六平面
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		Frustum_Planes planes;
		if (!frustum_planes_extract(planes, view_proj))
			return false;
		//CPU 侧逐实例求世界包围球（vec4：中心 xyz ＋ 半径）
		std::vector<float> spheres(static_cast<std::size_t>(count) * 4, 0.0f);
		for (uint32_t index = 0; index < count; ++index)
		{
			World_Bounds bounds;
			//包围体构造失败（矩阵非法）
			if (!world_bounds_build(bounds, local_min, local_max, transforms + static_cast<std::size_t>(index) * 16))
				return false;
			const std::size_t offset = static_cast<std::size_t>(index) * 4;
			spheres[offset + 0] = bounds.center[0];
			spheres[offset + 1] = bounds.center[1];
			spheres[offset + 2] = bounds.center[2];
			spheres[offset + 3] = bounds.radius;
		}
		//球缓冲与掩码缓冲（主机可见，便于直接回读）
		Compute_Buffer_Handle sphere_handle = 0;
		Compute_Buffer_Handle mask_handle = 0;
		if (!self().compute_buffer_load(sphere_handle, static_cast<uint32_t>(spheres.size() * sizeof(float)), spheres.data()))
			return false;
		//掩码缓冲全量由着色器写入，无需初值
		if (!self().compute_buffer_load(mask_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			self().compute_buffer_unload(sphere_handle);
			return false;
		}
		//剔除管线懒加载
		if (impl->gpu_cull_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "视锥剔除.comp.spv"))
			{
				self().compute_buffer_unload(mask_handle);
				self().compute_buffer_unload(sphere_handle);
				return false;
			}
			impl->gpu_cull_pipeline = loaded;
		}
		//推送常量：六平面（96 字节）＋ 球数（4 字节）
		struct Cull_Push
		{
			float planes[24];
			uint32_t count;
		} push{};
		for (uint32_t plane = 0; plane < 6; ++plane)
			for (uint32_t column = 0; column < 4; ++column)
				push.planes[plane * 4 + column] = planes.planes[plane][column];
		push.count = count;
		//绑定 → 调度 → 回读
		bool ok = self().compute_buffer_bind(sphere_handle, 0)
			&& self().compute_buffer_bind(mask_handle, 1)
			&& self().compute_dispatch(impl->gpu_cull_pipeline, count, 256, &push, static_cast<uint32_t>(sizeof(push)))
			&& self().compute_buffer_read(mask_handle, mask_out, count * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		self().compute_buffer_unload(mask_handle);
		self().compute_buffer_unload(sphere_handle);
		return ok;
	}

	//GPU 剔除开关（第二百四十九轮）
	void Renderer_Compute::gpu_cull_enable_set(bool enabled)
	{
		//未初始化时静默忽略
		if (impl == nullptr)
			return;
		impl->gpu_cull_enabled = enabled;
	}

	//读取 GPU 剔除开关
	bool Renderer_Compute::gpu_cull_enable_get(void) const
	{
		return impl != nullptr && impl->gpu_cull_enabled;
	}

	/*
	Hi-Z 开关设置（第二百五十三轮）
	开启时惰性构建「每帧槽深度历史 ＋ mip 金字塔」；能力不足或构建失败返回假且开关保持关闭。
	关闭时只置标志、保留已建资源，下一次开启直接复用。
	*/
	bool Renderer_Compute::hiz_enable_set(bool enabled)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//关闭只置标志
		if (!enabled)
		{
			impl->hiz_enabled = false;
			return true;
		}
		//已就绪直接开启
		if (impl->hiz.valid())
		{
			impl->hiz_enabled = true;
			return true;
		}
		//惰性构建资源
		if (!impl->hiz_ensure())
			return false;
		/*
		懒加载两条生成管线（第二百五十四轮）
		深度拷贝与逐级降采样；任一加载失败即保持关闭（避免「启用但生成被跳过」的假开启）。
		*/
		if (impl->hiz_depth_copy_pipeline == 0
			&& !self().compute_pipeline_load(impl->hiz_depth_copy_pipeline, "HiZ深度拷贝.comp.spv"))
		{
			logger.warn("渲染器:: Hi-Z 深度拷贝管线加载失败（开关保持关闭）");
			return false;
		}
		if (impl->hiz_downsample_pipeline == 0
			&& !self().compute_pipeline_load(impl->hiz_downsample_pipeline, "HiZ降采样.comp.spv"))
		{
			logger.warn("渲染器:: Hi-Z 降采样管线加载失败（开关保持关闭）");
			return false;
		}
		impl->hiz_enabled = true;
		return true;
	}

	//读取 Hi-Z 开关
	bool Renderer_Compute::hiz_enable_get(void) const
	{
		return impl != nullptr && impl->hiz_enabled;
	}

	//本机是否具备 Hi-Z 能力（计算路径 ＋ R32 浮点存储图像）
	bool Renderer_Compute::hiz_supported(void) const
	{
		//未初始化或能力不足
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->hiz_capable && impl->compute.supported();
	}

	//当前 Hi-Z 资源状态（未创建时全 0）
	bool Renderer_Compute::hiz_state_get(uint32_t& width_out, uint32_t& height_out, uint32_t& level_count_out) const
	{
		//出参先归零
		width_out = 0;
		height_out = 0;
		level_count_out = 0;
		//资源未创建
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		width_out = impl->hiz.width_get();
		height_out = impl->hiz.height_get();
		level_count_out = impl->hiz.level_count();
		return true;
	}

	//指定金字塔级尺寸（越界或未创建返回假）
	bool Renderer_Compute::hiz_level_extent(uint32_t level, uint32_t& width_out, uint32_t& height_out) const
	{
		//出参先归零
		width_out = 0;
		height_out = 0;
		//资源未创建
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		return impl->hiz.level_extent_get(level, width_out, height_out);
	}

	//累计金字塔生成次数（第二百五十四轮）
	uint64_t Renderer_Compute::hiz_generation_count(void) const
	{
		//未初始化或无资源
		if (impl == nullptr || !impl->hiz.valid())
			return 0;
		return impl->hiz.generation_count();
	}

	//指定帧槽是否已生成过有效金字塔（第二百五十四轮）
	bool Renderer_Compute::hiz_history_ready(uint32_t slot) const
	{
		//未初始化或无资源
		if (impl == nullptr || !impl->hiz.valid())
			return false;
		return impl->hiz.history_valid(slot);
	}

	/*
	HiZ 深度历史采样句柄（第三百六十五轮）
	历史视图与最近邻采样器以 uint64_t 原生句柄输出（门面公共头不引入 Vulkan 类型）；
	历史在生成结束时已转为 SHADER_READ_ONLY，可直接作为 sampler2D 绑定。
	*/
	bool Renderer_Compute::hiz_history_sample_get(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const
	{
		//默认无有效句柄
		view_out = 0;
		sampler_out = 0;
		//未初始化 / 未启用 / 资源未建 / 该槽无历史
		if (impl == nullptr || !impl->initialized || !impl->hiz_enabled || !impl->hiz.valid()
			|| !impl->hiz.history_valid(slot))
			return false;
		VkImageView history_view = impl->hiz.depth_history_view(slot);
		VkSampler depth_sampler = impl->hiz.depth_sampler_get();
		//句柄齐备才算成功
		if (history_view == VK_NULL_HANDLE || depth_sampler == VK_NULL_HANDLE)
			return false;
		view_out = reinterpret_cast<uint64_t>(history_view);
		sampler_out = reinterpret_cast<uint64_t>(depth_sampler);
		return true;
	}


	/*
	Cluster 光照开关设置（第二百六十六轮）
	开启时惰性构建每帧槽光源记录 SSBO；能力不足或构建失败返回假且开关保持关闭；关闭只置标志并保留资源。
	*/
	bool Renderer_Compute::cluster_enable_set(bool enabled)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//关闭只置标志并清空观测值
		if (!enabled)
		{
			impl->cluster_enabled = false;
			impl->last_cluster_light_count = 0;
			return true;
		}
		//惰性构建资源
		if (!impl->cluster_ensure())
			return false;
		impl->cluster_enabled = true;
		return true;
	}

	//读取 Cluster 光照开关
	bool Renderer_Compute::cluster_enable_get(void) const
	{
		return impl != nullptr && impl->cluster_enabled;
	}

	//本机是否具备 Cluster 能力（计算路径可用）
	bool Renderer_Compute::cluster_supported(void) const
	{
		//未初始化或能力不足
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->cluster_capable && impl->compute.supported();
	}

	//最近一帧上传的光源记录数（关闭或未出帧时为 0）
	uint32_t Renderer_Compute::cluster_light_count(void) const
	{
		return impl != nullptr ? impl->last_cluster_light_count : 0;
	}

	//分簇网格设置（第二百六十七轮；逐轴钳到 1..64 且总簇数 ≤ 4096）
	bool Renderer_Compute::cluster_grid_set(uint32_t x, uint32_t y, uint32_t z)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->cluster_grid = cluster_grid_clamp(Cluster_Grid{ x, y, z });
		return true;
	}

	//读入当前分簇网格（未初始化时全 0）
	bool Renderer_Compute::cluster_grid_get(uint32_t& x_out, uint32_t& y_out, uint32_t& z_out) const
	{
		//出参先归零
		x_out = 0;
		y_out = 0;
		z_out = 0;
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		x_out = impl->cluster_grid.x;
		y_out = impl->cluster_grid.y;
		z_out = impl->cluster_grid.z;
		return true;
	}

	//当前网格总簇数（未初始化为 0）
	uint32_t Renderer_Compute::cluster_grid_cluster_count(void) const
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return cluster_grid_clusters(impl->cluster_grid);
	}

	/*
	当前相机与光源的逐簇计数（CPU 参考；第二百六十七轮）
	口径与 GPU 计数（268 轮）一致：方向光照亮全部簇，点 / 聚光按包围球保守覆盖。
	*/
	bool Renderer_Compute::cluster_counts_reference_ex(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out) const
	{
		//出参先归零
		if (total_out != nullptr)
			*total_out = 0;
		//未初始化或出参为空
		if (impl == nullptr || !impl->initialized || counts_out == nullptr)
			return false;
		//分簇相机输入（当前相机与近远平面）
		Cluster_Input input{};
		impl->camera.view_proj_get(input.view_proj);
		impl->camera.position_get(input.camera_position);
		impl->camera.fov_get(&input.fov);
		input.aspect = impl->camera.aspect_effective_get();
		impl->camera.clip_get(&input.near_plane, &input.far_plane);
		//光源记录快照（容量上限内全部导出）
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = self().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		//调用 CPU 参考实现
		uint32_t total = cluster_counts_reference(counts_out, capacity, impl->cluster_grid,
			records.data(), count, input);
		//容量不足或入参非法
		if (total == 0)
			return false;
		if (total_out != nullptr)
			*total_out = total;
		return true;
	}

	/*
	GPU 逐簇计数（第二百六十八轮）
	光源记录与计数缓冲走计算存储缓冲；着色器与 CPU 参考同口径（方向光全簇、点光聚光保守覆盖）。
	*/
	bool Renderer_Compute::cluster_counts_gpu(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out)
	{
		//出参先归零
		if (total_out != nullptr)
			*total_out = 0;
		//未初始化、不支持计算或出参为空
		if (impl == nullptr || !impl->initialized || !self().compute_supported() || counts_out == nullptr)
			return false;
		//网格总簇数
		uint32_t total = cluster_grid_clusters(impl->cluster_grid);
		if (total == 0)
			return false;
		//容量不足
		if (capacity < total)
			return false;
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = self().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		if (count == 0)
			return false;
		//光源记录缓冲（只读）与计数缓冲（零初始化）
		Compute_Buffer_Handle light_handle = 0;
		Compute_Buffer_Handle count_handle = 0;
		if (!self().compute_buffer_load(light_handle, count * LIGHT_RECORD_BYTES, records.data()))
			return false;
		std::vector<uint32_t> zero(total, 0u);
		if (!self().compute_buffer_load(count_handle, total * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			self().compute_buffer_unload(light_handle);
			return false;
		}
		//计数管线懒加载
		if (impl->cluster_count_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster计数.comp.spv"))
			{
				self().compute_buffer_unload(count_handle);
				self().compute_buffer_unload(light_handle);
				return false;
			}
			impl->cluster_count_pipeline = loaded;
		}
		//推送常量：视图投影（64）＋ 网格与记录数（16）＋ 相机位置（16）＋ 参数（16）
		struct Cluster_Push
		{
			float view_proj[16];
			uint32_t grid[4];
			float camera_position[4];
			float params[4];
		} push{};
		impl->camera.view_proj_get(push.view_proj);
		push.grid[0] = impl->cluster_grid.x;
		push.grid[1] = impl->cluster_grid.y;
		push.grid[2] = impl->cluster_grid.z;
		push.grid[3] = count;
		impl->camera.position_get(push.camera_position);
		impl->camera.fov_get(&push.params[0]);
		push.params[1] = impl->camera.aspect_effective_get();
		impl->camera.clip_get(&push.params[2], &push.params[3]);
		//绑定 → 调度（每记录一线程） → 回读逐簇计数
		bool ok = self().compute_buffer_bind(light_handle, 0)
			&& self().compute_buffer_bind(count_handle, 1)
			&& self().compute_dispatch(impl->cluster_count_pipeline, count, 64, &push, static_cast<uint32_t>(sizeof(push)))
			&& self().compute_buffer_read(count_handle, counts_out, total * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		self().compute_buffer_unload(count_handle);
		self().compute_buffer_unload(light_handle);
		//写出总簇数
		if (ok && total_out != nullptr)
			*total_out = total;
		return ok;
	}

	//块内前缀和（CPU 参考；第二百六十九轮）
	bool Renderer_Compute::cluster_prefix_block_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
		uint32_t* block_totals_out, uint32_t block_capacity, uint32_t* block_count_out) const
	{
		//出参先归零
		if (block_count_out != nullptr)
			*block_count_out = 0;
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		uint32_t blocks = cluster_prefix_block_scan(prefix_out, block_totals_out, block_capacity, counts, count);
		//入参非法或容量不足
		if (blocks == 0)
			return false;
		if (block_count_out != nullptr)
			*block_count_out = blocks;
		return true;
	}

	/*
	块内前缀和（GPU；第二百六十九轮）
	与 CPU 参考同口径：256 元素一块，块内包含式扫描并写出每块总和。
	*/
	bool Renderer_Compute::cluster_prefix_block_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
		uint32_t* block_totals_out, uint32_t block_count_capacity, uint32_t* block_count_out)
	{
		//出参先归零
		if (block_count_out != nullptr)
			*block_count_out = 0;
		//未初始化、不支持计算或入参非法
		if (impl == nullptr || !impl->initialized || !self().compute_supported() || counts == nullptr
			|| prefix_out == nullptr || block_totals_out == nullptr || count == 0)
			return false;
		//块数与容量
		uint32_t blocks = (count + CLUSTER_SCAN_BLOCK - 1) / CLUSTER_SCAN_BLOCK;
		if (block_count_capacity < blocks)
			return false;
		//输入 / 前缀输出 / 块总和缓冲
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle prefix_handle = 0;
		Compute_Buffer_Handle totals_handle = 0;
		if (!self().compute_buffer_load(counts_handle, count * static_cast<uint32_t>(sizeof(uint32_t)), counts))
			return false;
		if (!self().compute_buffer_load(prefix_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			self().compute_buffer_unload(counts_handle);
			return false;
		}
		std::vector<uint32_t> zero(blocks, 0u);
		if (!self().compute_buffer_load(totals_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			self().compute_buffer_unload(prefix_handle);
			self().compute_buffer_unload(counts_handle);
			return false;
		}
		//扫描管线懒加载
		if (impl->cluster_scan_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster块内扫描.comp.spv"))
			{
				self().compute_buffer_unload(totals_handle);
				self().compute_buffer_unload(prefix_handle);
				self().compute_buffer_unload(counts_handle);
				return false;
			}
			impl->cluster_scan_pipeline = loaded;
		}
		//推送常量：元素总数
		uint32_t push_count = count;
		//绑定 → 调度（每块一个工作组） → 回读前缀与块总和
		bool ok = self().compute_buffer_bind(counts_handle, 0)
			&& self().compute_buffer_bind(prefix_handle, 1)
			&& self().compute_buffer_bind(totals_handle, 2)
			&& self().compute_dispatch(impl->cluster_scan_pipeline, count, CLUSTER_SCAN_BLOCK,
				&push_count, static_cast<uint32_t>(sizeof(push_count)))
			&& self().compute_buffer_read(prefix_handle, prefix_out,
				count * static_cast<uint32_t>(sizeof(uint32_t)), 0)
			&& self().compute_buffer_read(totals_handle, block_totals_out,
				blocks * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		self().compute_buffer_unload(totals_handle);
		self().compute_buffer_unload(prefix_handle);
		self().compute_buffer_unload(counts_handle);
		//写出块数
		if (ok && block_count_out != nullptr)
			*block_count_out = blocks;
		return ok;
	}

	//全量前缀和（CPU 参考；第二百七十轮）
	bool Renderer_Compute::cluster_prefix_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out) const
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return cluster_prefix_scan(prefix_out, counts, count) != 0;
	}

	/*
	全量前缀和（GPU；第二百七十轮）
	块内扫描 → 块总和排他偏移 → 逐元素叠加偏移，结果与 CPU 参考同口径。
	*/
	bool Renderer_Compute::cluster_prefix_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out)
	{
		//未初始化、不支持计算或入参非法
		if (impl == nullptr || !impl->initialized || !self().compute_supported() || counts == nullptr
			|| prefix_out == nullptr || count == 0)
			return false;
		//块数
		uint32_t blocks = (count + CLUSTER_SCAN_BLOCK - 1) / CLUSTER_SCAN_BLOCK;
		//计数 / 前缀 / 块总和 / 块偏移四缓冲
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle prefix_handle = 0;
		Compute_Buffer_Handle totals_handle = 0;
		Compute_Buffer_Handle offsets_handle = 0;
		auto cleanup = [&](void)
		{
			//逆序归还已建缓冲（空句柄由卸载函数幂等处理）
			self().compute_buffer_unload(offsets_handle);
			self().compute_buffer_unload(totals_handle);
			self().compute_buffer_unload(prefix_handle);
			self().compute_buffer_unload(counts_handle);
		};
		if (!self().compute_buffer_load(counts_handle, count * static_cast<uint32_t>(sizeof(uint32_t)), counts))
			return false;
		if (!self().compute_buffer_load(prefix_handle, count * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			cleanup();
			return false;
		}
		std::vector<uint32_t> zero(blocks, 0u);
		if (!self().compute_buffer_load(totals_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data())
			|| !self().compute_buffer_load(offsets_handle, blocks * static_cast<uint32_t>(sizeof(uint32_t)), zero.data()))
		{
			cleanup();
			return false;
		}
		//三条管线懒加载（块内扫描 / 块间偏移 / 偏移合成）
		if (impl->cluster_scan_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster块内扫描.comp.spv"))
			{
				cleanup();
				return false;
			}
			impl->cluster_scan_pipeline = loaded;
		}
		if (impl->cluster_block_offset_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster块间扫描.comp.spv"))
			{
				cleanup();
				return false;
			}
			impl->cluster_block_offset_pipeline = loaded;
		}
		if (impl->cluster_offset_apply_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster块偏移合成.comp.spv"))
			{
				cleanup();
				return false;
			}
			impl->cluster_offset_apply_pipeline = loaded;
		}
		//第一步：块内扫描（计数 → 块内前缀 ＋ 块总和）
		uint32_t push_count = count;
		bool ok = self().compute_buffer_bind(counts_handle, 0)
			&& self().compute_buffer_bind(prefix_handle, 1)
			&& self().compute_buffer_bind(totals_handle, 2)
			&& self().compute_dispatch(impl->cluster_scan_pipeline, count, CLUSTER_SCAN_BLOCK,
				&push_count, static_cast<uint32_t>(sizeof(push_count)));
		//第二步：块总和串行扫描 → 块排他偏移（单线程）
		uint32_t push_blocks = blocks;
		if (ok)
			ok = self().compute_buffer_bind(totals_handle, 0)
				&& self().compute_buffer_bind(offsets_handle, 1)
				&& self().compute_dispatch(impl->cluster_block_offset_pipeline, 1, 1,
					&push_blocks, static_cast<uint32_t>(sizeof(push_blocks)));
		//第三步：逐元素叠加块偏移
		if (ok)
			ok = self().compute_buffer_bind(prefix_handle, 0)
				&& self().compute_buffer_bind(offsets_handle, 1)
				&& self().compute_dispatch(impl->cluster_offset_apply_pipeline, count, CLUSTER_SCAN_BLOCK,
					&push_count, static_cast<uint32_t>(sizeof(push_count)));
		//回读全量前缀和
		if (ok)
			ok = self().compute_buffer_read(prefix_handle, prefix_out,
				count * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		cleanup();
		return ok;
	}

	//簇光源列表（CPU 参考；第二百七十一轮）
	bool Renderer_Compute::cluster_list_build_cpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
		const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out) const
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化或入参非法
		if (impl == nullptr || !impl->initialized || list_out == nullptr || offsets_out == nullptr
			|| counts == nullptr || cluster_total == 0)
			return false;
		//排他偏移与总条目
		uint32_t entries = cluster_list_offsets(counts, cluster_total, offsets_out);
		if (entries == 0 || capacity < entries)
			return false;
		//写入游标初值（复制排他偏移，参考实现会推进）
		std::vector<uint32_t> cursor(cluster_total);
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
			cursor[cluster] = offsets_out[cluster];
		//分簇相机输入
		Cluster_Input input{};
		impl->camera.view_proj_get(input.view_proj);
		impl->camera.position_get(input.camera_position);
		impl->camera.fov_get(&input.fov);
		input.aspect = impl->camera.aspect_effective_get();
		impl->camera.clip_get(&input.near_plane, &input.far_plane);
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t count = self().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		//调用 CPU 参考
		uint32_t written = cluster_list_reference(list_out, capacity, cursor.data(), counts, cluster_total,
			impl->cluster_grid, records.data(), count, input);
		if (written == 0)
			return false;
		if (entries_out != nullptr)
			*entries_out = written;
		return true;
	}

	/*
	簇光源列表（GPU；第二百七十一轮）
	计数 / 排他偏移 / 光源记录 / 列表四缓冲；每记录一线程，按覆盖范围以原子游标写索引。
	*/
	bool Renderer_Compute::cluster_list_build_gpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
		const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out)
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化、不支持计算或入参非法
		if (impl == nullptr || !impl->initialized || !self().compute_supported() || list_out == nullptr
			|| offsets_out == nullptr || counts == nullptr || cluster_total == 0)
			return false;
		//总条目与容量
		uint32_t entries = 0;
		for (uint32_t cluster = 0; cluster < cluster_total; ++cluster)
			entries += counts[cluster];
		if (entries == 0 || capacity < entries)
			return false;
		//排他偏移
		for (uint32_t cluster = 0, running = 0; cluster < cluster_total; ++cluster)
		{
			offsets_out[cluster] = running;
			running += counts[cluster];
		}
		//光源记录快照
		std::vector<Light_Record> records(LIGHT_RECORD_CAPACITY);
		uint32_t light_count = self().light_records_get(records.data(), LIGHT_RECORD_CAPACITY);
		if (light_count == 0)
			return false;
		//四缓冲：光源记录 / 计数 / 游标（排他偏移）/ 列表
		Compute_Buffer_Handle light_handle = 0;
		Compute_Buffer_Handle counts_handle = 0;
		Compute_Buffer_Handle cursor_handle = 0;
		Compute_Buffer_Handle list_handle = 0;
		auto cleanup = [&](void)
		{
			self().compute_buffer_unload(list_handle);
			self().compute_buffer_unload(cursor_handle);
			self().compute_buffer_unload(counts_handle);
			self().compute_buffer_unload(light_handle);
		};
		if (!self().compute_buffer_load(light_handle, light_count * LIGHT_RECORD_BYTES, records.data()))
			return false;
		if (!self().compute_buffer_load(counts_handle, cluster_total * static_cast<uint32_t>(sizeof(uint32_t)), counts))
		{
			cleanup();
			return false;
		}
		if (!self().compute_buffer_load(cursor_handle, cluster_total * static_cast<uint32_t>(sizeof(uint32_t)), offsets_out))
		{
			cleanup();
			return false;
		}
		if (!self().compute_buffer_load(list_handle, entries * static_cast<uint32_t>(sizeof(uint32_t))))
		{
			cleanup();
			return false;
		}
		//列表写入管线懒加载
		if (impl->cluster_list_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "Cluster列表写入.comp.spv"))
			{
				cleanup();
				return false;
			}
			impl->cluster_list_pipeline = loaded;
		}
		//推送常量：视图投影 ＋ 网格 ＋ 相机位置 ＋ 参数 ＋ 光源记录数（116 字节）
		struct Cluster_List_Push
		{
			float view_proj[16];
			uint32_t grid[4];
			float camera_position[4];
			float params[4];
			uint32_t light_count;
		} push{};
		impl->camera.view_proj_get(push.view_proj);
		push.grid[0] = impl->cluster_grid.x;
		push.grid[1] = impl->cluster_grid.y;
		push.grid[2] = impl->cluster_grid.z;
		push.grid[3] = cluster_total;
		impl->camera.position_get(push.camera_position);
		impl->camera.fov_get(&push.params[0]);
		push.params[1] = impl->camera.aspect_effective_get();
		impl->camera.clip_get(&push.params[2], &push.params[3]);
		push.light_count = light_count;
		//绑定 → 调度（每记录一线程） → 回读列表
		bool ok = self().compute_buffer_bind(light_handle, 0)
			&& self().compute_buffer_bind(counts_handle, 1)
			&& self().compute_buffer_bind(cursor_handle, 2)
			&& self().compute_buffer_bind(list_handle, 3)
			&& self().compute_dispatch(impl->cluster_list_pipeline, light_count, 64, &push,
				static_cast<uint32_t>(sizeof(push)))
			&& self().compute_buffer_read(list_handle, list_out,
				entries * static_cast<uint32_t>(sizeof(uint32_t)), 0);
		cleanup();
		if (ok && entries_out != nullptr)
			*entries_out = entries;
		return ok;
	}

	/*
	簇数据整备（第二百七十二轮）
	一次调用完成「逐簇计数 → 排他偏移 → 光源列表」，并记录构建耗时与条目数。
	*/
	bool Renderer_Compute::cluster_data_build(uint32_t* counts_out, uint32_t* offsets_out, uint32_t* list_out,
		uint32_t cluster_capacity, uint32_t list_capacity, uint32_t* entries_out, bool use_gpu)
	{
		//出参先归零
		if (entries_out != nullptr)
			*entries_out = 0;
		//未初始化或入参非法
		if (impl == nullptr || !impl->initialized || counts_out == nullptr || offsets_out == nullptr
			|| list_out == nullptr)
			return false;
		//网格总簇数与容量
		uint32_t total = cluster_grid_clusters(impl->cluster_grid);
		if (total == 0 || cluster_capacity < total)
			return false;
		//计时起点
		const auto begin = std::chrono::high_resolution_clock::now();
		//计数（GPU 可用且请求时走 GPU，否则回落 CPU 参考）
		bool gpu_path = use_gpu && self().compute_supported();
		uint32_t counted = 0;
		if (gpu_path)
			gpu_path = self().cluster_counts_gpu(counts_out, cluster_capacity, &counted);
		if (!gpu_path && !self().cluster_counts_reference_ex(counts_out, cluster_capacity, &counted))
			return false;
		//条目总数
		uint32_t entries = 0;
		for (uint32_t cluster = 0; cluster < total; ++cluster)
			entries += counts_out[cluster];
		if (entries == 0)
			return false;
		//列表（与计数采用同一路径）
		uint32_t built_entries = 0;
		bool ok = gpu_path
			? self().cluster_list_build_gpu(list_out, list_capacity, offsets_out, counts_out, total, &built_entries)
			: self().cluster_list_build_cpu(list_out, list_capacity, offsets_out, counts_out, total, &built_entries);
		if (!ok || built_entries != entries)
			return false;
		//计时终点与观测更新
		const auto end = std::chrono::high_resolution_clock::now();
		impl->last_cluster_entries = entries;
		impl->last_cluster_build_ms = std::chrono::duration<double, std::milli>(end - begin).count();
		if (entries_out != nullptr)
			*entries_out = entries;
		return true;
	}

	//最近一次簇数据整备的条目数（未构建时为 0）
	uint32_t Renderer_Compute::cluster_entries_get(void) const
	{
		return impl != nullptr ? impl->last_cluster_entries : 0;
	}

	//最近一次簇数据整备耗时（毫秒；未构建时为 0）
	double Renderer_Compute::cluster_build_ms_get(void) const
	{
		return impl != nullptr ? impl->last_cluster_build_ms : 0.0;
	}


	//GPU 实例压缩（第二百四十九轮；不限距离、背面关）
	bool Renderer_Compute::gpu_instance_compact(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], float* matrices_out, uint32_t* visible_out)
	{
		//无距离上限、背面关
		const float zero_camera[3] = { 0.0f, 0.0f, 0.0f };
		const float zero_forward[3] = { 0.0f, 0.0f, 0.0f };
		return self().gpu_instance_compact_ex(transforms, count, local_min, local_max, zero_camera, zero_forward,
			0.0f, false, matrices_out, visible_out);
	}

	//GPU 实例压缩（带距离 / 背面剔除；第二百五十轮）
	bool Renderer_Compute::gpu_instance_compact_ex(const float* transforms, uint32_t count,
		const float local_min[3], const float local_max[3], const float camera[3], const float forward[3],
		float max_distance, bool backface, float* matrices_out, uint32_t* visible_out)
	{
		//未初始化或不支持计算
		if (impl == nullptr || !impl->initialized || !self().compute_supported())
			return false;
		//入参非法
		if (transforms == nullptr || matrices_out == nullptr || visible_out == nullptr || count == 0
			|| local_min == nullptr || local_max == nullptr || camera == nullptr || forward == nullptr)
			return false;
		*visible_out = 0;
		//当前视图 × 投影（与 CPU 剔除同一矩阵）
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		//四个缓冲：源矩阵 / 压缩输出 / 原子计数 / 剔除参数
		const uint32_t matrix_bytes = count * static_cast<uint32_t>(sizeof(float)) * 16u;
		Compute_Buffer_Handle transform_handle = 0;
		Compute_Buffer_Handle compact_handle = 0;
		Compute_Buffer_Handle counter_handle = 0;
		Compute_Buffer_Handle params_handle = 0;
		if (!self().compute_buffer_load(transform_handle, matrix_bytes, transforms))
			return false;
		if (!self().compute_buffer_load(compact_handle, matrix_bytes))
		{
			self().compute_buffer_unload(transform_handle);
			return false;
		}
		//计数清零（主机可见初值 0；第 0 个为可见数、第 1 个为遮挡剔除数）
		const uint32_t zero[2] = { 0u, 0u };
		if (!self().compute_buffer_load(counter_handle, static_cast<uint32_t>(sizeof(zero)), zero))
		{
			self().compute_buffer_unload(compact_handle);
			self().compute_buffer_unload(transform_handle);
			return false;
		}
		//剔除参数（相机位置 / 前向 / 阈值与背面开关 ＋ Hi-Z 遮挡参数）
		struct Cull_Params
		{
			float camera_position[4];
			float camera_forward[4];
			float limits[4];
			//Hi-Z：(第 0 级宽, 第 0 级高, 级数, 是否启用)
			float hi_z[4];
		} params{};
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			params.camera_position[axis] = camera[axis];
			params.camera_forward[axis] = forward[axis];
		}
		params.limits[0] = max_distance;
		params.limits[1] = backface ? 1.0f : 0.0f;
		/*
		Hi-Z 遮挡启用判定（第二百五十六轮）
		需开关开启、资源就绪、最近生成槽的金字塔有效且完整链视图非空；
		首帧（尚无生成）自动跳过，保持与引入前逐字节一致。
		*/
		const bool occlusion_enabled = impl->hiz_enabled && impl->hiz.valid()
			&& impl->hiz.history_valid(impl->last_hiz_slot)
			&& impl->hiz.pyramid_full_view(impl->last_hiz_slot) != VK_NULL_HANDLE
			&& impl->hiz.depth_sampler_get() != VK_NULL_HANDLE;
		if (occlusion_enabled)
		{
			params.hi_z[0] = static_cast<float>(impl->hiz.width_get());
			params.hi_z[1] = static_cast<float>(impl->hiz.height_get());
			params.hi_z[2] = static_cast<float>(impl->hiz.level_count());
			params.hi_z[3] = 1.0f;
		}
		//遮挡开启时把计算帧槽切到「最近生成槽」并把金字塔绑到采样器位
		if (occlusion_enabled)
		{
			impl->compute.slot_set(impl->last_hiz_slot);
			impl->compute.sampler_bind(impl->last_hiz_slot, impl->hiz.depth_sampler_get(),
				impl->hiz.pyramid_full_view(impl->last_hiz_slot));
		}
		if (!self().compute_buffer_load(params_handle, static_cast<uint32_t>(sizeof(params)), &params))
		{
			self().compute_buffer_unload(counter_handle);
			self().compute_buffer_unload(compact_handle);
			self().compute_buffer_unload(transform_handle);
			return false;
		}
		//压缩管线懒加载
		if (impl->gpu_compact_pipeline == 0)
		{
			Compute_Pipeline_Handle loaded = 0;
			if (!self().compute_pipeline_load(loaded, "实例压缩.comp.spv"))
			{
				self().compute_buffer_unload(params_handle);
				self().compute_buffer_unload(counter_handle);
				self().compute_buffer_unload(compact_handle);
				self().compute_buffer_unload(transform_handle);
				return false;
			}
			impl->gpu_compact_pipeline = loaded;
		}
		//推送常量：视图投影（64）＋ 局部界（32）＋ 实例数（4）
		struct Compact_Push
		{
			float view_proj[16];
			float bounds_min[4];
			float bounds_max[4];
			uint32_t count;
		} push{};
		for (uint32_t index = 0; index < 16; ++index)
			push.view_proj[index] = view_proj[index];
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			push.bounds_min[axis] = local_min[axis];
			push.bounds_max[axis] = local_max[axis];
		}
		push.count = count;
		//绑定 → 调度 → 回读计数与压缩矩阵（含第 3 号剔除参数缓冲）
		uint32_t occluded = 0;
		uint32_t counters[2] = { 0u, 0u };
		bool ok = self().compute_buffer_bind(transform_handle, 0)
			&& self().compute_buffer_bind(compact_handle, 1)
			&& self().compute_buffer_bind(counter_handle, 2)
			&& self().compute_buffer_bind(params_handle, 3)
			&& self().compute_dispatch(impl->gpu_compact_pipeline, count, 256, &push, static_cast<uint32_t>(sizeof(push)))
			&& self().compute_buffer_read(counter_handle, counters, static_cast<uint32_t>(sizeof(counters)), 0);
		//可见数与遮挡剔除数（后者仅在 Hi-Z 启用时非零）
		if (ok)
		{
			*visible_out = static_cast<uint32_t>(counters[0]);
			occluded = static_cast<uint32_t>(counters[1]);
			impl->last_gpu_occluded = occluded;
		}
		//读取压缩后的矩阵（仅可见前缀）
		if (ok && *visible_out > 0)
			ok = self().compute_buffer_read(compact_handle, matrices_out,
				(*visible_out) * static_cast<uint32_t>(sizeof(float)) * 16u, 0);
		//记录 GPU 剔除耗时（计算时间戳；不支持时间戳时保持 −1 语义在统计侧）
		if (ok)
		{
			impl->last_gpu_cull_ms = impl->compute.stats_get().elapsed_ms;
			//累计 GPU 派发次数（第二百五十一轮）
			++impl->gpu_cull_dispatches;
		}
		self().compute_buffer_unload(params_handle);
		self().compute_buffer_unload(counter_handle);
		self().compute_buffer_unload(compact_handle);
		self().compute_buffer_unload(transform_handle);
		return ok;
	}

	//计算管线加载（第二百四十三轮）
	bool Renderer_Compute::compute_pipeline_load(Compute_Pipeline_Handle& handle_out, const std::string& shader_name)
	{
		//失败路径一律复位句柄（与材质句柄约定一致）
		handle_out = 0;
		//未初始化或不支持计算
		if (impl == nullptr || !impl->initialized || !self().compute_supported() || shader_name.empty())
			return false;
		VkShaderModule module = VK_NULL_HANDLE;
		//加载 SPIR-V 模块
		if (!impl->shader.module_load(module, shader_name))
		{
			logger.error("渲染器:: 计算着色器加载失败：{}", shader_name);
			return false;
		}
		uint64_t context_handle = 0;
		//登记计算管线（失败则回收模块）
		if (!impl->compute.pipeline_register(context_handle, module))
		{
			logger.error("渲染器:: 计算管线登记失败：{}", shader_name);
			impl->shader.module_destroy(module);
			return false;
		}
		Renderer_Impl_Holder::Impl::Compute_Pipeline_Record record;
		record.handle = impl->next_compute_pipeline_handle++;
		record.context_handle = context_handle;
		record.module = module;
		handle_out = record.handle;
		impl->compute_pipelines.push_back(record);
		return true;
	}

	//计算管线卸载（第二百四十三轮）
	void Renderer_Compute::compute_pipeline_unload(Compute_Pipeline_Handle handle)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		for (std::size_t index = 0; index < impl->compute_pipelines.size(); ++index)
		{
			//命中即销毁管线与模块并移出列表
			if (impl->compute_pipelines[index].handle == handle)
			{
				impl->compute.pipeline_release(impl->compute_pipelines[index].context_handle);
				if (impl->compute_pipelines[index].module != VK_NULL_HANDLE)
					impl->shader.module_destroy(impl->compute_pipelines[index].module);
				impl->compute_pipelines.erase(impl->compute_pipelines.begin() + static_cast<std::ptrdiff_t>(index));
				return;
			}
		}
	}

	//把存储缓冲绑定到计算集指定 binding（第二百四十三轮）
	bool Renderer_Compute::compute_buffer_bind(Compute_Buffer_Handle buffer, uint32_t binding)
	{
		//未初始化或 binding 越界
		if (impl == nullptr || !impl->initialized || binding >= COMPUTE_BINDING_MAX)
			return false;
		for (Renderer_Impl_Holder::Impl::Compute_Buffer_Record& record : impl->compute_buffers)
		{
			//命中缓冲即写入描述符（整段绑定）
			if (record.handle == buffer)
				return impl->compute.storage_bind(impl->compute.slot_get(), binding, record.buffer, 0, record.size);
		}
		return false;
	}

	//计算同步调度（第二百四十三轮）
	bool Renderer_Compute::compute_dispatch(Compute_Pipeline_Handle pipeline, uint32_t elements, uint32_t local_size,
		const void* push_data, uint32_t push_size)
	{
		//未初始化或不支持计算
		if (impl == nullptr || !impl->initialized || !self().compute_supported())
			return false;
		//空元素数视为无需调度
		if (elements == 0)
			return true;
		//未指定组大小
		if (local_size == 0)
			return false;
		uint64_t context_handle = 0;
		for (Renderer_Impl_Holder::Impl::Compute_Pipeline_Record& record : impl->compute_pipelines)
		{
			//命中门面句柄
			if (record.handle == pipeline)
			{
				context_handle = record.context_handle;
				break;
			}
		}
		//句柄无效
		if (context_handle == 0)
			return false;
		//一次性提交：起时间戳 → 调度 → 计算→主机屏障 → 止时间戳（返回即可读回）
		bool dispatched = impl->compute.single_submit([&](VkCommandBuffer command_buffer) -> bool
		{
			if (!impl->compute.timestamp_begin(command_buffer))
				return false;
			if (!impl->compute.dispatch_ceil(command_buffer, context_handle, elements, local_size, push_data, push_size))
				return false;
			if (!impl->compute.barrier(command_buffer, Compute_Barrier::ComputeToHost))
				return false;
			return impl->compute.timestamp_end(command_buffer);
		});
		//围栏等待之后读回计时（不支持时间戳时为空操作）
		if (dispatched)
			impl->compute.timestamp_refresh();
		return dispatched;
	}

}
