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
	//创建离屏渲染目标
	bool Renderer_Post::render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 创建渲染目标失败：尚未初始化");
			return false;
		}
		//转交渲染目标模块（颜色 / 深度格式取当前交换链格式）
		return impl->render_target.target_create(handle_out, width, height,
			impl->swapchain.format_get(), impl->swapchain.depth_format_get());
	}

	//查询离屏渲染目标尺寸
	bool Renderer_Post::render_target_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交渲染目标模块取尺寸
		VkExtent2D extent{};
		if (!impl->render_target.target_extent_get(handle, extent))
			return false;
		if (width_out != nullptr)
			*width_out = extent.width;
		if (height_out != nullptr)
			*height_out = extent.height;
		return true;
	}

	//读回离屏渲染目标的颜色内容
	bool Renderer_Post::render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回）
		return impl->render_target.target_readback(handle, pixels, capacity, width_out, height_out);
	}

	//读回离屏渲染目标的指定子矩形
	bool Renderer_Post::render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（子区域同步拷贝）
		return impl->render_target.target_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//读回离屏渲染目标的深度图像
	bool Renderer_Post::render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（深度同步拷贝）
		return impl->render_target.target_readback_depth(handle, values, capacity, width_out, height_out);
	}

	//读回离屏渲染目标的深度子区域（第一百六十三轮）
	bool Renderer_Post::render_target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该目标的写入冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（深度子区域同步拷贝）
		return impl->render_target.target_readback_depth_region(handle, x, y, region_width, region_height,
			values, capacity, width_out, height_out);
	}

	//销毁离屏渲染目标
	void Renderer_Post::render_target_destroy(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//若销毁的是当前设定的本帧渲染目标则先回落交换链路径
		if (impl->active_target == handle)
			impl->active_target = 0;
		//若销毁的是当前设定的同帧后处理呈现目标则一并回落并清空目标组
		if (impl->present_target == handle)
		{
			impl->present_target = 0;
			impl->present_extra.clear();
		}
		//若销毁的是目标组中的额外中间目标则从组内摘除（链长 ≥ 2 将因此回落单级路径）
		for (std::size_t index = 0; index < impl->present_extra.size(); ++index)
		{
			//命中即原地摘除并结束（句柄唯一）
			if (impl->present_extra[index] == handle)
			{
				impl->present_extra.erase(impl->present_extra.begin() + static_cast<std::ptrdiff_t>(index));
				break;
			}
		}
		//摘出颜色 / 深度图像与视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Render_Target_Record record{};
		if (!impl->render_target.target_detach(handle, record))
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
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//设置「本帧渲染到目标」
	void Renderer_Post::render_target_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄非零且未登记则回落交换链路径并告警
		if (handle != 0 && !impl->render_target.target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，回落交换链路径", handle);
			impl->active_target = 0;
			return;
		}
		impl->active_target = handle;
	}

	//设置「本帧渲染到目标并同帧后处理呈现到交换链」
	void Renderer_Post::render_target_present_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄非零且未登记则关闭该路径并告警
		if (handle != 0 && !impl->render_target.target_valid(handle))
		{
			logger.warn("渲染器:: 渲染目标句柄 {} 无效，关闭同帧后处理呈现路径", handle);
			impl->present_target = 0;
			impl->present_extra.clear();
			return;
		}
		impl->present_target = handle;
		//目标组重置为「仅该目标」（多级链须改用 render_target_present_chain_set）
		impl->present_extra.clear();
	}

	//设置「同帧后处理链」的中间目标组（2..4 个句柄；首项为场景目标）
	bool Renderer_Post::render_target_present_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 后处理目标组设置失败：尚未初始化");
			return false;
		}
		//若为清除目标组（关闭同帧后处理呈现路径）
		if (count == 0)
		{
			impl->present_target = 0;
			impl->present_extra.clear();
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
			if (!impl->render_target.target_valid(handles[each]))
			{
				logger.error("渲染器:: 后处理目标组设置失败：第 {} 个句柄 {} 未登记", each, handles[each]);
				return false;
			}
		}
		//写入目标组（首项为场景目标，其余为额外中间目标）
		impl->present_target = handles[0];
		impl->present_extra.assign(handles + 1, handles + count);
		logger.info("渲染器:: 后处理目标组已设置（{} 个）", count);
		return true;
	}

	//设置「同帧后处理链」（若干已登记管线句柄，下标即级序）
	bool Renderer_Post::post_chain_set(const uint64_t* handles, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 后处理链设置失败：尚未初始化");
			return false;
		}
		//若为清除链
		if (count == 0)
		{
			impl->post_chain.clear();
			//链已清空 → 逐级专设参数与描边色一并失效
			for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
			{
				impl->post_stage_param_valid[stage] = false;
				impl->post_stage_color_valid[stage] = false;
				impl->post_stage_extra_valid[stage] = false;
			}

			logger.info("渲染器:: 后处理链已清除（回落单级默认后处理）");
			return true;
		}
		//句柄数组为空而级数非零则整链拒绝
		if (handles == nullptr)
		{
			logger.error("渲染器:: 后处理链设置失败：句柄数组为空而级数非零");
			return false;
		}
		/*
		级数上限（处置 R-FRAME-12 之①）
		每级都要成对目标里的一级源 / 宿，且每帧后处理仅占 1 个实例位；超过上限即整链拒绝，
		避免调用方误用超长链导致隐性性能坍塌（上限取 8，足够覆盖常见后处理链）。
		*/
		if (count > POST_CHAIN_MAX_STAGES)
		{
			logger.error("渲染器:: 后处理链设置失败：级数 {} 超过上限 {}", count, POST_CHAIN_MAX_STAGES);
			return false;
		}
		//逐句柄校验已登记（任一句柄未登记即整链拒绝，保持原链不变）
		for (uint32_t index = 0; index < count; ++index)
		{
			//若该级句柄未登记
			if (!impl->pipeline.pipeline_has(handles[index]))
			{
				logger.error("渲染器:: 后处理链设置失败：第 {} 级句柄 {} 未登记", index, handles[index]);
				return false;
			}
		}
		//写入链（保持各句柄的登记顺序不变）
		impl->post_chain.assign(handles, handles + count);
		//链已变 → 逐级专设参数与描边色失效（旧下标对应旧级序）
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			impl->post_stage_param_valid[stage] = false;
			impl->post_stage_color_valid[stage] = false;
				impl->post_stage_extra_valid[stage] = false;
		}

		logger.info("渲染器:: 后处理链已设置（{} 级）", count);
		return true;
	}

	//读取当前后处理链级数（0 表未设链）
	uint32_t Renderer_Post::post_chain_count(void) const
	{
		//未初始化则视为空链
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->post_chain.size());
	}

	//读回当前后处理链的各级管线句柄（只读诊断入口）
	uint32_t Renderer_Post::post_chain_get(uint64_t* out, uint32_t capacity) const
	{
		//出参为空 / 未初始化 / 未设链则返回 0
		if (out == nullptr || impl == nullptr || !impl->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>(impl->post_chain.size());
		if (count > capacity)
			count = capacity;
		//按当前级序逐级写出（级序可能因管线卸载而压缩）
		for (uint32_t index = 0; index < count; ++index)
			out[index] = impl->post_chain[index];
		return count;
	}

	//当前链各级管线的实际采样数（第一百一十六轮）
	uint32_t Renderer_Post::post_chain_sample_counts(uint32_t* out, uint32_t capacity) const
	{
		//出参为空 / 容量为零 / 未初始化则返回 0
		if (out == nullptr || capacity == 0 || impl == nullptr || !impl->initialized)
			return 0;
		//实际写入级数 = min(链级数, 容量)
		uint32_t count = static_cast<uint32_t>(impl->post_chain.size());
		if (count > capacity)
			count = capacity;
		//逐级查询管线采样数
		for (uint32_t index = 0; index < count; ++index)
			out[index] = impl->pipeline.pipeline_sample_count(impl->post_chain[index]);
		return count;
	}

	//把渲染目标的颜色绑定为当前纹理（供后续绘制采样）
	void Renderer_Post::texture_bind_target(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//取目标颜色图像与视图
		VkImage color_image = VK_NULL_HANDLE;
		VkImageView color_view = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkExtent2D extent = { 0, 0 };
		//句柄无效则静默返回（保持原绑定）
		if (!impl->render_target.target_get(handle, color_image, color_view, depth_view, extent))
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
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, shared_sampler))
		{
			logger.error("渲染器:: 渲染目标绑定失败：共享采样器不可用");
			return;
		}
		//记录当前绑定视图（与纹理绑定共用同一语义；采样器状态切换后据此重写 set1）
		impl->bound_view = color_view;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//把目标颜色视图写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, shared_sampler, color_view);
		logger.info("渲染器:: 渲染目标已绑定为当前纹理（句柄 {}）", handle);
	}

	//绑定 3D LUT 纹理（第一百四十四轮；第一百五十七轮补清除语义）
	void Renderer_Post::post_lut_bind(uint64_t texture_handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄 0 表示清除：改写为内置默认白纹理（2×2，使查找级原样输出）
		const uint64_t target_handle = texture_handle == 0 ? impl->default_texture : texture_handle;
		//取目标纹理的视图与采样器（非零且未登记则告警并保持原绑定）
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(target_handle, view, sampler))
		{
			logger.warn("渲染器:: 查找表纹理句柄 {} 无效，绑定忽略", texture_handle);
			return;
		}
		/*
		写满全部帧槽的全部后处理描述符集之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//逐帧槽逐后处理集写入 binding 3（长命资源，一次写满全部槽）
		bool all_bound = true;
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			for (uint32_t index = 0; index < Pipeline_Context::POST_SET_PER_SLOT; ++index)
				//任一集写入失败即记为不完整
				if (!impl->pipeline.post_lut_bind(slot, sampler, view, index))
					all_bound = false;
		//全部写入成功才记录句柄（清除时记录 0）
		if (all_bound)
			impl->lut_texture = texture_handle == 0 ? 0 : texture_handle;
		logger.info("渲染器:: 3D LUT 绑定已更新（句柄 {}）；清除时为 0", texture_handle);
	}

	//当前 3D LUT 句柄（第一百四十四轮）
	uint64_t Renderer_Post::post_lut_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->lut_texture;
	}

	//从 TTF 文件加载字体（字号 = 字形像素高度）
	bool Renderer_Text::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 加载字体失败：尚未初始化");
			return false;
		}
		//转交字体模块（光栅化字形并打包图集）
		return impl->font.font_load_file(handle_out, path, pixel_height);
	}

	//卸载字体（一并清除其文本网格缓存）
	void Renderer_Text::font_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//先清除该字体的文本网格缓存（网格模块就绪时归还网格）
		impl->text.font_meshes_destroy(handle);
		//归还字体（含图集纹理）
		impl->font.font_destroy(handle);
	}

	//绘制文本（默认布局：单行、左对齐）
	bool Renderer_Text::text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y)
	{
		//默认布局即单行左对齐（不换行、行高取字体像素高度）
		return self().text_draw_layout(font, text, pixel_x, pixel_y, Text_Layout{});
	}

	//度量单行文本（像素；宽不含尾随空白，高恒为字体像素高度）
	bool Renderer_Text::text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const
	{
		//默认零度量
		if (width_out != nullptr)
			*width_out = 0.0f;
		if (height_out != nullptr)
			*height_out = 0.0f;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//取字体记录（只读）
		const Font_Record* record = impl->font.font_get(font);
		if (record == nullptr)
			return false;
		//像素度量（整型）
		uint32_t width = 0;
		uint32_t height = 0;
		if (!Font_Context::text_measure(*record, text, width, height))
			return false;
		//写出浮点出参
		if (width_out != nullptr)
			*width_out = static_cast<float>(width);
		if (height_out != nullptr)
			*height_out = static_cast<float>(height);
		return true;
	}

	//按布局绘制文本（像素坐标，原点在窗口左上、Y 向下）；第四十七轮下沉至 Text_Renderer
	bool Renderer_Text::text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
		const Text_Layout& layout)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//标记进入文字绘制（第五十二轮：文字网格不参与阴影投影）
		impl->in_text_draw = true;
		//转发文字绘制子系统
		const bool drawn = impl->text.draw_layout(font, text, pixel_x, pixel_y, layout);
		//恢复场景绘制标记
		impl->in_text_draw = false;
		return drawn;
	}

	//设置泛光参数（阈值 / 软膝 / 强度）
	void Renderer_Post::bloom_param_set(float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：阈值须正、软膝非负且小于阈值、强度须为有限正值
		bool valid = (threshold > 0.0f) && (knee >= 0.0f) && (knee < threshold)
			&& (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 泛光参数非法（阈值 {}、软膝 {}、强度 {}），整次忽略",
				threshold, knee, intensity);
			return;
		}
		impl->bloom_param[0] = threshold;
		impl->bloom_param[1] = knee;
		impl->bloom_param[2] = intensity;
	}

	//设置某一级后处理链的泛光参数（覆盖全局值）
	void Renderer_Post::post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级泛光参数设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		/*
		参数校验（第一百零八轮放宽）
		三级参数同时供「泛光类」与「色彩 / 色温类」预设使用，后者需要带符号或任意区间，
		故此处只要求三者均为有限值且强度落在 (0,100)；不再施加泛光语义的阈值 / 软膝约束。
		*/
		bool finite = (threshold == threshold) && (knee == knee) && (intensity == intensity);
		bool valid = finite && (intensity > 0.0f) && (intensity < 100.0f);
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 逐级后处理参数非法（级 {}：阈值 {}、软膝 {}、强度 {}），整次忽略",
				stage, threshold, knee, intensity);
			return;
		}
		impl->post_stage_param[stage][0] = threshold;
		impl->post_stage_param[stage][1] = knee;
		impl->post_stage_param[stage][2] = intensity;
		impl->post_stage_param[stage][3] = 0.0f;
		impl->post_stage_param_valid[stage] = true;
	}

	//四参逐级设置（第一百二十二轮；供分级调色阶段使用）
	void Renderer_Post::post_stage_param4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		/*
		参数校验（分级调色语义）
		黑位抬升 −1..1、伽马 0.01..10、增益 0..10、饱和度 0..4；
		任一越界或非有限值即整次忽略，保持该级原参数不变。
		*/
		bool finite = (x == x) && (y == y) && (z == z) && (w == w);
		bool valid = finite
			&& x >= -1.0f && x <= 1.0f
			&& y >= 0.01f && y <= 10.0f
			&& z >= 0.0f && z <= 10.0f
			&& w >= 0.0f && w <= 4.0f;
		//若非法则整次忽略（保持原值）
		if (!valid)
		{
			logger.warn("渲染器:: 逐级四参非法（级 {}：lift {}、gamma {}、gain {}、sat {}），整次忽略",
				stage, x, y, z, w);
			return;
		}
		impl->post_stage_param[stage][0] = x;
		impl->post_stage_param[stage][1] = y;
		impl->post_stage_param[stage][2] = z;
		impl->post_stage_param[stage][3] = w;
		impl->post_stage_param_valid[stage] = true;
	}

	/*
	设置某一级后处理链的通用四参（第三百七十三轮）
	与 post_stage_param4_set 写同一份逐级参数，但不做调色语义的取值校验（仅拒绝非有限值）；
	供体积雾等自定义后处理阶段使用任意量纲的参数。
	*/
	void Renderer_Post::post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级通用四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		//非有限值整次忽略（保持原值）
		bool finite = (x == x) && (y == y) && (z == z) && (w == w)
			&& x <= 3.4e38f && x >= -3.4e38f && y <= 3.4e38f && y >= -3.4e38f
			&& z <= 3.4e38f && z >= -3.4e38f && w <= 3.4e38f && w >= -3.4e38f;
		if (!finite)
		{
			logger.warn("渲染器:: 逐级通用四参含非有限值（级 {}），整次忽略", stage);
			return;
		}
		impl->post_stage_param[stage][0] = x;
		impl->post_stage_param[stage][1] = y;
		impl->post_stage_param[stage][2] = z;
		impl->post_stage_param[stage][3] = w;
		impl->post_stage_param_valid[stage] = true;
	}

	//设置某一级后处理链的描边色（第一百八十一轮）
	void Renderer_Post::post_stage_color_set(uint32_t stage, float r, float g, float b)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级描边色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0（黑），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			impl->post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		//第四分量保留（恒 0）
		impl->post_stage_color[stage][3] = 0.0f;
		impl->post_stage_color_valid[stage] = true;
	}

	/*
	四分量逐级颜色设置（第三百七十六轮）
	前三通道与 post_stage_color_set 同口径；第四分量供自定义阶段使用（如体积雾密度噪声强度）。
	*/
	void Renderer_Post::post_stage_color4_set(uint32_t stage, float r, float g, float b, float a)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级四分量色设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		//逐分量归一：非有限值按 0，否则钳到 [0, 1]
		const float channels[4] = { r, g, b, a };
		for (std::size_t index = 0; index < 4; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 0.0f;
			impl->post_stage_color[stage][index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
		impl->post_stage_color_valid[stage] = true;
	}

	/*
	逐级额外四参（第四百零一轮）
	写入推送块 offset 128 的第四组；设备推送块上限不足 144 字节时整次拒绝（参数保持全零）。
	仅拒绝非有限值；stage 须在链内。post_chain_set / 级序压缩时随其他逐级参数一并清除。
	*/
	void Renderer_Post::post_stage_extra_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//设备推送块上限不足：该组不可用
		if (!impl->post_extra_supported)
		{
			logger.warn("渲染器:: 逐级额外四参被拒绝：设备推送块上限不足 144 字节");
			return;
		}
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= POST_CHAIN_MAX_STAGES)
		{
			logger.warn("渲染器:: 逐级额外四参设置被忽略：级序 {} 不在当前链内（链长 {}）",
				stage, static_cast<uint32_t>(impl->post_chain.size()));
			return;
		}
		//非有限值整次忽略（保持原值）
		bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
		if (!finite)
		{
			logger.warn("渲染器:: 逐级额外四参含非有限值（级 {}），整次忽略", stage);
			return;
		}
		impl->post_stage_extra[stage][0] = x;
		impl->post_stage_extra[stage][1] = y;
		impl->post_stage_extra[stage][2] = z;
		impl->post_stage_extra[stage][3] = w;
		impl->post_stage_extra_valid[stage] = true;
	}

	//启用 / 关闭视锥剔除
	void Renderer_Post::cull_enabled_set(bool enabled)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->cull_enabled = enabled;
	}

	//查询视锥剔除是否启用
	bool Renderer_Post::cull_enabled(void) const
	{
		//未初始化时返回默认值（开启）
		if (impl == nullptr || !impl->initialized)
			return true;
		return impl->cull_enabled;
	}

	//启用 / 关闭静态合批（第一百二十七轮）
	void Renderer_Post::static_batching_set(bool enabled)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->static_batching = enabled;
	}

	//查询静态合批是否启用
	bool Renderer_Post::static_batching(void) const
	{
		//未初始化时返回默认值（开启）
		if (impl == nullptr || !impl->initialized)
			return true;
		return impl->static_batching;
	}

	//设置渲染工作线程数（第一百七十七轮）
	void Renderer_Post::render_threads_set(uint32_t count)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//线程数钳到 0..64
		if (count > 64)
			count = 64;
		//同值重复设置为空操作
		if (count == impl->thread_pool.thread_count_get())
			return;
		//先回收旧线程，再按新数量启动
		impl->thread_pool.threads_stop();
		impl->render_threads = 0;
		if (count > 0 && !impl->thread_pool.threads_start(count))
		{
			logger.warn("渲染器:: 渲染工作线程切换失败，保持单线程");
			return;
		}
		impl->render_threads = impl->thread_pool.thread_count_get();
		logger.info("渲染器:: 渲染工作线程已设为 {}", impl->render_threads);
	}

	//查询当前渲染工作线程数
	uint32_t Renderer_Post::render_threads(void) const
	{
		//未初始化时返回默认值（0 = 单线程）
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->thread_pool.thread_count_get();
	}

	//设置实例级剔除的并行阈值（第二百二十二轮）
	void Renderer_Post::cull_parallel_min_instances_set(uint32_t count)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//钳到 1..65536
		if (count < 1)
			count = 1;
		if (count > 65536)
			count = 65536;
		//同值重复设置为空操作
		if (impl->cull_parallel_min_instances == count)
			return;
		impl->cull_parallel_min_instances = count;
		logger.info("渲染器:: 实例剔除并行阈值已设为 {}", count);
	}

	//查询实例级剔除的并行阈值
	uint32_t Renderer_Post::cull_parallel_min_instances(void) const
	{
		//未初始化时返回 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->cull_parallel_min_instances);
	}

	//设置实例缓冲差异扫描 / 缓存回写的并行阈值（第二百二十六轮）
	void Renderer_Post::instance_scan_parallel_min_set(uint32_t count)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//钳到 1..65536
		if (count < 1)
			count = 1;
		if (count > 65536)
			count = 65536;
		//同值重复设置为空操作
		if (impl->instance_scan_parallel_min == count)
			return;
		impl->instance_scan_parallel_min = count;
		logger.info("渲染器:: 实例扫描并行阈值已设为 {}", count);
	}

	//查询实例缓冲差异扫描 / 缓存回写的并行阈值
	uint32_t Renderer_Post::instance_scan_parallel_min(void) const
	{
		//未初始化时返回 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->instance_scan_parallel_min);
	}

	//读取某一级的泛光专设参数（只读诊断）
	uint32_t Renderer_Post::post_stage_param_get(uint32_t stage, float out[4]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return 0;
		//默认写零
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = 0.0f;
		//未初始化 / 级序不在链内 / 该级未设专设值 → 0
		if (impl == nullptr || !impl->initialized || stage >= impl->post_chain.size()
			|| stage >= POST_CHAIN_MAX_STAGES || !impl->post_stage_param_valid[stage])
			return 0;
		//写出该级专设值
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = impl->post_stage_param[stage][each];
		return 1;
	}

	//批量读回当前链各级的专设参数（只读诊断）
	uint32_t Renderer_Post::post_chain_params_get(float* out, uint32_t capacity) const
	{
		//出参为空或容量不足一级则无内容可写
		if (out == nullptr || capacity < 4)
			return 0;
		//实际可写的级数（受链级数与容量共同限制）
		uint32_t stages = capacity / 4;
		//未初始化时链为空
		uint32_t chain_size = (impl == nullptr || !impl->initialized)
			? 0u : static_cast<uint32_t>(impl->post_chain.size());
		if (chain_size > POST_CHAIN_MAX_STAGES)
			chain_size = POST_CHAIN_MAX_STAGES;
		if (stages > chain_size)
			stages = chain_size;
		//逐级写出（未专设的级写 0）
		for (uint32_t stage = 0; stage < stages; ++stage)
		{
			//该级是否设过专设值
			bool valid = impl->post_stage_param_valid[stage];
			for (uint32_t each = 0; each < 4; ++each)
				out[stage * 4 + each] = valid ? impl->post_stage_param[stage][each] : 0.0f;
		}
		return stages;
	}

	//设置文字颜色（RGBA，逐分量钳制）
	void Renderer_Text::text_color_set(const float color[4])
	{
		//未初始化或出参为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//转发文字绘制子系统（逐分量钳制）
		impl->text.color_set(color);
	}

	//设置文字描边（第一百四十轮；UV 半宽 ＋ 描边色）
	void Renderer_Text::text_outline_set(float uv_width, const float color[3])
	{
		//未初始化或颜色为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//半宽按 0..0.05 钳制（负值 / 非数归 0，避免无意义的大范围采样）
		if (!(uv_width > 0.0f))
			uv_width = 0.0f;
		if (uv_width > 0.05f)
			uv_width = 0.05f;
		impl->text_outline_uv_width = uv_width;
		//描边色逐分量钳制到 0..1
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = color[index];
			//非数 / 负值归 0、大于 1 归 1
			if (!(value > 0.0f))
				value = 0.0f;
			if (value > 1.0f)
				value = 1.0f;
			impl->text_outline_color[index] = value;
		}
	}

	//取该字体累计被跳过的码点数（诊断入口）
	uint64_t Renderer_Text::font_skipped_codepoints(uint64_t font) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->font.font_skipped_codepoints_get(font);
	}

	//文字网格缓存命中次数（第七十五轮）
	uint64_t Renderer_Text::text_cache_hits(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_hits();
	}

	//设置文本网格缓存条目上限（第八十四轮）
	void Renderer_Text::text_cache_capacity_set(uint32_t capacity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->text.mesh_cache_capacity_set(capacity);
	}

	//文本网格缓存条目上限（第八十四轮）
	uint32_t Renderer_Text::text_cache_capacity(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_capacity();
	}

	//当前文本网格缓存条目数（第八十四轮）
	uint32_t Renderer_Text::text_cache_size(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_size();
	}

	//文字网格缓存未命中次数（第七十五轮）
	uint64_t Renderer_Text::text_cache_misses(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_misses();
	}

}
