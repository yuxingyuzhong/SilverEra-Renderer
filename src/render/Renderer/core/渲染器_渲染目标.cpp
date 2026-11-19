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
	//创建离屏渲染目标

	bool Render_Resource::render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 创建渲染目标失败：尚未初始化");
			return false;
		}
		//颜色格式：开启「离屏目标默认 16F」时改用 16F，否则取当前交换链格式
		VkFormat color_format = owner->device().hdr_targets()
			? VK_FORMAT_R16G16B16A16_SFLOAT : core->swapchain.format_get();
		//转交渲染目标模块
		return (*state_ptr).render_target.target_create(handle_out, width, height,
			color_format, core->swapchain.depth_format_get());
	}

	bool Render_Resource::render_target_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//转交渲染目标模块取尺寸
		VkExtent2D extent{};
		if (!(*state_ptr).render_target.target_extent_get(handle, extent))
			return false;
		if (width_out != nullptr)
			*width_out = extent.width;
		if (height_out != nullptr)
			*height_out = extent.height;
		return true;
	}

	bool Render_Resource::render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!core->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回）
		return (*state_ptr).render_target.target_readback(handle, pixels, capacity, width_out, height_out);
	}

	bool Render_Resource::render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!core->frame.frames_wait())
			return false;
		//转交渲染目标模块（子区域同步拷贝）
		return (*state_ptr).render_target.target_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	bool Render_Resource::render_target_readback_float(uint64_t handle, float* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!core->frame.frames_wait())
			return false;
		//转交渲染目标模块（浮点转换）
		return (*state_ptr).render_target.target_readback_float(handle, values, capacity, width_out, height_out);
	}

	bool Render_Resource::render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽，避免与该目标的写入冲突
		if (!core->frame.frames_wait())
			return false;
		//转交渲染目标模块（深度同步拷贝）
		return (*state_ptr).render_target.target_readback_depth(handle, values, capacity, width_out, height_out);
	}

	bool Render_Resource::render_target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽，避免与该目标的写入冲突
		if (!core->frame.frames_wait())
			return false;
		//转交渲染目标模块（深度子区域同步拷贝）
		return (*state_ptr).render_target.target_readback_depth_region(handle, x, y, region_width, region_height,
			values, capacity, width_out, height_out);
	}

	void Render_Resource::render_target_destroy(uint64_t handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//先解除设备帧路径对目标的全部引用
		owner->device().render_target_references_release(handle);
		//摘出颜色 / 深度图像与视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Render_Target_Record record{};
		if (!(*state_ptr).render_target.target_detach(handle, record))
			return;
		//组装延迟条目（颜色与深度各一条内存资源）
		Deferred_Entry entry{};
		Deferred_Memory_Resource color{};
		color.image = record.color_image;
		color.image_allocation = record.color_alloc;
		color.view = record.color_view;
		entry.memory_resources.push_back(color);
		Deferred_Memory_Resource depth{};
		depth.image = record.depth_image;
		depth.image_allocation = record.depth_alloc;
		depth.view = record.depth_view;
		entry.memory_resources.push_back(depth);
		//MSAA 颜色 / 深度附件（第九十六轮；未启用时句柄为空，释放时自动跳过）
		Deferred_Memory_Resource msaa_color{};
		msaa_color.image = record.msaa_color_image;
		msaa_color.image_allocation = record.msaa_color_alloc;
		msaa_color.view = record.msaa_color_view;
		entry.memory_resources.push_back(msaa_color);
		Deferred_Memory_Resource msaa_depth{};
		msaa_depth.image = record.msaa_depth_image;
		msaa_depth.image_allocation = record.msaa_depth_alloc;
		msaa_depth.view = record.msaa_depth_view;
		entry.memory_resources.push_back(msaa_depth);
		//色调编号附件（第 801 轮；未启用时句柄为空，释放时自动跳过）
		Deferred_Memory_Resource tone_id{};
		tone_id.image = record.tone_id_image;
		tone_id.image_allocation = record.tone_id_alloc;
		tone_id.view = record.tone_id_view;
		entry.memory_resources.push_back(tone_id);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		core->frame.deferred_push(std::move(entry), core->frame.in_flight_count_get() + 1);
	}

	void Render_Device::render_target_references_release(uint64_t handle)
	{
		//当前场景目标命中时回落交换链
		if (state_ptr->active_target == handle)
			state_ptr->active_target = 0;
		//同帧呈现首目标命中时清空完整目标组
		if (state_ptr->present_target == handle)
		{
			state_ptr->present_target = 0;
			state_ptr->present_extra.clear();
			return;
		}
		//额外目标命中时只摘除该级
		for (std::size_t index = 0; index < state_ptr->present_extra.size(); ++index)
		{
			if (state_ptr->present_extra[index] != handle)
				continue;
			state_ptr->present_extra.erase(state_ptr->present_extra.begin() + static_cast<std::ptrdiff_t>(index));
			break;
		}
	}

	void Render_Device::render_target_set(uint64_t handle)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//句柄非零且未登记则回落交换链路径并告警
		if (handle != 0 && !owner->resource().render_target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", handle);
			state_ptr->active_target = 0;
			return;
		}
		state_ptr->active_target = handle;
	}

	void Render_Device::render_target_present_set(uint64_t handle)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//句柄非零且未登记则关闭该路径并告警
		if (handle != 0 && !owner->resource().render_target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，关闭同帧后处理呈现路径", handle);
			state_ptr->present_target = 0;
			state_ptr->present_extra.clear();
			return;
		}
		state_ptr->present_target = handle;
		//目标组重置为「仅该目标」（多级链须改用 render_target_present_chain_set）
		state_ptr->present_extra.clear();
	}

	bool Render_Device::render_target_present_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
		{
			logger.error("渲染器:: 后处理目标组设置失败：尚未初始化");
			return false;
		}
		//若为清除目标组（关闭同帧后处理呈现路径）
		if (count == 0)
		{
			state_ptr->present_target = 0;
			state_ptr->present_extra.clear();
			logger.info("渲染器:: 后处理目标组已清除");
			return true;
		}
		//若句柄数组为空 / 数量越界则整组拒绝
		if (handles == nullptr || count > PRESENT_TARGET_MAX)
		{
			logger.error("渲染器:: 后处理目标组设置失败：句柄数组为空或数量 {} 越界（上限 {}）",
				count, PRESENT_TARGET_MAX);
			return false;
		}
		//逐个校验句柄已登记
		for (uint32_t each = 0; each < count; ++each)
		{
			//若某句柄未登记则整组拒绝（保持原目标组不变）
			if (!owner->resource().render_target_valid(handles[each]))
			{
				logger.error("渲染器:: 后处理目标组设置失败：第 {} 个句柄 {} 未登记", each, handles[each]);
				return false;
			}
		}
		//写入目标组（首项为场景目标，其余为额外中间目标）
		state_ptr->present_target = handles[0];
		state_ptr->present_extra.assign(handles + 1, handles + count);
		logger.info("渲染器:: 后处理目标组已设置（{} 个）", count);
		return true;
	}

	void Render_Resource::texture_bind_target(uint64_t handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//取目标颜色图像与视图
		VkImage color_image = VK_NULL_HANDLE;
		VkImageView color_view = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkExtent2D extent = { 0, 0 };
		//句柄无效则静默返回（保持原绑定）
		if (!(*state_ptr).render_target.target_get(handle, color_image, color_view, depth_view, extent))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，纹理绑定忽略", handle);
			return;
		}
		/*
		取共享采样器
		采样器由纹理模块持有且被烘焙进各帧槽 set1；此处借内置默认白纹理取一次采样器句柄，
		随后以「目标颜色视图 ＋ 该采样器」写入所有帧槽 set1。
		*/
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler shared_sampler = VK_NULL_HANDLE;
		if (!(*state_ptr).texture.texture_bind_get((*state_ptr).default_texture, default_view, shared_sampler))
		{
			logger.error("渲染器:: 渲染目标绑定失败：共享采样器不可用");
			return;
		}
		//记录当前绑定视图（与纹理绑定共用同一语义；采样器状态切换后据此重写 set1）
		(*state_ptr).bound_view = color_view;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		core->frame.frames_wait();
		//把目标颜色视图写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < core->config.frame_overlap; ++slot)
			(*state_ptr).pipeline.texture_bind(slot, shared_sampler, color_view);
		logger.info("渲染器:: 渲染目标已绑定为当前纹理（句柄 {}）", handle);
	}
}
