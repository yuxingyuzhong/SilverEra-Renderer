#include "src/render/Renderer/局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//std::nothrow
#include <new>
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
#include <cmath>
//管线缓存文件读写（第一百零六轮）
#include <fstream>
#include <filesystem>
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

//游戏引擎命名空间
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

		//列主序 4×4 矩阵相乘（out = a × b；out 可与 a / b 不同）
		void matrix4_multiply(const float a[16], const float b[16], float out[16])
		{
			for (std::size_t column = 0; column < 4; ++column)
				for (std::size_t row = 0; row < 4; ++row)
				{
					//累加「a 的第 row 行」与「b 的第 column 列」的点积
					float sum = 0.0f;
					for (std::size_t k = 0; k < 4; ++k)
						sum += a[k * 4 + row] * b[column * 4 + k];
					out[column * 4 + row] = sum;
				}
		}

		/*
		相邻等价绘制项合批（第一百二十九轮；主 pass 与阴影 pass 共用）
		合并条件：管线 / 布局 / 顶点与索引缓冲 / 索引区 / 推送常量逐字节 / 后处理参数 /
		实例缓冲相同且实例区首尾相接 / 阴影标记 / 采样数全等。
		只累加实例数、不重排顺序，故绘制结果逐字节不变。
		返回被合并掉的项数（第一百三十五轮）；scratch 为复用缓冲（第一百三十六轮，避免逐帧分配）。
		*/
		std::size_t batch_adjacent_items(std::vector<Frame_Render_Item>& items, std::vector<Frame_Render_Item>& scratch)
		{
			//不足两项无需合并
			if (items.size() < 2)
				return 0;
			//复用外部缓冲作为合并结果存储（保留首个等价项并累加实例数）
			scratch.clear();
			scratch.reserve(items.size());
			for (const Frame_Render_Item& item : items)
			{
				//能否并入前一项
				bool can_merge = false;
				//若已有可比较的前一项
				if (!scratch.empty())
				{
					const Frame_Render_Item& last = scratch.back();
					//推送常量是否逐字节一致（0 字节表示两侧都不推送）
					bool push_same = last.push_constant_size == item.push_constant_size
						&& last.push_constant_offset == item.push_constant_offset;
					if (push_same && last.push_constant_size != 0)
						push_same = last.push_constants != nullptr && item.push_constants != nullptr
							&& memcmp(last.push_constants, item.push_constants, last.push_constant_size) == 0;
					//后处理参数是否一致（场景项恒相等）
					bool post_same = memcmp(last.post_params, item.post_params, sizeof(last.post_params)) == 0;
					//描边色是否一致（第一百八十一轮；场景项恒全零）
					if (post_same)
						post_same = memcmp(last.post_color, item.post_color, sizeof(last.post_color)) == 0;
					//实例区是否首尾相接
					bool instance_adjacent = last.instance_buffer == item.instance_buffer
						&& last.instance_offset + static_cast<VkDeviceSize>(last.instance_count) * INSTANCE_STRIDE_BYTES
							== item.instance_offset;
					can_merge = push_same && post_same && instance_adjacent
						&& !last.transparent && !item.transparent
						&& last.pipeline == item.pipeline
						&& last.layout == item.layout
						&& last.vertex_buffer == item.vertex_buffer
						&& last.index_buffer == item.index_buffer
						&& last.index_offset == item.index_offset
						&& last.index_count == item.index_count
						&& last.cast_shadow == item.cast_shadow
						&& last.sample_count == item.sample_count
						/*
						采样状态须一致（第一百七十四轮）
						合批会让多个实例共用一条采样器集，纹理不同则不能合并。
						*/
						&& last.draw_sampler == item.draw_sampler
						&& last.draw_albedo == item.draw_albedo
						&& last.draw_normal == item.draw_normal
						&& last.draw_roughness == item.draw_roughness
						&& last.draw_emissive == item.draw_emissive
						&& last.draw_ao == item.draw_ao
						&& last.draw_metallic == item.draw_metallic;
				}
				//可合并则把实例数并入前一项（实例数据仍按原顺序连续）
				if (can_merge)
					scratch.back().instance_count += item.instance_count;
				else
					scratch.push_back(item);
			}
			//合并前项数
			std::size_t original_size = items.size();
			items.swap(scratch);
			//返回被合并掉的项数（原项数 − 合并后项数）
			return original_size > items.size() ? original_size - items.size() : 0;
		}

		/*
		阴影输入签名（第五十五轮；第一百二十一轮加级联信息）
		把光源空间矩阵（含远区级联矩阵）、本帧实例矩阵（全部 float 位模式）、投影项数、阴影图边长
		与级联数混入 64 位 FNV-1a；用于判定阴影图能否跨帧复用（完全一致才跳过重绘）。
		*/
		uint64_t shadow_signature_build(const float light_view_proj[16], const float light_view_proj_far[16],
			const float light_view_proj_far2[16],
			const float* instances, std::size_t instance_count, std::size_t shadow_item_count,
			uint32_t shadow_size, uint32_t cascade_count)
		{
			//FNV-1a 偏移基
			uint64_t hash = 1469598103934665603ull;
			//逐 float 混入位模式
			auto mix_float = [&hash](float value)
			{
				uint32_t bits = 0;
				std::memcpy(&bits, &value, sizeof(bits));
				hash ^= static_cast<uint64_t>(bits);
				hash *= 1099511628211ull;
			};
			//光源空间矩阵（近区）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj[index]);
			//远区级联矩阵（单级时为零矩阵，不影响既有签名分布）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj_far[index]);
			//第三级级联矩阵（第一百四十六轮；非三级时为零矩阵）
			for (std::size_t index = 0; index < 16; ++index)
				mix_float(light_view_proj_far2[index]);
			//实例矩阵（每项 16 个 float）
			for (std::size_t index = 0; index < instance_count; ++index)
				mix_float(instances[index]);
			//投影项数与阴影图边长
			hash ^= static_cast<uint64_t>(shadow_item_count);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(shadow_size);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(cascade_count);
			hash *= 1099511628211ull;
			return hash;
		}

		/*
		视锥剔除（保守口径；第二十二轮新增）
		把局部包围盒 8 个角经「model × view_proj」变换到裁剪空间，逐个测 6 个裁剪面；
		**仅当 8 个角同时落在同一个面外**时判为「完全不可见」（返回真）；
		任何相交 / 跨越近平面一律保留（不做背面与遮挡剔除）。
		*/
		bool frustum_cull_aabb(const float view_proj[16], const float model[16], const float bounds_min[3], const float bounds_max[3])
		{
			//组合矩阵（列主序）
			float mvp[16] = {};
			matrix4_multiply(view_proj, model, mvp);
			//六个裁剪面的「全部角点在外」标志（左 / 右 / 下 / 上 / 近 / 远）
			bool outside[6] = { true, true, true, true, true, true };
			for (std::size_t corner = 0; corner < 8; ++corner)
			{
				//角点局部坐标（按位取上下界）
				float x = (corner & 1) ? bounds_max[0] : bounds_min[0];
				float y = (corner & 2) ? bounds_max[1] : bounds_min[1];
				float z = (corner & 4) ? bounds_max[2] : bounds_min[2];
				//变换到裁剪空间
				float clip_x = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
				float clip_y = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
				float clip_z = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
				float clip_w = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
				//逐面判「该角是否在外」
				if (!(clip_x > clip_w))
					outside[0] = false;
				if (!(clip_x < -clip_w))
					outside[1] = false;
				if (!(clip_y > clip_w))
					outside[2] = false;
				if (!(clip_y < -clip_w))
					outside[3] = false;
				if (!(clip_z < 0.0f))
					outside[4] = false;
				if (!(clip_z > clip_w))
					outside[5] = false;
			}
			//任一面「全部角点在外」即视为完全不可见
			for (std::size_t plane = 0; plane < 6; ++plane)
				if (outside[plane])
					return true;
			return false;
		}

		//单位矩阵（列主序）
		const float IDENTITY_MATRIX4[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};

		//白色（RGBA；第六十九轮：未设置逐物体色调时的默认推送值，乘到反照率上画面不变）
		const float WHITE_COLOR_RGBA[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	//渲染层版本查询
	string renderer_version(void)
	{
		//渲染层版本号
		return "0.1.0";
	}

	/*
	查询颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十六轮）
	按 8 → 4 → 2 依次探测两种格式的「最优平铺 ＋ 对应附件用途」图像属性；
	返回不超过 requested 的最高支持值；均不支持则返回 1（关闭）。
	*/
	static uint32_t msaa_supported_max(VkPhysicalDevice physical_device, VkFormat color_format,
		VkFormat depth_format, uint32_t requested)
	{
		//颜色格式属性（含其支持的采样数位集）
		VkImageFormatProperties color_properties{};
		if (vkGetPhysicalDeviceImageFormatProperties(physical_device, color_format, VK_IMAGE_TYPE_2D,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &color_properties) != VK_SUCCESS)
			return 1;
		//深度格式属性（含其支持的采样数位集）
		VkImageFormatProperties depth_properties{};
		if (vkGetPhysicalDeviceImageFormatProperties(physical_device, depth_format, VK_IMAGE_TYPE_2D,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &depth_properties) != VK_SUCCESS)
			return 1;
		//候选采样数（自高到低；两种格式须同时支持）
		const uint32_t candidates[3] = { 8, 4, 2 };
		for (uint32_t candidate : candidates)
		{
			//超过请求值则跳过
			if (candidate > requested)
				continue;
			VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(candidate);
			//颜色与深度格式均须支持该采样数
			if ((color_properties.sampleCounts & samples) != 0 && (depth_properties.sampleCounts & samples) != 0)
				return candidate;
		}
		return 1;
	}

	//重建交换链并把旧资源压入帧延迟销毁队列
	bool Renderer::Impl::swapchain_rebuild_deferred(uint32_t width, uint32_t height)
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
		return true;
	}

	/*
	把后处理采样集绑定到给定目标视图组（第九十七轮；第一百二十三轮增加场景深度）
	签名（采样器 ＋ 视图组与数量 ＋ 深度视图）未变则直接返回；变化时先等尽在途帧，再逐帧槽写入。
	binding 0 = 该级源（views[index]）、binding 1 = 场景目标（views[0]）、
	binding 2 = 场景深度（depth_view 非空时才写，供景深类片元采样）。
	*/
	bool Renderer::Impl::post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler, VkImageView depth_view)
	{
		//数量钳到每帧槽后处理集上限
		if (count > PRESENT_TARGET_MAX)
			count = PRESENT_TARGET_MAX;
		//签名比较：采样器、数量、深度与逐视图一致则空操作
		bool same = post_bind_cached && post_bound_sampler == sampler && post_bound_view_count == count
			&& post_bound_depth == depth_view;
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
			}
		//更新签名缓存
		post_bound_sampler = sampler;
		post_bound_view_count = count;
		post_bound_depth = depth_view;
		for (uint32_t index = 0; index < count; ++index)
			post_bound_views[index] = views[index];
		post_bind_cached = true;
		return true;
	}

	//确保后处理全屏四边形网格就绪（惰性创建；单级路径与链路径共用）
	bool Renderer::Impl::post_mesh_ensure(void)
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
	bool Renderer::Impl::post_pipeline_ensure(void)
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
	bool Renderer::Impl::post_resources_ensure(void)
	{
		//先确保默认后处理管线，再确保全屏网格
		if (!post_pipeline_ensure())
			return false;
		return post_mesh_ensure();
	}

	//确保 SSAA 降采样具名管线就绪（惰性登记）
	bool Renderer::Impl::ssaa_pipeline_ensure(void)
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
	bool Renderer::Impl::sky_pipeline_ensure(void)
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
	void Renderer::Impl::ssaa_targets_destroy(void)
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
	bool Renderer::Impl::ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor)
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

	//构造（不创建任何 GPU 资源）
	Renderer::Renderer(void)
		: impl(new(std::nothrow) Impl())
	{
	}

	//析构（内部调用 shutdown，保证资源不泄漏）
	Renderer::~Renderer(void)
	{
		shutdown();
	}

	//初始化
	bool Renderer::init(const Renderer_Config& config)
	{
		//若实现体未分配成功
		if (impl == nullptr)
		{
			logger.error("渲染器:: 实现体分配失败");
			return false;
		}
		//若已初始化
		if (impl->initialized)
		{
			logger.warn("渲染器:: 已初始化，忽略重复 init");
			return true;
		}

		//参数校验：宽高非零
		if (config.width == 0 || config.height == 0)
		{
			logger.error("渲染器:: 窗口尺寸不得为零（{}×{}）", config.width, config.height);
			return false;
		}
		//参数校验：在途帧数量在 [1,3]
		if (config.frame_overlap < 1 || config.frame_overlap > 3)
		{
			logger.error("渲染器:: 在途帧数量越界（{}，应在 [1,3]）", config.frame_overlap);
			return false;
		}

		//生效配置（Release 下强制关闭校验层；K21）
		Renderer_Config effective = config;
#ifdef NDEBUG
		effective.validation = false;
#endif

		//① 平台：视频子系统与窗口
		if (!impl->platform.build(effective.width, effective.height, effective.title, effective.hidden))
		{
			logger.error("渲染器:: 平台初始化失败");
			shutdown();
			return false;
		}
		//② 平台：实例扩展清单
		vector<const char*> instance_extensions;
		if (!impl->platform.instance_extensions_get(instance_extensions))
		{
			logger.error("渲染器:: 实例扩展清单获取失败");
			shutdown();
			return false;
		}
		//③ 设备：实例与调试信使
		if (!impl->device.build_instance(instance_extensions, effective.validation))
		{
			logger.error("渲染器:: 实例创建失败");
			shutdown();
			return false;
		}
		//④ 平台：表面（必须先于设备创建，供呈现队列族查询）
		if (!impl->platform.surface_build(impl->device.instance_get()))
		{
			logger.error("渲染器:: 表面创建失败");
			shutdown();
			return false;
		}
		//⑤ 设备：物理设备与逻辑设备
		if (!impl->device.build_device(impl->platform.surface_get()))
		{
			logger.error("渲染器:: 逻辑设备创建失败");
			shutdown();
			return false;
		}
		//⑥ 内存：VMA 分配器
		if (!impl->memory.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get()))
		{
			logger.error("渲染器:: 内存初始化失败");
			shutdown();
			return false;
		}

		//取窗口客户区实际尺寸（HiDPI 下可能与配置不同）
		uint32_t swapchain_width = 0;
		uint32_t swapchain_height = 0;
		impl->platform.window_size_get(swapchain_width, swapchain_height);
		//若窗口尺寸不可用则回退配置值
		if (swapchain_width == 0 || swapchain_height == 0)
		{
			swapchain_width = effective.width;
			swapchain_height = effective.height;
		}
		//⑦ 交换链（深度图像依赖内存模块，故传入 memory；首次建链固定单采样，待格式确定后再择定 MSAA）
		impl->swapchain.sample_count_set(1);
		if (!impl->swapchain.build(impl->device.physical_device_get(), impl->memory, impl->device.device_get(),
			impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
			swapchain_width, swapchain_height, effective.vsync))
		{
			logger.error("渲染器:: 交换链创建失败");
			shutdown();
			return false;
		}
		/*
		MSAA 采样数择定（第九十六轮）
		交换链格式已确定，按设备对「颜色 ＋ 深度」两种格式的共同支持上限钳制请求值；
		请求 > 1 时重建一次交换链以建立多重采样附件（初始化期一次性开销）。
		*/
		{
			//请求值归一（仅接受 2 / 4 / 8，其余按 1）
			uint32_t requested_samples = effective.msaa_samples;
			if (requested_samples != 2 && requested_samples != 4 && requested_samples != 8)
				requested_samples = 1;
			//按设备支持钳制
			uint32_t chosen_samples = msaa_supported_max(impl->device.physical_device_get(),
				impl->swapchain.format_get(), impl->swapchain.depth_format_get(), requested_samples);
			//请求未被完全满足时告警
			if (requested_samples > 1 && chosen_samples < requested_samples)
				logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", requested_samples, chosen_samples);
			//若启用 MSAA：重建交换链以建立多重采样附件
			if (chosen_samples > 1)
			{
				impl->swapchain.sample_count_set(chosen_samples);
				impl->swapchain.destroy();
				if (!impl->swapchain.build(impl->device.physical_device_get(), impl->memory, impl->device.device_get(),
					impl->platform.surface_get(), impl->device.graphics_family_get(), impl->device.present_family_get(),
					swapchain_width, swapchain_height, effective.vsync))
				{
					logger.error("渲染器:: 交换链 MSAA 附件创建失败");
					shutdown();
					return false;
				}
			}
			impl->msaa_samples = impl->swapchain.sample_count_get();
			logger.info("渲染器:: MSAA 采样数 {}", impl->msaa_samples);
		}
		/*
		钳制在途帧数
		图像可用信号量属「每交换链图像」（数量 = 交换链图像数），帧模块按该池轮转取用；
		仅当在途帧数不超过信号量数时，「先等帧栅栏」才能保证被取用的信号量已在前一次被消费（K13）。
		*/
		uint32_t image_available_count = impl->swapchain.image_available_count_get();
		//若在途帧数超过图像可用信号量数则下调并告警
		if (image_available_count > 0 && effective.frame_overlap > image_available_count)
		{
			logger.warn("渲染器:: 在途帧数 {} 超过图像可用信号量数 {}，已下调至前者", effective.frame_overlap, image_available_count);
			effective.frame_overlap = image_available_count;
		}
		//⑧ 帧（像素回读缓冲依赖内存模块，故传入 memory）
		/*
		实例存储容量与渲染工作线程（第一百七十七轮）
		容量钳到 [16, 1048576]，随后按该容量分配逐帧实例缓冲（默认 256，与引入该配置前一致）；
		线程数大于 0 时启动工作线程，启动失败回落为单线程（仅影响耗时，不影响结果）。
		*/
		{
			//容量归一
			uint32_t instance_capacity = effective.instance_capacity;
			if (instance_capacity < 16)
				instance_capacity = 16;
			if (instance_capacity > 1048576)
				instance_capacity = 1048576;
			impl->instance_capacity = instance_capacity;
			/*
			实例登记缓冲预保留（第一百九十四轮，优化）
			pending_items 已在下方按实例容量预留（第一百三十六轮）；此处补 pending_instances 的同类预留，
			避免帧循环内首次扩容（行为不变）。
			*/
			impl->pending_instances.reserve(impl->instance_capacity);
			//掩码缓冲按容量预留（避免逐次调用分配）
			impl->instanced_cull_mask.reserve(instance_capacity);
			impl->instance_changed_mask.reserve(instance_capacity);
			//线程数归一
			uint32_t render_threads = effective.render_threads;
			if (render_threads > 64)
				render_threads = 64;
			//启动工作线程（0 = 单线程）
			if (render_threads > 0 && !impl->thread_pool.threads_start(render_threads))
			{
				logger.warn("渲染器:: 渲染工作线程启动失败，回落为单线程");
				render_threads = 0;
			}
			impl->render_threads = render_threads;
			/*
			实例剔除并行阈值（第二百二十二轮；钳 1..65536）
			默认 256 与引入该配置前一致；调低可让小批量实例也走并行，调高则更保守。
			*/
			uint32_t cull_parallel_min = effective.cull_parallel_min_instances;
			if (cull_parallel_min < 1)
				cull_parallel_min = 1;
			if (cull_parallel_min > 65536)
				cull_parallel_min = 65536;
			impl->cull_parallel_min_instances = cull_parallel_min;
			/*
			差异扫描 / 缓存回写并行阈值（第二百二十六轮；钳 1..65536）
			默认 512 与引入该配置前一致。
			*/
			uint32_t scan_parallel_min = effective.instance_scan_parallel_min;
			if (scan_parallel_min < 1)
				scan_parallel_min = 1;
			if (scan_parallel_min > 65536)
				scan_parallel_min = 65536;
			impl->instance_scan_parallel_min = scan_parallel_min;
			/*
			并行录制最小项数（第二百零二轮；钳到 2..4096）
			线程数为 0 时该值无作用（始终单线程）。
			*/
			uint32_t parallel_min_items = effective.parallel_min_items;
			if (parallel_min_items < 2)
				parallel_min_items = 2;
			if (parallel_min_items > 4096)
				parallel_min_items = 4096;
			impl->parallel_min_items = parallel_min_items;
			//把阈值同步给帧模块（第二百零二轮）
			impl->frame.parallel_min_items_set(parallel_min_items);
			/*
			并行段数上限（第二百零三轮；钳到 2..32）
			段数 = min(线程数, 上限, 项数 / 16)。
			*/
			uint32_t parallel_max_segments = effective.parallel_max_segments;
			if (parallel_max_segments < 2)
				parallel_max_segments = 2;
			if (parallel_max_segments > 32)
				parallel_max_segments = 32;
			impl->parallel_max_segments = parallel_max_segments;
			impl->frame.parallel_max_segments_set(parallel_max_segments);
			//把线程池交给帧模块（第一百九十七轮：场景 pass 次级缓冲并行录制；空 / 0 线程时退回单线程）
			impl->frame.thread_pool_set(&impl->thread_pool);
			logger.info("渲染器:: 渲染工作线程 {}（每帧实例容量 {}）", render_threads, instance_capacity);
		}
		/*
		预留帧内待绘制项与实例存储容量（第一百三十六轮）
		容量取自实例上限，避免逐帧扩容；仅在初始化时执行一次。
		*/
		impl->pending_items.reserve(impl->instance_capacity);
		impl->pending_instances.reserve(impl->instance_capacity);
		//逐绘制项采样集输入缓冲按预留上限预分配（第二百二十一轮，避免逐帧扩容）
		impl->draw_set_inputs.reserve(Pipeline_Context::DRAW_SET_PER_SLOT);
		//实例级剔除的筛选缓冲（第一百五十三轮；按实例上限预留，避免逐帧扩容）
		impl->instanced_cull_scratch.reserve(static_cast<std::size_t>(impl->instance_capacity) * 16);
		impl->batch_scratch.reserve(impl->instance_capacity);
		//阴影列表与后处理绘制项的复用缓冲（第一百八十二轮；避免逐帧堆分配）
		impl->shadow_items_scratch.reserve(64);
		impl->post_items_scratch.reserve(Impl::POST_CHAIN_MAX_STAGES + 2);
		//后处理级管线序列复用缓冲（第一百八十七轮）
		impl->post_stage_pipelines_scratch.reserve(Impl::POST_CHAIN_MAX_STAGES + 1);

		if (!impl->frame.build(impl->device.device_get(), impl->device.physical_device_get(),
			impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->device.present_family_get(), impl->device.present_queue_get(),
			impl->swapchain, impl->memory, effective.frame_overlap))
		{
			logger.error("渲染器:: 帧基础设施创建失败");
			shutdown();
			return false;
		}
		//⑨ 着色器：记录逻辑设备与着色器目录
		if (!impl->shader.build(impl->device.device_get()))
		{
			logger.error("渲染器:: 着色器模块初始化失败");
			shutdown();
			return false;
		}
		impl->shader.directory_set(effective.shader_dir);
		//⑩ 加载顶点 / 片元着色器模块
		//加载场景顶点着色器（顺带解析其声明的顶点输入位置，供管线裁剪属性）
		if (!impl->shader.module_load(impl->vert_module, "场景.vert.spv", &impl->scene_input_locations))
		{
			logger.error("渲染器:: 顶点着色器加载失败");
			shutdown();
			return false;
		}
		if (!impl->shader.module_load(impl->frag_module, "场景.frag.spv"))
		{
			logger.error("渲染器:: 片元着色器加载失败");
			shutdown();
			return false;
		}
		//⑪ 管线：描述符布局 / 管线布局 / 描述符池与每帧槽描述符集
		{
			Pipeline_Config pipeline_config{};
			pipeline_config.color_format = impl->swapchain.format_get();
			pipeline_config.depth_format = impl->swapchain.depth_format_get();
			pipeline_config.frame_overlap = effective.frame_overlap;
			pipeline_config.sample_count = impl->msaa_samples;
			if (!impl->pipeline.build(impl->device.device_get(), pipeline_config))
			{
				logger.error("渲染器:: 管线基础设施创建失败");
				shutdown();
				return false;
			}
		}
		/*
		载入管线缓存（第一百零六轮；可选）
		须在创建任何图形管线之前注入；文件缺失或读取失败仅告警，不阻断初始化。
		*/
		if (!effective.pipeline_cache_path.empty())
		{
			std::ifstream cache_stream(path_from_utf8(effective.pipeline_cache_path), std::ios::binary | std::ios::ate);
			//文件存在则读出全部字节
			if (cache_stream.is_open())
			{
				std::streamsize cache_size = cache_stream.tellg();
				if (cache_size > 0)
				{
					std::vector<uint8_t> cache_bytes(static_cast<std::size_t>(cache_size));
					cache_stream.seekg(0);
					cache_stream.read(reinterpret_cast<char*>(cache_bytes.data()), cache_size);
					if (!cache_bytes.empty())
						impl->pipeline.cache_set(cache_bytes.data(), cache_bytes.size());
				}
				cache_stream.close();
			}
		}
		//⑫ 管线：图形管线（初始档位 = 不透明）
		if (!impl->pipeline.pipeline_build(impl->vert_module, impl->frag_module, Blend_Mode::Opaque,
			impl->scene_input_locations))
		{
			logger.error("渲染器:: 图形管线创建失败");
			shutdown();
			return false;
		}
		/*
		⑫.1 反向外扩描边管线（第二百二十八轮）
		剔除正面的场景管线（沿法线外扩后只剩背面），与默认场景管线共享布局与描述符集布局；
		着色器缺失（未随构建拷贝）时不阻断初始化，仅告警并保持描边关闭。
		*/
		{
			uint64_t outline_handle = 0;
			if (impl->pipeline_load_ex(outline_handle, "反向外扩.vert.spv", "反向外扩.frag.spv",
				Blend_Mode::Opaque, false, false, true, true))
				impl->outline_pipeline = outline_handle;
			else
				logger.warn("渲染器:: 外扩描边管线登记失败（描边将保持关闭）");
		}
		/*
		⑬ 场景 UBO：每帧槽一个主机可见缓冲（64 字节 mat4），
		写入相机视图 × 投影并把该帧槽 set0 绑定到对应 UBO。
		*/
		impl->scene_ubos.resize(effective.frame_overlap);
		impl->scene_ubo_allocs.resize(effective.frame_overlap);
		//实例缓冲按帧槽各一（主机可见，逐帧写入本帧全部实例模型矩阵）
		impl->instance_buffers.resize(effective.frame_overlap);
		impl->instance_allocations.resize(effective.frame_overlap);
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途；第一百二十五轮由 1072 扩为 1088 字节：追加天空盒参数；第一百四十六轮扩为 1168；第一百七十九轮扩为 1184：追加卡通高光参数）
			if (!impl->memory.buffer_create(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				static_cast<VkDeviceSize>(Impl::SCENE_BLOCK_BYTES),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 场景 UBO 创建失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始视图 × 投影矩阵（列主序 16 浮点）
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], view_proj, 64, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始光照块（offset 64、64 字节：公共参数 ＋ 方向光 ＋ 颜色 ＋ 相机位置）
			float light_block[16] = {
				impl->light_ambient, impl->specular_intensity, impl->specular_shininess, static_cast<float>(impl->point_light_count),
				impl->light_direction[0], impl->light_direction[1], impl->light_direction[2], impl->light_intensity,
				impl->light_color[0], impl->light_color[1], impl->light_color[2], static_cast<float>(impl->attenuation_mode),
				0.0f, 0.0f, 0.0f, 0.0f,
			};
			//相机世界位置（镜面高光的视线方向来源）；位置在首帧前为默认机位，w = 方向光逐光高光缩放（第五十轮）
			float camera_position[3] = {};
			impl->camera.position_get(camera_position);
			light_block[12] = camera_position[0];
			light_block[13] = camera_position[1];
			light_block[14] = camera_position[2];
			light_block[15] = impl->directional_specular;
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], light_block, 64, 64))
			{
				logger.error("渲染器:: 场景光照写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始点光源数组（offset 128、256 字节；8 个槽位，未用槽位清零；第六十二轮扩为 8 盏）
			float point_block[64] = {};
			for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
			{
				//先排全部「位置 ＋ 半径」，再排全部「颜色 ＋ 强度」（与着色器两个数组的 std140 布局一致）
				const std::size_t base = static_cast<std::size_t>(light) * 4;
				point_block[base + 0] = impl->point_lights[light].position[0];
				point_block[base + 1] = impl->point_lights[light].position[1];
				point_block[base + 2] = impl->point_lights[light].position[2];
				point_block[base + 3] = impl->point_lights[light].radius;
				//颜色段起点 = 8 盏位置（32 浮点）之后
				point_block[32 + base + 0] = impl->point_lights[light].color[0];
				point_block[32 + base + 1] = impl->point_lights[light].color[1];
				point_block[32 + base + 2] = impl->point_lights[light].color[2];
				point_block[32 + base + 3] = impl->point_lights[light].intensity;
			}
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], point_block, 256, 128))
			{
				logger.error("渲染器:: 场景点光源写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			/*
			写入初始聚光参数（offset 384、256 字节；第三十四轮；第六十二轮扩为 8 盏）
			前 128 字节为 8 个「方向 ＋ 有效标志」，后 128 字节为 8 组「内锥余弦 ＋ 外锥余弦」；
			两个 std140 数组各自连续，故分两段排布。
			*/
			float spot_block[64] = {};
			for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
			{
				//槽内基址（每个槽 4 个浮点）
				const std::size_t base = static_cast<std::size_t>(light) * 4;
				spot_block[base + 0] = impl->point_lights[light].spot_direction[0];
				spot_block[base + 1] = impl->point_lights[light].spot_direction[1];
				spot_block[base + 2] = impl->point_lights[light].spot_direction[2];
				spot_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
				//锥参数段起点 = 8 盏方向（32 浮点）之后
				spot_block[32 + base + 0] = impl->point_lights[light].spot_inner_cos;
				spot_block[32 + base + 1] = impl->point_lights[light].spot_outer_cos;
				//z = 逐光源衰减模式（0 = 跟随全局、1 = 线性、2 = 平方反比；第四十六轮）
				spot_block[32 + base + 2] = static_cast<float>(impl->point_lights[light].attenuation);
			}
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], spot_block, 256, 384))
			{
				logger.error("渲染器:: 场景聚光参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			/*
			写入初始雾参数（offset 640、32 字节；第三十八轮）
			前 16 字节 = 雾色 rgb ＋ 密度 w；后 16 字节 = 起始距离 x ＋ 结束距离 y ＋ 模式 z（第一百四十三轮）。
			*/
			float fog_block[8] = {
				impl->fog_color[0], impl->fog_color[1], impl->fog_color[2], impl->fog_density,
				impl->fog_start, impl->fog_end, static_cast<float>(impl->fog_mode), impl->fog_height_falloff,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], fog_block, 32, 640))
			{
				logger.error("渲染器:: 场景雾参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始环境光颜色（offset 672、16 字节；第四十轮）
			float ambient_block[4] = {
				impl->ambient_color[0], impl->ambient_color[1], impl->ambient_color[2], 0.0f,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], ambient_block, 16, 672))
			{
				logger.error("渲染器:: 场景环境光颜色写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始半球环境光（offset 688、16 字节；第四十七轮）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				impl->ambient_ground, 16, 688))
			{
				logger.error("渲染器:: 场景半球环境光写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始补光方向与强度（offset 704、16 字节；第四十七轮）
			float fill_dir_block[4] = {
				impl->fill_direction[0], impl->fill_direction[1], impl->fill_direction[2], impl->fill_intensity,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				fill_dir_block, 16, 704))
			{
				logger.error("渲染器:: 场景补光方向写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始补光颜色（offset 720、16 字节；第四十七轮；w = 逐光高光缩放，第五十轮）
			float fill_color_block[4] = {
				impl->fill_color[0], impl->fill_color[1], impl->fill_color[2], impl->fill_specular,
			};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				fill_color_block, 16, 720))
			{
				logger.error("渲染器:: 场景补光颜色写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始逐盏光泽度（offset 736、128 字节；第五十一轮；第六十二轮扩为 8 盏，std140 每盏占 16 字节）
			float shininess_block[32] = {};
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shininess_block, 128, 736))
			{
				logger.error("渲染器:: 场景逐盏光泽度写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始阴影块（offset 864、96 字节；第五十二轮：光源空间矩阵 64 ＋ 阴影参数 16；第五十三轮：阴影强度 4 ＋ 尾部圆整 12）
			float shadow_init_block[24] = {};
			//阴影强度默认 1（块内第 20 浮点 = offset 944）
			shadow_init_block[20] = 1.0f;
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				shadow_init_block, 96, 864))
			{
				logger.error("渲染器:: 场景阴影参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//写入初始 PBR-lite 参数（offset 960、16 字节；第六十四轮：默认关闭、粗糙度缩放 1）
			float pbr_init_block[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
				pbr_init_block, 16, 960))
			{
				logger.error("渲染器:: 场景 PBR 参数写入失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//把该帧槽 set0 绑定到对应 UBO
			if (!impl->pipeline.scene_bind(slot, impl->scene_ubos[slot]))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			//创建该帧槽的实例缓冲（主机可见、顶点缓冲用途；容量 = instance_capacity 个实例，一个实例 80 字节）
			if (!impl->memory.buffer_create(impl->instance_buffers[slot], impl->instance_allocations[slot],
				static_cast<VkDeviceSize>(impl->instance_capacity) * INSTANCE_STRIDE_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 实例缓冲创建失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
		}
		/*
		⑭ 纹理：记录内存与设备，生成内置默认白纹理（2×2 全白）并绑定到所有帧槽 set1，
		保证 descriptor_set_count = 2 时不存在未写入的描述符集。
		*/
		if (!impl->texture.build(impl->memory, impl->device.device_get()))
		{
			logger.error("渲染器:: 纹理模块初始化失败");
			shutdown();
			return false;
		}
		//2×2 全白像素（RGBA8 紧排）
		const uint8_t white_pixels[16] = {
			255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!impl->texture.texture_create_from_memory(impl->default_texture, white_pixels, 2, 2))
		{
			logger.error("渲染器:: 默认白纹理创建失败");
			shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			shutdown();
			return false;
		}
		//初始采样状态（逐绘制项采样集按此快照写入；第一百七十四轮）
		impl->bound_view = default_view;
		impl->bound_sampler = default_sampler;
		for (VkImageView& material_view : impl->material_views)
			material_view = default_view;
		//把默认纹理写入所有帧槽 set1（长命资源，一次写满）
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			if (!impl->pipeline.texture_bind(slot, default_sampler, default_view))
			{
				logger.error("渲染器:: 默认纹理绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
			/*
			材质贴图槽同步写入默认白纹理（第一百二十四轮；第一百七十轮合并为一次批量写）
			场景片元静态声明 binding 3 / 4 / 6 / 7 / 8，即使未使用材质也必须有有效绑定；
			白纹理使法线 / 粗糙度 / 自发光 / 遮蔽 / 金属度乘子恒为 1，故不影响默认画面。
			*/
			if (!impl->pipeline.material_textures_bind(slot, default_sampler, default_view, default_view,
				default_view, default_view, default_view))
			{
				logger.error("渲染器:: 默认材质贴图绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
		}
		/*
		内置纯白立方体（第一百二十五轮）
		场景片元静态声明 binding 5（samplerCube），故即使未启用天空盒也必须有有效绑定；
		1×1×6 面全白，且默认天空盒强度为 0，故不改变既有画面。
		*/
		const uint8_t white_cube_faces[6 * 4] = {
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!impl->texture.texture_create_cube(impl->default_sky_texture, white_cube_faces, 1, 1))
		{
			logger.error("渲染器:: 默认天空盒立方体创建失败");
			shutdown();
			return false;
		}
		//取默认立方体的视图与采样器（与默认白纹理共用同一共享采样器）
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_sky_texture, sky_view, sky_sampler))
		{
			logger.error("渲染器:: 默认天空盒立方体视图获取失败");
			shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
			if (!impl->pipeline.sky_bind(slot, sky_sampler, sky_view))
			{
				logger.error("渲染器:: 默认天空盒绑定失败（帧槽 {}）", slot);
				shutdown();
				return false;
			}
		//⑮ 网格：记录内存模块
		if (!impl->mesh.build(impl->memory))
		{
			logger.error("渲染器:: 网格模块初始化失败");
			shutdown();
			return false;
		}
		//⑯ 渲染目标：记录设备 / 图形队列与内存模块（离屏目标按需创建）
		if (!impl->render_target.build(impl->device.device_get(), impl->device.graphics_family_get(),
			impl->device.graphics_queue_get(), impl->memory))
		{
			logger.error("渲染器:: 渲染目标模块初始化失败");
			shutdown();
			return false;
		}
		//渲染目标默认采样数与交换链一致（离屏 / 后处理目标随 MSAA 建立多重采样附件）
		impl->render_target.sample_count_set(impl->msaa_samples);
		//⑰ 字体：记录纹理模块（字形图集经纹理模块上传与销毁）
		if (!impl->font.build(impl->texture))
		{
			logger.error("渲染器:: 字体模块初始化失败");
			shutdown();
			return false;
		}
		/*
		文字绘制宿主绑定（第四十七轮下沉）
		文字绘制子系统只持网格缓存与文字颜色；字形补烘之外的 Vulkan 侧动作
		（管线登记、管线 / 纹理绑定、网格创建与归还、绘制项登记、目标尺寸查询）经本组回调转交本门面。
		回调均为无捕获 lambda（转函数指针），以 this 作上下文；生命周期不超过 Renderer。
		*/
		{
			Text_Host text_host{};
			text_host.user = this;
			//确保文字管线就绪（懒登记：Alpha 混合档，与场景管线共享同一管线布局）
			text_host.pipeline_ensure = [](void* user) -> uint64_t
			{
				Renderer* self = static_cast<Renderer*>(user);
				//尚未初始化则无管线可登记
				if (self->impl == nullptr || !self->impl->initialized)
					return self->impl == nullptr ? 0 : self->impl->text.pipeline_get();
				//已登记则直接返回
				if (self->impl->text.pipeline_get() != 0)
					return self->impl->text.pipeline_get();
				uint64_t handle = 0;
				//登记失败保持 0（文字覆盖率写入按配置决定；仅 MSAA > 1× 时生效）
				if (!self->pipeline_load(handle, "文字.vert.spv", "文字.frag.spv", Blend_Mode::Alpha,
					self->impl->config.text_alpha_to_coverage))
					return 0;
				self->impl->text.pipeline_set(handle);
				return handle;
			};
			//绑定文字管线
			text_host.pipeline_bind = [](void* user, uint64_t pipeline)
			{
				static_cast<Renderer*>(user)->pipeline_bind_set(pipeline);
			};
			//绑定字形图集纹理
			text_host.texture_bind = [](void* user, uint64_t texture)
			{
				static_cast<Renderer*>(user)->texture_bind_set(texture);
			};
			//创建文字网格
			text_host.mesh_create = [](void* user, Mesh_Handle& mesh, const Mesh_Data& data) -> bool
			{
				return static_cast<Renderer*>(user)->impl->mesh.mesh_create(mesh, data);
			};
			//归还文字网格（网格模块未就绪时只丢弃句柄）
			text_host.mesh_destroy = [](void* user, Mesh_Handle mesh)
			{
				Renderer* self = static_cast<Renderer*>(user);
				//若网格模块仍有效
				if (self->impl != nullptr && self->impl->mesh.valid())
					self->impl->mesh.mesh_destroy(mesh);
			};
			//登记文字绘制项
			text_host.mesh_draw = [](void* user, Mesh_Handle mesh, const float* transform, const float* color) -> bool
			{
				return static_cast<Renderer*>(user)->mesh_draw(mesh, transform, color);
			};
			//取当前绘制目标尺寸（像素）
			text_host.extent_get = [](void* user, float& width, float& height)
			{
				Renderer* self = static_cast<Renderer*>(user);
				width = static_cast<float>(self->extent_width());
				height = static_cast<float>(self->extent_height());
			};
			//绑定字体模块与宿主回调
			impl->text.init(impl->font, text_host);
		}
		//⑱ 相机：设置宽高比（取当前交换链尺寸）
		{
			VkExtent2D extent = impl->swapchain.extent_get();
			//尺寸有效时刷新宽高比
			if (extent.width != 0 && extent.height != 0)
				impl->camera.aspect_set(static_cast<float>(extent.width) / static_cast<float>(extent.height));
		}

		/*
		⑲ UI 通道
		向上层提供 ImGui Vulkan 后端所需的实例 / 设备 / 队列 / 窗口 / 图像信息与动态渲染口径；
		绘制回调由上层经 ui_channel() 另行设置（渲染层不代管 UI）。
		*/
		impl->ui_channel.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->platform.window_get(), effective.frame_overlap, impl->swapchain.image_count_get(),
		impl->swapchain.format_get(), impl->swapchain.depth_format_get());
		/*
		把事件泵的 SDL 事件桥接到 UI 通道
		上层经 ui_channel()->event_callback_set 注册回调后，事件泵每帧逐个事件转交，
		供上层平台后端（imgui_impl_sdl3）处理键鼠与文本输入；未注册时该桥接为空操作。
		桥接不持有资源；解除见 shutdown。
		*/
		impl->platform.event_forward_set(
			[](const SDL_Event* event, void* user_data) {
				static_cast<Ui_Channel_Context*>(user_data)->event_dispatch(event);
			},
			&impl->ui_channel);

		//记录配置并标记初始化完成
		impl->config = effective;
		//静态合批开关（第一百二十七轮；默认开启）
		impl->static_batching = effective.static_batching;
		//SSAA 倍率生效（钳制 1..4；默认 1 保持既有画面逐字节不变）
		impl->ssaa_factor = effective.ssaa_factor < 1 ? 1 : (effective.ssaa_factor > 4 ? 4 : effective.ssaa_factor);
		impl->ssaa_extent_failed = false;
		//记录生效的 MSAA 采样数（已按设备支持钳制）
		impl->config.msaa_samples = impl->msaa_samples;
		//MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = effective.msaa_depth_resolve;
		impl->initialized = true;
		/*
		⑳ 阴影资源（第五十二轮；第五十四轮边长可配）
		阴影目标（颜色存打包深度，边长取 impl->shadow_size）＋ 阴影管线（与场景管线共享管线布局，
		光源空间矩阵经推送常量下发）；并把阴影图写入各帧槽场景采样集的 binding 1。
		默认关闭，故不改变既有画面。
		*/
		{
			/*
			① 阴影目标（边长取配置值，颜色存打包深度）
			颜色格式固定 R8G8B8A8_UNORM（第五十四轮修正）：交换链优先 sRGB，若沿用其格式，
			打包深度的字节会被 sRGB 编码 / 解码破坏，导致片元读回深度失真并误判为全遮蔽（自阴影全黑）。
			*/
			//阴影目标固定单采样（第九十六轮；与阴影管线一致，不随 MSAA 变化）
			if (!impl->render_target.target_create(impl->shadow_target, impl->shadow_size, impl->shadow_size,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				logger.error("渲染器:: 阴影目标创建失败");
				shutdown();
				return false;
			}
			//② 把阴影图写入各帧槽场景采样集的 binding 1（管线在首次开启阴影时懒登记）
			VkImage shadow_image = VK_NULL_HANDLE;
			VkImageView shadow_view = VK_NULL_HANDLE;
			VkImageView shadow_depth_view = VK_NULL_HANDLE;
			VkExtent2D shadow_extent{};
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			if (!impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view,
				shadow_depth_view, shadow_extent)
				|| !impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
			{
				logger.error("渲染器:: 阴影图绑定失败：资源查询失败");
				shutdown();
				return false;
			}
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
				//把阴影图写入该帧槽场景采样集的 binding 1
				if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, shadow_view))
				{
					logger.error("渲染器:: 阴影图绑定失败（帧槽 {}）", slot);
					shutdown();
					return false;
				}
		}
		logger.info("渲染器:: 初始化完成（校验层 {}）", impl->device.validation_get() ? "开" : "关");
		return true;
	}

	//关闭（幂等）
	void Renderer::shutdown(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return;
		/*
		先回收渲染工作线程（第一百七十七轮）
		并行任务均为同步调用（调用方阻塞到全部块完成），故 shutdown 时不存在在途任务；
		此处直接停止可保证后续任何模块销毁都不会被工作线程访问。
		*/
		impl->thread_pool.threads_stop();
		impl->render_threads = 0;
		//严格逆序销毁（各模块 destroy 幂等，可安全处理半初始化状态；§5.4 / §9）
		/*
		阴影资源（第五十二轮）
		须先于渲染目标与管线模块销毁：阴影管线为门面具名管线（着色器模块所有权在门面）。
		*/
		if (impl->initialized)
		{
			//归还阴影管线
			if (impl->shadow_pipeline != 0)
			{
				pipeline_unload(impl->shadow_pipeline);
				impl->shadow_pipeline = 0;
			}
			//归还阴影目标
			if (impl->shadow_target != 0)
			{
				render_target_destroy(impl->shadow_target);
				impl->shadow_target = 0;
			}
			//阴影图已失效（第五十五轮）
			impl->shadow_pass_ready = false;
			impl->shadow_signature = 0;
			impl->shadow_pass_count = 0;
		}
		impl->frame.destroy();
		//文本网格缓存依赖网格模块，须先归还网格（含管线句柄与文字颜色复位）
		impl->text.shutdown();
		//网格缓冲依赖内存模块，须先于 memory 销毁
		impl->mesh.destroy();
		//字体图集纹理依赖纹理模块，须先归还
		impl->font.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		impl->texture.destroy();
		//渲染目标依赖设备与内存，须先于两者销毁
		impl->render_target.destroy();
		impl->active_target = 0;
		//复位同帧后处理呈现路径与其资源句柄（全屏网格随网格模块销毁、后处理管线随管线模块销毁）
		impl->present_target = 0;
		impl->present_extra.clear();
		impl->post_chain.clear();
		impl->post_pipeline = 0;
		impl->fullscreen_mesh = 0;
		//复位 SSAA 状态（超采样目标已随渲染目标模块销毁，仅清句柄、倍率与失败标记）
		impl->ssaa_scene_target = 0;
		impl->ssaa_resolve_target = 0;
		impl->ssaa_extent_width = 0;
		impl->ssaa_extent_height = 0;
		impl->ssaa_target_factor = 0;
		impl->ssaa_pipeline = 0;
		impl->ssaa_factor = 1;
		impl->ssaa_extent_failed = false;
		//复位 MSAA 采样数（第九十六轮）
		impl->msaa_samples = 1;
		//复位 MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = true;
		//复位逐级泛光参数覆盖与描边色专设，并复位剔除观测
		for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
		{
			impl->post_stage_param_valid[stage] = false;
			impl->post_stage_color_valid[stage] = false;
		}
		impl->culled_items = 0;
		//剔除开关恢复默认（开启）
		impl->cull_enabled = true;
		//静态合批恢复默认（开启，第一百二十七轮）
		impl->static_batching = true;
		//逐物体 PBR 材质恢复默认（不覆盖全局，第六十八轮）
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
		//逐物体色调恢复默认（白色，第六十九轮）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
		//逐物体自发光恢复默认（0，第七十轮）
		impl->material_emissive = 0.0f;
		impl->material_normal_strength = 0.0f;
		//UV 旋转与 Alpha 裁剪恢复默认（0 = 不旋转 / 关闭；第一百八十三轮）
		impl->material_uv_rotation = 0.0f;
		impl->material_alpha_cutoff = 0.0f;
		//光照恢复默认（满照：朝 -Z、白色、强度 1、环境光 0）
		impl->light_direction[0] = 0.0f;
		impl->light_direction[1] = 0.0f;
		impl->light_direction[2] = -1.0f;
		impl->light_color[0] = 1.0f;
		impl->light_color[1] = 1.0f;
		impl->light_color[2] = 1.0f;
		impl->light_intensity = 1.0f;
		impl->light_ambient = 0.0f;
		//点光源与高光恢复默认（无点光源、方向光强度 1、高光关闭、光泽度 16）
		impl->point_light_count = 0;
		impl->specular_intensity = 0.0f;
		impl->specular_shininess = 16.0f;
		//复位距离衰减模式（第三十六轮：默认线性）
		impl->attenuation_mode = 0;
		//复位距离雾（第三十八轮：密度 0 = 关闭）
		impl->fog_color[0] = 0.05f;
		impl->fog_color[1] = 0.05f;
		impl->fog_color[2] = 0.10f;
		impl->fog_density = 0.0f;
		impl->fog_start = 1.0f;
		impl->fog_end = 10.0f;
		//复位雾模式（第一百四十三轮：默认线性区间）
		impl->fog_mode = 0;
		//复位雾高度衰减（第一百四十三轮：默认关闭）
		impl->fog_height_falloff = 0.0f;
		//复位环境光颜色（第四十轮：默认白）
		impl->ambient_color[0] = 1.0f;
		impl->ambient_color[1] = 1.0f;
		impl->ambient_color[2] = 1.0f;
		//半球环境光恢复默认（地面白、强度 0 = 关闭）
		impl->ambient_ground[0] = 1.0f;
		impl->ambient_ground[1] = 1.0f;
		impl->ambient_ground[2] = 1.0f;
		impl->ambient_ground[3] = 0.0f;
		//补光恢复默认（朝 -Z、白、强度 0 = 关闭）
		impl->fill_direction[0] = 0.0f;
		impl->fill_direction[1] = 0.0f;
		impl->fill_direction[2] = -1.0f;
		impl->fill_color[0] = 1.0f;
		impl->fill_color[1] = 1.0f;
		impl->fill_color[2] = 1.0f;
		impl->fill_intensity = 0.0f;
		//逐光高光缩放恢复默认（第五十轮：均为 1）
		impl->directional_specular = 1.0f;
		impl->fill_specular = 1.0f;
		impl->pending_push_constants.clear();
		//场景 UBO 依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->scene_ubos.size(); ++slot)
				//归还场景 UBO 与其内存
				impl->memory.buffer_destroy(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot]);
		impl->scene_ubos.clear();
		impl->scene_ubo_allocs.clear();
		//实例缓冲依赖内存模块，须先于 memory 销毁
		if (impl->memory.valid())
			for (size_t slot = 0; slot < impl->instance_buffers.size(); ++slot)
				//归还实例缓冲与其内存
				impl->memory.buffer_destroy(impl->instance_buffers[slot], impl->instance_allocations[slot]);
		impl->instance_buffers.clear();
		impl->instance_allocations.clear();
		//默认纹理句柄复位
		impl->default_texture = 0;
		//天空盒状态复位（第一百二十五轮；默认关闭、强度 0）
		impl->sky_enabled = false;
		impl->sky_texture = 0;
		impl->default_sky_texture = 0;
		impl->sky_pipeline = 0;
		impl->sky_ambient = 0.0f;
		impl->sky_specular = 0.0f;
		//二次元风格化状态复位（第一百七十四轮；默认关闭）
		impl->toon_steps = 0;
		impl->toon_rim = 0.0f;
		//暗部色调恢复默认（白色 = 乘性恒等；第一百七十八轮）
		for (std::size_t index = 0; index < 3; ++index)
			impl->toon_shadow_tint[index] = 1.0f;
		//卡通高光阈值复位（0 = 关闭；第一百七十九轮）
		impl->toon_specular = 0.0f;
		//反向外扩描边复位（宽度 0 = 关闭；第二百二十八轮）
		impl->outline_width = 0.0f;
		impl->outline_pipeline = 0;
		for (std::size_t index = 0; index < 3; ++index)
			impl->outline_color[index] = 0.0f;
		/*
		写回管线缓存（第一百零六轮；可选）
		在销毁管线缓存之前取出字节并写盘；失败仅告警，不影响关闭流程。
		*/
		if (impl->initialized && !impl->config.pipeline_cache_path.empty())
		{
			std::vector<uint8_t> cache_bytes;
			if (impl->pipeline.cache_get(cache_bytes))
			{
				std::ofstream cache_stream(path_from_utf8(impl->config.pipeline_cache_path),
					std::ios::binary | std::ios::trunc);
				if (cache_stream.is_open())
					cache_stream.write(reinterpret_cast<const char*>(cache_bytes.data()),
						static_cast<std::streamsize>(cache_bytes.size()));
			}
		}
		//管线依赖设备与内存，须先于两者销毁
		impl->pipeline.destroy();
		//着色器模块须先于着色器上下文复位
		if (impl->vert_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->vert_module);
			impl->vert_module = VK_NULL_HANDLE;
		}
		if (impl->frag_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(impl->frag_module);
			impl->frag_module = VK_NULL_HANDLE;
		}
		//具名管线的着色器模块（须先于着色器上下文复位）
		for (auto& record : impl->extra_pipelines)
		{
			//回收该管线的顶点着色器模块
			if (record.vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.vert_module);
			//回收该管线的片元着色器模块
			if (record.frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.frag_module);
		}
		impl->extra_pipelines.clear();
		//材质记录与按档缓存的共享具名管线清空（具名管线已随 extra_pipelines 释放）
		impl->materials.clear();
		impl->material_pipelines.clear();
		impl->bound_pipeline_request = 0;
		impl->shader.destroy();
		//交换链销毁连带销毁深度图像，须排在管线之后
		impl->swapchain.destroy();
		impl->memory.destroy();
		/*
		先平台后设备
		平台持有「借用」的实例句柄，销毁表面（vkDestroySurfaceKHR）要求实例仍在途；
		设备销毁会连带 vkDestroyInstance，故必须排在平台之后。
		*/
		//解除事件泵到 UI 通道的桥接（平台销毁后不应再有回调指向 UI 通道）
		impl->platform.event_forward_clear();
		impl->platform.destroy();
		impl->device.destroy();
		//清空待绘制项与其推送常量存储
		impl->pending_items.clear();
		impl->pending_instances.clear();
		//复位状态
		impl->ui_channel.destroy();
		impl->frame_count = 0;
		impl->initialized = false;
	}

	//全屏切换（第一百一十一轮）
	bool Renderer::window_fullscreen_set(bool fullscreen)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->platform.fullscreen_set(fullscreen);
	}

	//当前是否处于全屏（第一百一十一轮）
	bool Renderer::window_fullscreen(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->platform.fullscreen_get();
	}

	//窗口显示缩放比例（第一百一十二轮）
	float Renderer::display_scale(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->platform.display_scale_get();
	}

	//是否已初始化
	bool Renderer::valid(void) const
	{
		return impl != nullptr && impl->initialized;
	}

	//每帧推进（可选像素回读）
	bool Renderer::frame_render(uint8_t* readback_pixels, uint32_t readback_capacity,
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

		//① 泵事件
		impl->platform.events_pump();
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

		/*
		⑤a 光源空间矩阵（第五十二轮）
		无论阴影是否开启都先算好并写入 UBO，使开关切换可即时生效。
		以方向光传播方向构造正交视锥（覆盖以原点为中心、半径 12 的场景）；
		投影与相机同口径做 Y 翻转；方向与世界上方平行时改用 X 轴作上向量，避免 lookAt 退化。
		*/
		{
			glm::vec3 light_dir(impl->light_direction[0], impl->light_direction[1], impl->light_direction[2]);
			//方向退化时取默认传播方向
			if (glm::dot(light_dir, light_dir) < 1e-12f)
				light_dir = glm::vec3(0.0f, 0.0f, -1.0f);
			light_dir = glm::normalize(light_dir);
			//上向量与光向平行时改用 X 轴
			glm::vec3 up = glm::abs(light_dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			/*
			级联阴影矩阵（第一百二十一轮）
			单级：与引入级联前完全一致 —— 光源机位固定在场景中心的「逆光向」一侧，
			      正交半宽取 shadow_half_extent，近远取 shadow_near / shadow_far。
			双级：两片各为「以相机为中心的包围球」，半径分别为 split_distance 与 split × far_scale；
				      片元按与相机的距离选择，故与着色器的选择口径严格一致（不依赖视场角与宽高比）。
				三级（第一百四十六轮）：再追加一片，半径为 split × far_scale²，选择阈值再加 split × far_scale。
			*/
			if (impl->cascade_count >= 2 && impl->cascade_far_scale > 1.0f)
			{
				//相机世界位置（各片包围球的共同中心）
				float camera_position[3] = {};
				impl->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				//级联总数（2 或 3；第一百四十六轮）
				const uint32_t cascade_total = impl->cascade_count == 3 ? 3u : 2u;
				for (uint32_t cascade = 0; cascade < cascade_total; ++cascade)
				{
					/*
					本片包围球半径
					第 0 片 = 切换距离；其后每片再乘一次远区倍数（三级时第 2 片为 split × far_scale²）。
					*/
					float radius = impl->cascade_split_distance;
					for (uint32_t level = 0; level < cascade; ++level)
						radius *= impl->cascade_far_scale;
					//光源机位：沿逆光向退到「球半径 + 近平面」处，使整球都落在近平面之后
					glm::vec3 eye = center - light_dir * (radius + impl->shadow_near);
					glm::mat4 light_view = glm::lookAt(eye, center, up);
					//深度跨度：球直径 + 两端近平面余量
					float depth_far = 2.0f * radius + 2.0f * impl->shadow_near;
					glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, impl->shadow_near, depth_far);
					proj[1][1] *= -1.0f;
					glm::mat4 vp = proj * light_view;
					//按片序写入对应矩阵（第四片不存在，级联数上限为 3）
					float* target_matrix = cascade == 0 ? impl->light_view_proj
						: (cascade == 1 ? impl->light_view_proj_far : impl->light_view_proj_far2);
					std::memcpy(target_matrix, glm::value_ptr(vp), sizeof(float) * 16);
					//记录本片深度跨度（着色器法线偏移换算用）
					impl->cascade_depth_span[cascade] = depth_far - impl->shadow_near;
				}
			}
			else
			{
				//光源机位位于场景中心的「逆光向」一侧
				glm::vec3 eye = -light_dir * 20.0f;
				glm::mat4 light_view = glm::lookAt(eye, glm::vec3(0.0f), up);
				//正交范围可配（第一百零七轮；默认 12 / 1 / 60 与旧版一致）
				glm::mat4 light_proj = glm::ortho(-impl->shadow_half_extent, impl->shadow_half_extent,
					-impl->shadow_half_extent, impl->shadow_half_extent, impl->shadow_near, impl->shadow_far);
				light_proj[1][1] *= -1.0f;
				glm::mat4 light_vp = light_proj * light_view;
				std::memcpy(impl->light_view_proj, glm::value_ptr(light_vp), sizeof(float) * 16);
				//单级：远区矩阵为零，深度跨度固定 59（与引入级联前一致）
				std::memset(impl->light_view_proj_far, 0, sizeof(impl->light_view_proj_far));
				std::memset(impl->light_view_proj_far2, 0, sizeof(impl->light_view_proj_far2));
				impl->cascade_depth_span[0] = 59.0f;
				impl->cascade_depth_span[1] = 59.0f;
				impl->cascade_depth_span[2] = 59.0f;
			}
		}
		/*
		⑤b 阴影 pass（第五十二轮；第五十五轮按需重绘）
		以光源视角把「参与投影的几何」渲染进阴影目标（RGBA8 打包深度）；
		该 pass 走独立的离屏提交（推进帧槽），故必须在取主 pass 帧槽之前完成；
		随后等尽在途帧，保证主 pass 采样时阴影图已写完。
		若本帧输入签名与上一帧完全一致则跳过整个 pass（阴影图原样复用），静态场景省一次离屏提交与等待。
		*/
		if (impl->shadow_enabled && impl->shadow_pipeline != 0 && impl->shadow_target != 0
			&& !impl->pending_items.empty())
		{
			//本帧参与投影的项数
			std::size_t shadow_item_count = 0;
			for (const auto& item : impl->pending_items)
			{
				//跳过文字等非投影项
				if (item.cast_shadow)
					++shadow_item_count;
			}
			//本帧阴影输入签名（实例矩阵按 float 个数计；实例存储为连续 20 浮点数组）
			const float* instance_floats = impl->pending_instances.empty()
				? nullptr : impl->pending_instances.front().data();
			uint64_t shadow_signature = shadow_signature_build(impl->light_view_proj,
				impl->light_view_proj_far, impl->light_view_proj_far2, instance_floats,
				impl->pending_instances.size() * INSTANCE_FLOAT_COUNT,
				shadow_item_count, impl->shadow_size, impl->cascade_count);
			//输入与上一帧完全一致且阴影图可用 → 跳过重绘
			if (shadow_item_count != 0 && impl->shadow_pass_ready && impl->shadow_signature == shadow_signature)
			{
				//阴影图复用，主 pass 直接采样
				++impl->shadow_reuses;
				impl->shadow_last_redrawn = 0;
			}
			else if (shadow_item_count != 0)
			{
				//阴影 pass 使用的帧槽（render_to_target 内部同样推进帧计数）
				uint32_t shadow_slot = impl->frame.slot_index_get();
				//等待该槽上一轮提交完成，再写实例缓冲
				if (!impl->frame.slot_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待帧槽失败");
					return false;
				}
				//写入该槽实例缓冲（与主 pass 使用同一批模型矩阵）
				if (!impl->pending_instances.empty())
				{
					VkDeviceSize instance_bytes = static_cast<VkDeviceSize>(impl->pending_instances.size()) * INSTANCE_STRIDE_BYTES;
					if (!impl->memory.buffer_write(impl->instance_buffers[shadow_slot],
						impl->instance_allocations[shadow_slot], impl->pending_instances.data(), instance_bytes, 0))
					{
						logger.error("渲染器:: 阴影 pass 实例缓冲写入失败");
						return false;
					}
					/*
					使该帧槽的实例缓存失效（第六十一轮）
					阴影 pass 直接整块写入该槽缓冲，绕过了按需写入的缓存；
					主 pass 之后若复用该槽，须以完整写入重建缓存，避免按旧缓存跳过必要更新。
					*/
					if (impl->instance_block_cached.size() > shadow_slot)
						impl->instance_block_cached[shadow_slot] = 0;
					//累计阴影 pass 的实例上传字节
					impl->instance_upload_bytes += static_cast<uint64_t>(instance_bytes);
				}
				//组装阴影绘制项（仅投影几何；改用阴影管线并以推送常量下发光源空间矩阵）
				//阴影列表复用缓冲（第一百八十二轮；避免逐帧新建向量）
				std::vector<Frame_Render_Item>& shadow_items = impl->shadow_items_scratch;
				shadow_items.clear();
				shadow_items.reserve(shadow_item_count);
				VkPipeline shadow_pipeline = impl->pipeline.pipeline_by_handle_get(impl->shadow_pipeline);
				VkPipelineLayout shadow_layout = impl->pipeline.layout_get();
				for (const auto& item : impl->pending_items)
				{
					//文字等非投影项跳过
					if (!item.cast_shadow)
						continue;
					Frame_Render_Item copy = item;
					copy.pipeline = shadow_pipeline;
					copy.layout = shadow_layout;
					/*
					实例缓冲指向阴影 pass 刚写入的槽
					pending_items 中的 instance_buffer 恒为空（主 pass 稍后才填装），
					若此处不指定，顶点属性 binding 1（实例模型矩阵）无缓冲，实例矩阵退化会让
					所有几何塌到原点、阴影图只剩一点，表现为整幅画面全遮蔽。
					*/
					copy.instance_buffer = impl->instance_buffers[shadow_slot];
					copy.scene_set = VK_NULL_HANDLE;
					copy.sampler_set = VK_NULL_HANDLE;
					copy.descriptor_set_count = 0;
					copy.push_constants = impl->light_view_proj;
					copy.push_constant_size = 64;
					copy.push_constant_offset = 0;
					shadow_items.push_back(copy);
				}
				//阴影目标附件
				Frame_Render_Target shadow_target{};
				if (!impl->render_target.target_get(impl->shadow_target, shadow_target.color_image,
					shadow_target.color_view, shadow_target.depth_view, shadow_target.extent))
				{
					logger.error("渲染器:: 阴影目标附件查询失败");
					return false;
				}
				/*
				阴影目标格式（第二百一十轮）
				并行录制需在次级命令缓冲的继承信息里声明附件的颜色 / 深度格式与采样数，
				故此处补齐（阴影颜色恒为 RGBA8 UNORM，深度取交换链深度格式，恒单采样）。
				*/
				shadow_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
				shadow_target.depth_format = impl->swapchain.depth_format_get();
				shadow_target.sample_count = 1;
				//阴影图清空为全 0（打包深度 0 = 最远）
				const float shadow_clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				/*
				按级联分片绘制（第一百二十一轮）
				单级：整幅目标一次绘制，行为与引入级联前逐字节一致；
				双级：目标沿 X 切成左右两片，各占「边长的一半」，第 1 片先清屏整幅、第 2 片以「保留」写入。
				*/
				uint32_t cascade_total = impl->cascade_count == 3 ? 3u : (impl->cascade_count == 2 ? 2u : 1u);
				uint32_t tile_size = cascade_total > 1 ? (impl->shadow_size / cascade_total) : impl->shadow_size;
				/*
				阴影列表合批（第一百二十九轮；第二百一十七轮移到分片循环外）
				各分片的投影项完全相同、仅推送常量（光源矩阵）不同，而合批判据只比较项内静态字段
				与推送常量的**内容**；此处各类项尚未改写的推送常量完全一致，故合批结果与逐片合批等价，
				单级 / 多级级联下都只需合并一次，省去重复扫描。
				*/
				if (impl->static_batching)
					impl->frame_merged_items += batch_adjacent_items(shadow_items, impl->batch_scratch);
				for (uint32_t cascade = 0; cascade < cascade_total; ++cascade)
				{
					//本片目标（多级时收缩视口到对应方形小格；X 按片序偏移、Y 恒为顶格）
					Frame_Render_Target cascade_target = shadow_target;
					if (cascade_total > 1)
					{
						uint32_t tile_origin = cascade * tile_size;
						cascade_target.viewport_rect.offset = { static_cast<int32_t>(tile_origin), 0 };
						cascade_target.viewport_rect.extent = { tile_size, tile_size };
					}
					//本片使用的光源空间矩阵
					const float* cascade_matrix = cascade == 0 ? impl->light_view_proj
						: (cascade == 1 ? impl->light_view_proj_far : impl->light_view_proj_far2);
					for (auto& shadow_item : shadow_items)
						shadow_item.push_constants = cascade_matrix;
					//第 1 片负责清屏，后续片保留已有内容
					const float* cascade_clear = cascade == 0 ? shadow_clear : nullptr;
					if (!impl->frame.render_to_target(cascade_target, shadow_items.data(),
						static_cast<uint32_t>(shadow_items.size()), cascade_clear, nullptr, nullptr, true))
					{
						logger.error("渲染器:: 阴影 pass 提交失败（级联 {}）", cascade);
						return false;
					}
				}
				//等尽在途帧：确保阴影图写入完成后再由主 pass 采样
				if (!impl->frame.frames_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待完成失败");
					return false;
				}
				//记录阴影 pass 的并行段数（最后一次分片录制的结果；第二百一十二轮）
				impl->last_shadow_parallel_segments = impl->frame.last_parallel_segments_get();
				//读回阴影 pass 的 GPU 耗时（第一百二十八轮；此时该提交已完成）
				impl->shadow_gpu_ms = impl->frame.shadow_gpu_ms_read();
				//记录本帧签名并标记阴影图可复用
				impl->shadow_signature = shadow_signature;
				impl->shadow_pass_ready = true;
				++impl->shadow_pass_count;
				//重绘观测（第一百四十五轮）
				++impl->shadow_redraws;
				impl->last_shadow_items = static_cast<uint32_t>(shadow_item_count);
				impl->shadow_last_redrawn = 1;
			}
		}

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
		float view_proj[16] = {};
		impl->camera.view_proj_get(view_proj);
		//推进文字子系统帧时钟并归还已淘汰且无在途引用的网格（第四十八轮）
		impl->text.frame_begin(impl->config.frame_overlap);
		/*
		场景 UBO 整块组装（1184 字节 = 296 浮点；第六十二轮 960、第六十四轮追加 PBR-lite 参数；
		第一百二十一轮追加远区级联矩阵与级联参数；第一百二十五轮追加天空盒参数；
		第一百四十六轮追加第三级级联矩阵与第三级参数；第一百七十九轮追加卡通高光参数）
		各段浮点偏移与 std140 契约一致：0 视图投影、16 光照块、32 点光源位置、64 点光源颜色、
		96 聚光方向、128 聚光锥参数、160 雾、168 环境光颜色、172 半球环境光、176 补光方向与强度、
		180 补光颜色、184 逐盏光泽度（8 盏 × 4）、216 光源空间矩阵、232 阴影参数、236 阴影强度、
		240 PBR-lite 参数（x 启用 / y 粗糙度缩放）、244 远区级联矩阵、260 级联参数、264 相机近远平面、
		268 天空盒参数（x 启用 / y 漫反射强度 / z 镜面强度）、272 第三级级联矩阵、288 第三级参数、
		292 卡通高光参数（x 阈值）。
		*/
		float scene_block[Impl::SCENE_BLOCK_FLOATS] = {};
		//视图 × 投影矩阵（列主序 16 浮点）
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[index] = view_proj[index];
		//光照公共块：环境光 / 高光强度 / 光泽度 / 点光源数量
		scene_block[16] = impl->light_ambient;
		scene_block[17] = impl->specular_intensity;
		scene_block[18] = impl->specular_shininess;
		scene_block[19] = static_cast<float>(impl->point_light_count);
		//方向光方向 / 强度 / 颜色 / 衰减模式
		scene_block[20] = impl->light_direction[0];
		scene_block[21] = impl->light_direction[1];
		scene_block[22] = impl->light_direction[2];
		scene_block[23] = impl->light_intensity;
		scene_block[24] = impl->light_color[0];
		scene_block[25] = impl->light_color[1];
		scene_block[26] = impl->light_color[2];
		scene_block[27] = static_cast<float>(impl->attenuation_mode);
		//相机世界位置（镜面高光的视线方向来源）；w = 方向光逐光高光缩放（第五十轮）
		float camera_position[3] = {};
		impl->camera.position_get(camera_position);
		scene_block[28] = camera_position[0];
		scene_block[29] = camera_position[1];
		scene_block[30] = camera_position[2];
		scene_block[31] = impl->directional_specular;
		//点光源：先排全部「位置 ＋ 半径」，再排全部「颜色 ＋ 强度」（与着色器两个 std140 数组一致）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
		{
			const std::size_t base = 32 + static_cast<std::size_t>(light) * 4;
			scene_block[base + 0] = impl->point_lights[light].position[0];
			scene_block[base + 1] = impl->point_lights[light].position[1];
			scene_block[base + 2] = impl->point_lights[light].position[2];
			scene_block[base + 3] = impl->point_lights[light].radius;
			//颜色段起点 = 8 盏位置（32 浮点）之后
			scene_block[base + 32 + 0] = impl->point_lights[light].color[0];
			scene_block[base + 32 + 1] = impl->point_lights[light].color[1];
			scene_block[base + 32 + 2] = impl->point_lights[light].color[2];
			scene_block[base + 32 + 3] = impl->point_lights[light].intensity;
		}
		//聚光参数：前 32 浮点为「方向 ＋ 有效标志」（8 盏），后 32 浮点为「内锥余弦 ＋ 外锥余弦 ＋ 逐光源衰减模式 ＋ 逐盏高光缩放」
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
		{
			const std::size_t base = 96 + static_cast<std::size_t>(light) * 4;
			scene_block[base + 0] = impl->point_lights[light].spot_direction[0];
			scene_block[base + 1] = impl->point_lights[light].spot_direction[1];
			scene_block[base + 2] = impl->point_lights[light].spot_direction[2];
			scene_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
			//锥参数段起点 = 8 盏方向（32 浮点）之后
			scene_block[base + 32 + 0] = impl->point_lights[light].spot_inner_cos;
			scene_block[base + 32 + 1] = impl->point_lights[light].spot_outer_cos;
			scene_block[base + 32 + 2] = static_cast<float>(impl->point_lights[light].attenuation);
			//w = 逐盏高光缩放（第四十九轮）
			scene_block[base + 32 + 3] = impl->point_lights[light].specular_scale;
		}
		//雾参数（雾色 ＋ 密度 ＋ 区间；第六十二轮起点后移 64 浮点）
		scene_block[160] = impl->fog_color[0];
		scene_block[161] = impl->fog_color[1];
		scene_block[162] = impl->fog_color[2];
		scene_block[163] = impl->fog_density;
		scene_block[164] = impl->fog_start;
		scene_block[165] = impl->fog_end;
		//雾模式（第一百四十三轮：0 = 线性区间、1 = 指数平方）
		scene_block[166] = static_cast<float>(impl->fog_mode);
		//雾高度衰减系数（第一百四十三轮）
		scene_block[167] = impl->fog_height_falloff;
		//环境光颜色（第四十轮）
		scene_block[168] = impl->ambient_color[0];
		scene_block[169] = impl->ambient_color[1];
		scene_block[170] = impl->ambient_color[2];
		//半球环境光（第四十七轮：rgb = 地面色、w = 混合强度）
		for (std::size_t index = 0; index < 4; ++index)
			scene_block[172 + index] = impl->ambient_ground[index];
		//补光方向与强度（第四十七轮）
		scene_block[176] = impl->fill_direction[0];
		scene_block[177] = impl->fill_direction[1];
		scene_block[178] = impl->fill_direction[2];
		scene_block[179] = impl->fill_intensity;
		//补光颜色（第四十七轮）＋ 逐光高光缩放（第五十轮；w）
		scene_block[180] = impl->fill_color[0];
		scene_block[181] = impl->fill_color[1];
		scene_block[182] = impl->fill_color[2];
		scene_block[183] = impl->fill_specular;
		//逐盏光泽度（第五十一轮；第六十二轮扩为 8 盏，std140 float[8] 每盏占 16 字节 → 偏移 184 + light × 4）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_MAX; ++light)
			scene_block[184 + static_cast<std::size_t>(light) * 4] = impl->point_lights[light].shininess;
		/*
		光源空间矩阵沿用 ⑤a 结果（第五十二轮）
		写入 UBO 供场景片元解包比较；阴影关闭时字段保留但不参与计算。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[216 + index] = impl->light_view_proj[index];
		//阴影参数（第五十二轮：x = 启用、y = 偏置、z = 阴影图边长；第五十三轮 w = 软度）
		scene_block[232] = impl->shadow_enabled ? 1.0f : 0.0f;
		scene_block[233] = impl->shadow_bias;
		scene_block[234] = static_cast<float>(impl->shadow_size);
		scene_block[235] = impl->shadow_softness;
		//阴影强度（第五十三轮：0 = 阴影无效、1 = 全遮蔽）
		scene_block[236] = impl->shadow_strength;
		//PBR-lite 参数（第六十四轮：x = 启用、y = 粗糙度缩放；第六十五轮 z = 金属度）
		scene_block[240] = impl->pbr_enabled ? 1.0f : 0.0f;
		scene_block[241] = impl->pbr_roughness;
		scene_block[242] = impl->pbr_metallic;
		//二次元风格化色阶数（第一百七十四轮；w 槽；0 = 关闭）
		scene_block[243] = static_cast<float>(impl->toon_steps);
		/*
		级联阴影参数（第一百二十一轮；第一百四十六轮扩为三级）
		244..259 为远区级联矩阵（单级时为零矩阵）；260..263 为级联参数：
		x = 级联数（1 / 2 / 3）、y = 第 1 个切换距离（与相机的世界距离）、
		z = 第 1 片深度跨度、w = 第 2 片深度跨度（供着色器换算法线偏移；单级时均为 59）。
		272..287 为第三级矩阵、288..291 为第三级参数：
		x = 第 2 个切换距离（= 切换距离 × 远区倍数）、y = 第 3 片深度跨度。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[244 + index] = impl->light_view_proj_far[index];
		scene_block[260] = static_cast<float>(impl->cascade_count);
		scene_block[261] = impl->cascade_split_distance;
		scene_block[262] = impl->cascade_depth_span[0];
		scene_block[263] = impl->cascade_depth_span[1];
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[272 + index] = impl->light_view_proj_far2[index];
		scene_block[288] = impl->cascade_split_distance * impl->cascade_far_scale;
		scene_block[289] = impl->cascade_depth_span[2];
		//级联接缝淡化带宽比例（第一百四十八轮；z 槽）
		scene_block[290] = impl->cascade_blend;
		/*
		相机近远平面（第一百二十三轮）
		264 = 近平面、265 = 远平面（世界单位）；供景深片元把深度缓冲换算成以相机为原点的距离。
		*/
		float clip_near = 0.0f;
		float clip_far = 0.0f;
		impl->camera.clip_get(&clip_near, &clip_far);
		scene_block[264] = clip_near;
		scene_block[265] = clip_far;
		/*
		二次元暗部色调（第一百七十八轮；复用三个保留分量）
		266 = 红、267 = 绿（camera_range.zw），291 = 蓝（cascade_params2.w）；
		默认 (1,1,1) 时着色器乘性恒等，画面与引入前逐字节一致。
		*/
		scene_block[266] = impl->toon_shadow_tint[0];
		scene_block[267] = impl->toon_shadow_tint[1];
		/*
		天空盒参数（第一百二十五轮）
		268 = 启用（> 0.5 生效）、269 = 漫反射（环境）强度、270 = 镜面强度、271 保留；
		未启用时着色器整段跳过，画面与引入前逐字节一致。
		*/
		scene_block[268] = impl->sky_enabled ? 1.0f : 0.0f;
		scene_block[269] = impl->sky_ambient;
		scene_block[270] = impl->sky_specular;
		//二次元风格化边缘光强度（第一百七十四轮；w 槽；0 = 关闭）
		scene_block[271] = impl->toon_rim;
		//二次元暗部色调蓝分量（第一百七十八轮；w 槽；与 266 / 267 共同构成 RGB）
		scene_block[291] = impl->toon_shadow_tint[2];
		/*
		卡通高光参数（第一百七十九轮；x = 阈值 0..1、0 = 关闭；yzw 保留）
		场景片元按「高光亮度 ≥ 阈值」二值化高光，形成硬边高光块；阈值为 0 时整段短路。
		*/
		scene_block[292] = impl->toon_specular;
		/*
		场景 UBO 按需写入（第六十轮）
		逐帧槽缓存上一次写入的全部浮点（296），按 16 字节（4 浮点）单元比较；
		连续变化单元合并为一段，仅对变化区间调用 buffer_write；
		首次访问某帧槽（缓存未建立）仍整块写入 1184 字节，保证内容完整。
		写入内容与整块写逐字节一致，故画面不变。
		*/
		const std::size_t scene_slot_count = impl->scene_ubos.size();
		//若缓存规模与帧槽数不一致则重建（帧槽数由在途帧数决定，运行期恒定）
		if (impl->scene_block_cache.size() != scene_slot_count * Impl::SCENE_BLOCK_FLOATS)
		{
			impl->scene_block_cache.assign(scene_slot_count * Impl::SCENE_BLOCK_FLOATS, 0.0f);
			impl->scene_block_cached.assign(scene_slot_count, 0);
		}
		float* slot_cache = impl->scene_block_cache.data() + static_cast<std::size_t>(slot) * Impl::SCENE_BLOCK_FLOATS;
		//本帧实际上传字节数
		std::size_t uploaded_bytes = 0;
		//若该帧槽尚未建立缓存
		if (impl->scene_block_cached[slot] == 0)
		{
			//整块写入（与既有行为一致）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], scene_block,
				Impl::SCENE_BLOCK_BYTES, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				return false;
			}
			uploaded_bytes = Impl::SCENE_BLOCK_BYTES;
		}
		else
		{
			//比较单元：4 浮点 = 16 字节（std140 vec4 对齐）
			constexpr std::size_t UNIT_FLOATS = 4;
			constexpr std::size_t UNIT_BYTES = 16;
			constexpr std::size_t UNIT_COUNT = Impl::SCENE_BLOCK_FLOATS / UNIT_FLOATS;
			//当前变化段的起始单元与是否处于段内
			std::size_t run_start = 0;
			bool in_run = false;
			//逐单元扫描并合并连续变化区间
			for (std::size_t unit = 0; unit < UNIT_COUNT; ++unit)
			{
				//该单元是否有任一浮点变化（非数视为变化，保证安全上传）
				bool changed = false;
				for (std::size_t offset = 0; offset < UNIT_FLOATS; ++offset)
					if (scene_block[unit * UNIT_FLOATS + offset] != slot_cache[unit * UNIT_FLOATS + offset])
						changed = true;
				//若变化段遇到未变化单元则收束并写入
				if (!changed && in_run)
				{
					const std::size_t segment_bytes = (unit - run_start) * UNIT_BYTES;
					if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
						scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
					{
						logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
					in_run = false;
				}
				//若未在段内且该单元变化则开启新段
				if (changed && !in_run)
				{
					run_start = unit;
					in_run = true;
				}
			}
			//收尾：最后一段延伸到末尾
			if (in_run)
			{
				const std::size_t segment_bytes = (UNIT_COUNT - run_start) * UNIT_BYTES;
				if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
					scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
				{
					logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes += segment_bytes;
			}
		}
		//更新该帧槽缓存（与已写入内容一致；第一百七十九轮起 296 浮点）
		for (std::size_t index = 0; index < Impl::SCENE_BLOCK_FLOATS; ++index)
			slot_cache[index] = scene_block[index];
		impl->scene_block_cached[slot] = 1;
		//累计上传字节（观测用）
		impl->scene_upload_bytes += uploaded_bytes;
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

		/*
		为本帧待绘制项填装描述符集与实例缓冲句柄
		逐绘制项采样集（第一百七十四轮）：每项按登记时的纹理快照写一条独立 set1，
		使同帧内不同物体（含文字图集）各自采样自己的纹理；超出预留条数的项回落共享集。
		*/
		{
			//默认采样资源（回落用）
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
			//阴影图视图（未建立阴影目标时以默认白纹理占位）
			VkImageView shadow_view = default_view;
			if (impl->shadow_target != 0)
			{
				VkImage shadow_image = VK_NULL_HANDLE;
				VkImageView shadow_depth_view = VK_NULL_HANDLE;
				VkExtent2D shadow_extent{};
				impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view,
					shadow_depth_view, shadow_extent);
			}
			//天空盒立方体视图（未设置时用内置纯白立方体）
			VkImageView sky_view = VK_NULL_HANDLE;
			VkSampler sky_sampler = VK_NULL_HANDLE;
			const uint64_t sky_texture = impl->sky_texture != 0 ? impl->sky_texture : impl->default_sky_texture;
			impl->texture.texture_bind_get(sky_texture, sky_view, sky_sampler);
			//写逐绘制项集前等尽在途帧（这些集会被下一轮同槽帧复用）
			impl->frame.frames_wait();
			//本帧逐绘制项集游标
			uint32_t draw_index = 0;
			/*
			第一遍：填装公共字段并收集采样快照（第二百二十一轮：批量写入）
			条件与逐项写入完全一致（有采样快照且预留未用尽），只是把「写哪一条集」延后到一次批量提交。
			*/
			impl->draw_set_inputs.clear();
			for (auto& item : impl->pending_items)
			{
				//本帧槽的实例缓冲（模型矩阵来源）
				item.instance_buffer = impl->instance_buffers[slot];
				//本帧槽的场景 UBO 描述符集（set0）
				item.scene_set = impl->pipeline.scene_set_get(slot);
				//默认回落共享采样器集（set1）
				item.sampler_set = impl->pipeline.sampler_set_get(slot);
				item.descriptor_set_count = 2;
				//若该项有采样快照且预留未用尽则收集待写快照
				if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE
					&& draw_index < Pipeline_Context::DRAW_SET_PER_SLOT)
				{
					Pipeline_Context::Draw_Set_Views views{};
					views.sampler = item.draw_sampler;
					views.albedo = item.draw_albedo;
					views.normal = item.draw_normal != VK_NULL_HANDLE ? item.draw_normal : default_view;
					views.roughness = item.draw_roughness != VK_NULL_HANDLE ? item.draw_roughness : default_view;
					views.emissive = item.draw_emissive != VK_NULL_HANDLE ? item.draw_emissive : default_view;
					views.ao = item.draw_ao != VK_NULL_HANDLE ? item.draw_ao : default_view;
					views.metallic = item.draw_metallic != VK_NULL_HANDLE ? item.draw_metallic : default_view;
					views.shadow = shadow_view != VK_NULL_HANDLE ? shadow_view : default_view;
					views.sky = sky_view != VK_NULL_HANDLE ? sky_view : default_view;
					impl->draw_set_inputs.push_back(views);
					++draw_index;
				}
			}
			//一次写入本帧全部逐绘制项采样集
			const uint32_t draw_set_written = static_cast<uint32_t>(impl->draw_set_inputs.size());
			if (draw_set_written > 0)
				impl->pipeline.draw_sets_bind_batch(slot, impl->draw_set_inputs.data(), draw_set_written);
			/*
			第二遍：回填专属采样集句柄
			收集顺序与逐项写入一致（同一批集下标），故只有前 draw_set_written 项使用专属集，其余回落共享集。
			*/
			uint32_t assign_cursor = 0;
			for (auto& item : impl->pending_items)
			{
				if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE
					&& assign_cursor < draw_set_written)
				{
					item.sampler_set = impl->pipeline.draw_set_get(slot, assign_cursor);
					++assign_cursor;
				}
			}
		}
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
				std::array<float, 28>& sky_push = impl->pending_push_constants.back();
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
					//场景 pass ＋ 降采样 ＋ 默认后处理 ＋ 呈现
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(), 2,
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr))
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
				VkImageView post_views[Impl::PRESENT_TARGET_MAX] = {};
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
					if (index < Impl::POST_CHAIN_MAX_STAGES && impl->post_stage_param_valid[index])
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
					if (index < Impl::POST_CHAIN_MAX_STAGES && impl->post_stage_color_valid[index])
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
					post_items.push_back(post_item);
				}
				//若各级绘制项齐备
				if (items_ready)
				{
					offscreen = true;
					//场景 pass ＋ N 级后处理 pass（目标轮转）＋ 呈现（单级路径即本函数 N = 1 的特例）
					if (!impl->frame.render_chain_present(impl->swapchain, targets.data(),
						static_cast<uint32_t>(targets.size()),
						impl->pending_items.data(), static_cast<uint32_t>(impl->pending_items.size()),
						post_items.data(), static_cast<uint32_t>(post_items.size()),
						clear_color, rebuild_needed, readback_ptr))
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
				&& impl->render_target.target_msaa_get(impl->active_target, target.msaa_color_image,
					target.msaa_color_view, target.msaa_depth_view, target.sample_count))
			{
				//并行录制格式（第二百轮；离屏目标与交换链同格式）
				target.color_format = impl->swapchain.format_get();
				target.depth_format = impl->swapchain.depth_format_get();
				offscreen = true;
				//渲染到离屏目标（不取交换链图像、不呈现）
				if (!impl->frame.render_to_target(target, impl->pending_items.data(),
					static_cast<uint32_t>(impl->pending_items.size()), clear_color,
					impl->ui_channel.draw_callback_get(), readback_ptr))
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
			if (!impl->frame.render(impl->swapchain, impl->pending_items.data(),
				static_cast<uint32_t>(impl->pending_items.size()), rebuild_needed, clear_color,
				impl->ui_channel.draw_callback_get(), readback_ptr))
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

	//窗口是否请求退出
	bool Renderer::close_requested(void) const
	{
		//若实现体未分配则视为未请求退出
		if (impl == nullptr)
			return false;
		return impl->platform.close_requested();
	}

	//已呈现帧数
	uint64_t Renderer::frame_count(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->frame_count;
	}

	//等待全部在途帧完成（第二百一十八轮）
	void Renderer::frame_wait_idle(void)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//等尽全部帧槽的完成栅栏（与门面卸载路径同一口径）
		impl->frame.frames_wait();
	}

	//清零累计帧统计（第九十一轮）
	void Renderer::frame_stats_reset(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//帧模块累计量归零
		impl->frame.totals_reset();
		//门面自身累计量归零（帧数 / 剔除数 / 按需上传字节）
		impl->frame_count = 0;
		impl->culled_items = 0;
		impl->culled_instances = 0;
		impl->scene_upload_bytes = 0;
		impl->instance_upload_bytes = 0;
		//阴影重绘 / 复用计数归零（第一百四十五轮；最近一次量保持不变）
		impl->shadow_redraws = 0;
		impl->shadow_reuses = 0;
	}

	//取帧统计快照
	Frame_Stats Renderer::frame_stats_get(void) const
	{
		//默认全零
		Frame_Stats stats{};
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return stats;
		//已推进帧数取自门面自身计数（与 frame_count() 同口径）
		stats.frames = impl->frame_count;
		//帧耗时与绘制 / 实例计数取自帧模块（主机侧录制统计）
		stats.frame_ms = impl->frame.last_frame_ms_get();
		//主机侧分段（第一百三十三轮；录制 / 提交 / 呈现）
		stats.host_record_ms = impl->frame.last_host_record_ms_get();
		stats.host_submit_ms = impl->frame.last_host_submit_ms_get();
		stats.host_present_ms = impl->frame.last_host_present_ms_get();
		stats.draw_items = impl->frame.total_draw_items_get();
		stats.instances = impl->frame.total_instances_get();
		//GPU 侧耗时取自帧模块时间戳查询池（设备不支持时恒 0）
		stats.gpu_frame_ms = impl->frame.last_gpu_frame_ms_get();
		//GPU 分解：场景 / 后处理取自主查询池，阴影取自阴影专用池（第一百二十八轮）
		stats.gpu_scene_ms = impl->frame.last_gpu_scene_ms_get();
		stats.gpu_post_ms = impl->frame.last_gpu_post_ms_get();
		stats.gpu_shadow_ms = impl->shadow_gpu_ms;
		//累计分段耗时与阴影样本数（第一百三十一轮）
		stats.gpu_scene_total_ms = impl->frame.total_gpu_scene_ms_get();
		stats.gpu_post_total_ms = impl->frame.total_gpu_post_ms_get();
		stats.gpu_shadow_total_ms = impl->frame.total_gpu_shadow_ms_get();
		stats.gpu_shadow_samples = impl->frame.gpu_shadow_sample_count_get();
		//实际绘制量（不含被跳过的不完整项；与登记量 draw_items / instances 对照可见差异）
		stats.drawn_items = impl->frame.total_drawn_items_get();
		stats.drawn_instances = impl->frame.total_drawn_instances_get();
		//累计口径：主机侧耗时之和、GPU 侧成功读回之和与样本数
		stats.frame_total_ms = impl->frame.total_frame_ms_get();
		//主机侧分段累计（第一百三十四轮）
		stats.host_record_total_ms = impl->frame.total_host_record_ms_get();
		stats.host_submit_total_ms = impl->frame.total_host_submit_ms_get();
		stats.host_present_total_ms = impl->frame.total_host_present_ms_get();
		stats.gpu_total_ms = impl->frame.total_gpu_frame_ms_get();
		stats.gpu_samples = impl->frame.gpu_sample_count_get();
		//最近一帧实际执行的后处理级数（0 = 未走后处理链）
		stats.post_stages = impl->frame.last_post_stages_get();
		//最近一帧实际使用的采样数（第一百零一轮）
		stats.msaa_samples = impl->frame.last_sample_count_get();
		//累计因视锥剔除而未登记的项数
		stats.culled_items = impl->culled_items;
		//实例级剔除（第一百五十三轮）
		stats.culled_instances = impl->culled_instances;
		stats.last_culled_instances = impl->last_culled_instances;
		//最近一帧即时观测（第一百三十二轮；清零累计量时保持不变）
		stats.last_draw_items = impl->last_draw_items;
		stats.last_instances = impl->last_instances;
		stats.last_culled_items = impl->last_culled_items;
		//合批收益（第一百三十五轮）
		stats.last_merged_items = impl->last_merged_items;
		stats.merged_items = impl->merged_items;
		//阴影按需重绘观测（第一百四十五轮）
		stats.shadow_redraws = impl->shadow_redraws;
		stats.shadow_reuses = impl->shadow_reuses;
		stats.last_shadow_items = impl->last_shadow_items;
		stats.shadow_last_redrawn = impl->shadow_last_redrawn;
		//最近一次阴影 pass 的并行段数（第二百一十二轮）
		stats.shadow_parallel_segments = impl->last_shadow_parallel_segments;
		//累计按需上传字节（第七十二轮）
		stats.scene_upload_bytes = impl->scene_upload_bytes;
		stats.instance_upload_bytes = impl->instance_upload_bytes;
		//渲染工作线程观测（第一百七十七轮）
		stats.render_threads = impl->thread_pool.thread_count_get();
		stats.parallel_dispatches = impl->thread_pool.dispatch_count_get();
		//按用途拆分的派发次数（第二百二十四轮）
		stats.cull_parallel_dispatches = impl->cull_parallel_dispatches;
		stats.scan_parallel_dispatches = impl->scan_parallel_dispatches;
		stats.cache_parallel_dispatches = impl->cache_parallel_dispatches;
		//最近一帧并行录制段数（第一百九十八轮）
		stats.parallel_segments = impl->frame.last_parallel_segments_get();
		return stats;
	}

	//是否支持 GPU 时间戳
	bool Renderer::gpu_timestamp_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转调帧模块能力标志
		return impl->frame.gpu_timestamp_supported_get();
	}

	//是否支持像素回读
	bool Renderer::readback_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//交换链图像具备传输源用法才可回读
		return impl->swapchain.color_transfer_src_get();
	}

	//设置超采样抗锯齿倍率（SSAA）
	void Renderer::ssaa_factor_set(uint32_t factor)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//钳制到 1..4（0 视为关闭）
		uint32_t clamped = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
		//同值重复设置为空操作
		if (clamped == impl->ssaa_factor)
			return;
		impl->ssaa_factor = clamped;
		//倍率变化：清失败标记，下一次 frame_render 按需重建目标
		impl->ssaa_extent_failed = false;
		logger.info("渲染器:: SSAA 倍率已设为 {}（下一帧生效）", clamped);
	}

	//当前超采样倍率
	uint32_t Renderer::ssaa_factor(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return impl->ssaa_factor;
	}

	/*
	设置 MSAA 采样数（第九十六轮）
	请求值按设备支持钳制后立即生效：等尽在途帧 → 重建交换链 MSAA 附件 → 重建全部渲染目标
	（句柄保持）→ 重建全部图形管线。失败时回退交换链采样数并保持原值。
	*/
	void Renderer::msaa_samples_set(uint32_t samples)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//请求值归一（仅接受 2 / 4 / 8，其余按 1）
		if (samples != 2 && samples != 4 && samples != 8)
			samples = 1;
		//按设备对当前颜色 / 深度格式的支持钳制
		uint32_t supported = msaa_supported_max(impl->device.physical_device_get(),
			impl->swapchain.format_get(), impl->swapchain.depth_format_get(), samples);
		//若请求不被支持则降级
		if (supported < samples)
			logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", samples, supported);
		samples = supported;
		//同值重复设置为空操作
		if (samples == impl->msaa_samples)
			return;
		//等尽在途帧，确保旧附件不再被引用
		if (!impl->frame.frames_wait())
		{
			logger.error("渲染器:: MSAA 切换失败：等待在途帧失败");
			return;
		}
		uint32_t previous = impl->msaa_samples;
		//重建交换链 MSAA 附件（尺寸不变；旧资源进延迟销毁队列）
		VkExtent2D extent = impl->swapchain.extent_get();
		impl->swapchain.sample_count_set(samples);
		if (extent.width != 0 && extent.height != 0)
		{
			if (!impl->swapchain_rebuild_deferred(extent.width, extent.height))
			{
				logger.error("渲染器:: MSAA 切换失败：交换链重建失败，保持 {}×", previous);
				impl->swapchain.sample_count_set(previous);
				return;
			}
		}
		//重建跟随默认采样数的渲染目标（句柄不变）
		if (!impl->render_target.sample_count_set(samples))
		{
			logger.warn("渲染器:: MSAA 切换时部分渲染目标重建失败");
		}
		//重建全部图形管线（采样数被烘焙进管线状态）
		if (!impl->pipeline.pipeline_rebuild(impl->swapchain.format_get(),
			impl->swapchain.depth_format_get(), samples))
		{
			logger.warn("渲染器:: MSAA 切换时图形管线重建失败");
		}
		impl->msaa_samples = samples;
		impl->config.msaa_samples = samples;
		/*
		当前绑定视图可能指向被重建目标的旧视图（已销毁），统一回落到默认白纹理；
		各帧槽后处理采样集在每帧渲染前按当前目标重写，无需在此处理。
		*/
		{
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			if (impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler))
			{
				impl->bound_view = default_view;
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.texture_bind(slot, default_sampler, default_view);
			}
		}
		logger.info("渲染器:: MSAA 采样数已设为 {}×（下一帧生效）", impl->msaa_samples);
	}

	//当前 MSAA 采样数
	uint32_t Renderer::msaa_samples(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return impl->msaa_samples;
	}

	//设置 MSAA 深度解析开关（第一百九十轮）
	void Renderer::msaa_depth_resolve_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//记录开关（下一帧起由帧模块据此决定是否取多重采样深度图像）
		impl->msaa_depth_resolve = enabled;
		impl->config.msaa_depth_resolve = enabled;
		logger.info("渲染器:: MSAA 深度解析已{}（下一帧生效）", enabled ? "开启" : "关闭");
	}

	//当前 MSAA 深度解析开关（第一百九十轮）
	bool Renderer::msaa_depth_resolve(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->msaa_depth_resolve;
	}

	//当前颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十九轮）
	uint32_t Renderer::msaa_max_samples(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1;
		return msaa_supported_max(impl->device.physical_device_get(),
			impl->swapchain.format_get(), impl->swapchain.depth_format_get(), 8);
	}

	//是否支持采样器各向异性过滤
	bool Renderer::sampler_anisotropy_supported(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->device.anisotropy_enabled_get();
	}

	//采样器 LOD 偏置绝对值上限（设备上限；第九十七轮）
	float Renderer::sampler_max_lod_bias(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->device.max_sampler_lod_bias_get();
	}

	//采样器最大各向异性倍数（设备上限）
	float Renderer::sampler_max_anisotropy(void) const
	{
		//若尚未初始化则无能力可言
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->device.max_anisotropy_get();
	}

	//当前交换链宽度
	uint32_t Renderer::extent_width(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().width;
	}

	//当前交换链高度
	uint32_t Renderer::extent_height(void) const
	{
		//若实现体未分配则返回零
		if (impl == nullptr)
			return 0;
		return impl->swapchain.extent_get().height;
	}

	//请求改变窗口客户区尺寸
	bool Renderer::window_size_set(uint32_t width, uint32_t height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 窗口尺寸设置失败：尚未初始化");
			return false;
		}
		//转交平台层（成功即置「交换链待重建」标志，重建在下一次 frame_render 内完成）
		return impl->platform.window_size_set(width, height);
	}

	//按程序化球体加载网格（第六十六轮新增）
	bool Renderer::mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks)
	{
		//前置：已初始化、半径为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f)
			|| slices < 3 || slices > 128 || stacks < 2 || stacks > 64)
		{
			logger.warn("渲染器:: 球体参数非法（半径 {}、经度 {}、纬度 {}）", radius, slices, stacks);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::sphere_generate(radius, slices, stacks));
	}

	//按正二十面体球加载网格（第一百八十九轮）
	bool Renderer::mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions)
	{
		//前置：已初始化、半径为正（非数亦拒）、细分次数在 0..4
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || subdivisions > 4)
		{
			logger.warn("渲染器:: 正二十面体球参数非法（半径 {}、细分 {}）", radius, subdivisions);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::icosphere_generate(radius, subdivisions));
	}

	//按 XZ 平面细分网格加载网格（第一百九十三轮）
	bool Renderer::mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
		uint32_t segments_x, uint32_t segments_z)
	{
		//顶点总数（分段乘积上限约 100 万，防止误传超大分段耗尽内存）
		const uint64_t vertex_count = (static_cast<uint64_t>(segments_x) + 1)
			* (static_cast<uint64_t>(segments_z) + 1);
		//前置：已初始化、宽深为正（非数亦拒）、分段在 1..512 且顶点总数受限
		if (impl == nullptr || !impl->initialized || !(width > 0.0f) || !(depth > 0.0f)
			|| segments_x < 1 || segments_x > 512 || segments_z < 1 || segments_z > 512
			|| vertex_count > (1ull << 20))
		{
			logger.warn("渲染器:: 细分平面参数非法（宽 {}、深 {}、分段 {}×{}）",
				width, depth, segments_x, segments_z);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::ground_grid_generate(width, depth, segments_x, segments_z));
	}

	//按程序化圆柱加载网格（第六十六轮新增）
	bool Renderer::mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆柱参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cylinder_generate(radius, height, segments));
	}

	//按程序化平面加载网格（第七十一轮新增）
	bool Renderer::mesh_plane_load(Mesh_Handle& handle_out, float width, float height)
	{
		//前置：已初始化、宽高为正（非数亦拒）
		if (impl == nullptr || !impl->initialized || !(width > 0.0f) || !(height > 0.0f))
		{
			logger.warn("渲染器:: 平面参数非法（宽 {}、高 {}）", width, height);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::plane_generate(width, height));
	}

	//按程序化三角形加载网格（第一百四十二轮新增）
	bool Renderer::mesh_triangle_load(Mesh_Handle& handle_out)
	{
		//前置：已初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.warn("渲染器:: 三角形加载失败：尚未初始化");
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::triangle_generate());
	}

	//按程序化立方体加载网格（第一百四十二轮新增）
	bool Renderer::mesh_cube_load(Mesh_Handle& handle_out, float half_extent)
	{
		//前置：已初始化、半边长正（非数亦拒）
		if (impl == nullptr || !impl->initialized || !(half_extent > 0.0f))
		{
			logger.warn("渲染器:: 立方体参数非法（半边长 {}）", half_extent);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cube_generate(half_extent));
	}

	//按程序化圆盘加载网格（第八十三轮新增）
	bool Renderer::mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments)
	{
		//前置：已初始化、半径为正（非数亦拒）、分段在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f)
			|| segments < 3 || segments > 256)
		{
			logger.warn("渲染器:: 圆盘参数非法（半径 {}、分段 {}）", radius, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::disc_generate(radius, segments));
	}

	//按细分平面加载网格（第九十三轮新增）
	bool Renderer::mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
		uint32_t columns, uint32_t rows)
	{
		//前置：已初始化、宽高为正（非数亦拒）、分段在区间内
		if (impl == nullptr || !impl->initialized || !(width > 0.0f) || !(height > 0.0f)
			|| columns < 1 || columns > 256 || rows < 1 || rows > 256)
		{
			logger.warn("渲染器:: 细分平面参数非法（宽 {}、高 {}、列 {}、行 {}）",
				width, height, columns, rows);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::grid_generate(width, height, columns, rows));
	}

	//查询网格顶点 / 索引数量（第九十三轮新增）
	bool Renderer::mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const
	{
		//出参先复位
		if (vertex_count_out != nullptr)
			*vertex_count_out = 0;
		if (index_count_out != nullptr)
			*index_count_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交网格模块
		uint32_t vertex_count = 0;
		uint32_t index_count = 0;
		if (!impl->mesh.mesh_counts_get(handle, vertex_count, index_count))
			return false;
		if (vertex_count_out != nullptr)
			*vertex_count_out = vertex_count;
		if (index_count_out != nullptr)
			*index_count_out = index_count;
		return true;
	}

	//按程序化胶囊加载网格（第六十九轮新增）
	bool Renderer::mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
		uint32_t segments, uint32_t rings)
	{
		//前置：已初始化、半径为正、中段高度非负（非数亦拒）、分段与纬圈在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height >= 0.0f)
			|| segments < 3 || segments > 128 || rings < 2 || rings > 64)
		{
			logger.warn("渲染器:: 胶囊参数非法（半径 {}、高度 {}、分段 {}、纬圈 {}）",
				radius, height, segments, rings);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::capsule_generate(radius, height, segments, rings));
	}

	//按程序化圆锥加载网格（第六十八轮新增）
	bool Renderer::mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
	{
		//前置：已初始化、半径与高度为正（非数亦拒）、分段数在区间内
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || !(height > 0.0f)
			|| segments < 3 || segments > 128)
		{
			logger.warn("渲染器:: 圆锥参数非法（半径 {}、高度 {}、分段 {}）", radius, height, segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::cone_generate(radius, height, segments));
	}

	//按程序化圆环加载网格（第六十七轮新增）
	bool Renderer::mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
		uint32_t major_segments, uint32_t minor_segments)
	{
		//前置：已初始化、双半径为正（非数亦拒）、双分段在区间内
		if (impl == nullptr || !impl->initialized || !(major_radius > 0.0f) || !(minor_radius > 0.0f)
			|| major_segments < 3 || major_segments > 128 || minor_segments < 3 || minor_segments > 128)
		{
			logger.warn("渲染器:: 圆环参数非法（环半径 {}、管半径 {}、环分段 {}、管分段 {}）",
				major_radius, minor_radius, major_segments, minor_segments);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return mesh_load(handle_out, Mesh_Context::torus_generate(major_radius, minor_radius,
			major_segments, minor_segments));
	}

	bool Renderer::mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格加载失败：尚未初始化");
			return false;
		}
		//转交网格模块创建顶点 / 索引缓冲
		return impl->mesh.mesh_create(handle_out, data);
	}

	//卸载网格
	void Renderer::mesh_unload(Mesh_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//摘出网格缓冲（不立即销毁，避免销毁仍被在途命令缓冲引用的缓冲）
		Mesh_Record record{};
		if (!impl->mesh.mesh_detach(handle, record))
			return;
		//组装延迟条目（缓冲经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.buffer = record.buffer;
		resource.buffer_allocation = record.allocation;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//查询网格包围盒（第七十三轮新增）
	bool Renderer::mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const
	{
		//未初始化或出参为空则拒绝
		if (impl == nullptr || !impl->initialized || min_out == nullptr || max_out == nullptr)
			return false;
		//转交网格模块（句柄无效时返回假，出参不变）
		return impl->mesh.mesh_bounds_get(handle, min_out, max_out);
	}

	//更新网格顶点区域（第一百五十九轮）
	bool Renderer::mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
		const Vertex* vertices)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍被绘制命令读取的顶点缓冲）
		if (!impl->frame.frames_wait())
			return false;
		//转交网格模块（顶点区覆盖写入并保守扩张包围盒）
		return impl->mesh.mesh_update_vertices(handle, first_vertex, count, vertices);
	}

	//更新网格索引区域（第一百六十轮）
	bool Renderer::mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
		const uint32_t* indices)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍被绘制命令读取的索引缓冲）
		if (!impl->frame.frames_wait())
			return false;
		//转交网格模块（索引区覆盖写入并校验取值）
		return impl->mesh.mesh_update_indices(handle, first_index, count, indices);
	}

	//把一批模型矩阵追加入待绘制实例存储（列主序 16 浮点 / 个，材质 4 浮点 / 个），返回首个实例的字节偏移
	//（文件内辅助函数；容量是否足够由调用方先行校验，此处不重复检查）
	static VkDeviceSize instance_matrices_append(std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& store,
		const float* transforms, uint32_t count, bool material_active, float roughness, float metallic,
		float emissive)
	{
		//首个实例的字节偏移（追加前的元素个数 × 80）
		VkDeviceSize offset = static_cast<VkDeviceSize>(store.size()) * INSTANCE_STRIDE_BYTES;
		//逐实例追加
		for (uint32_t instance = 0; instance < count; ++instance)
		{
			//新实例的矩阵存储
			store.emplace_back();
			std::array<float, INSTANCE_FLOAT_COUNT>& target = store.back();
			//transforms 为空则整批用单位矩阵
			if (transforms == nullptr)
			{
				//单位矩阵（列主序）
				const float identity[16] = {
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				};
				for (uint32_t element = 0; element < 16; ++element)
					target[element] = identity[element];
			}
			else
			{
				//拷贝第 instance 个外部模型矩阵
				for (uint32_t element = 0; element < 16; ++element)
					target[element] = transforms[static_cast<size_t>(instance) * 16 + element];
			}
			/*
			逐物体材质（第六十八轮）
			覆盖标志置于第 2 分量（z）：0 = 不覆盖，片元着色器回落场景 UBO 的全局 PBR 参数；
			故未调用 material_pbr_set 的绘制项与旧版逐字节一致。
			*/
			target[INSTANCE_MATERIAL_OFFSET + 0] = material_active ? roughness : 0.0f;
			target[INSTANCE_MATERIAL_OFFSET + 1] = material_active ? metallic : 0.0f;
			target[INSTANCE_MATERIAL_OFFSET + 2] = material_active ? 1.0f : 0.0f;
			//第 4 分量为自发光强度（第七十轮；恒生效，0 = 无自发光）
			target[INSTANCE_MATERIAL_OFFSET + 3] = emissive;
		}
		return offset;
	}

	//绘制网格
	bool Renderer::mesh_draw(Mesh_Handle handle, const float* transform, const float* color)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格绘制失败：尚未初始化");
			return false;
		}

		//取网格共用缓冲、索引区偏移与索引数
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!impl->mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 网格绘制失败：网格句柄 {} 无效", handle);
			return false;
		}

		/*
		实例级视锥剔除（第二十二轮新增）
		完全在视锥外的绘制项**不登记**（合法跳过，不是错误）；返回真表示已剔除。
		保守口径：包围盒与视锥相交一律保留，故画面与旧版一致、仅减少不可见项。
		*/
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		if (impl->cull_enabled && impl->mesh.mesh_bounds_get(handle, bounds_min, bounds_max))
		{
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			//模型矩阵（空指针视作单位矩阵）
			const float* model = (transform != nullptr) ? transform : IDENTITY_MATRIX4;
			//若包围盒完全落在视锥外则跳过登记
			if (frustum_cull_aabb(view_proj, model, bounds_min, bounds_max))
			{
				//计入剔除观测（累计量与最近一帧量）
				++impl->culled_items;
				++impl->frame_culled_items;
				return true;
			}
		}

		/*
		模型矩阵改由「实例缓冲」承载（本层统一走实例路径）
		transform 为空则用单位矩阵；实例数为 1 即等价于逐次绘制（模型矩阵不再经推送常量传递）。
		*/
		if (impl->pending_instances.size() >= impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储已满（容量 {}），绘制被拒", impl->instance_capacity);
			return false;
		}
		//追加本项模型矩阵与材质并取其在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(impl->pending_instances, transform, 1,
			impl->material_pbr_active, impl->material_pbr_roughness, impl->material_pbr_metallic,
			impl->material_emissive);

		/*
		组装待绘制项
		描述符集（set0 场景 UBO / set1 采样器）延迟到 frame_render 时按「本帧帧槽」填装，
		故此处先置空、数量为 0。
		*/
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_get();
		item.layout = impl->pipeline.layout_get();
		item.scene_set = VK_NULL_HANDLE;
		item.sampler_set = VK_NULL_HANDLE;
		item.descriptor_set_count = 0;
		item.vertex_buffer = mesh_buffer;
		item.index_buffer = mesh_buffer;
		item.index_offset = index_offset;
		item.index_count = index_count;
		item.instance_buffer = VK_NULL_HANDLE;
		item.instance_offset = instance_offset;
		item.instance_count = 1;
		//采样状态快照（第一百七十四轮；帧渲染阶段据此写专属采样器集）
		item.draw_sampler = impl->bound_sampler;
		item.draw_albedo = impl->bound_view;
		item.draw_normal = impl->material_views[0];
		item.draw_roughness = impl->material_views[1];
		item.draw_emissive = impl->material_views[2];
		item.draw_ao = impl->material_views[3];
		item.draw_metallic = impl->material_views[4];
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !impl->in_text_draw;
		//透明标记（第一百三十轮：文字等非不透明混合项不参与合批，避免混叠顺序改变）
		item.transparent = impl->pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		/*
		颜色 / 色调推送（文字着色 ＋ 第六十九轮逐物体色调）
		矩阵已由实例缓冲承载，推送块前 64 字节仅用于占位（单位矩阵），后 16 字节为颜色；
		存储于 Impl 的 deque（元素引用稳定），绘制项持其指针，直到 frame_render 消费。
		非文字路径**恒推送**：显式 color 优先，其次吸附式逐物体色调，最后回落白色——
		场景片元着色器把反照率 × 该颜色，白色时画面逐字节不变。
		*/
		const float* push_color = color;
		//非文字且未显式给出颜色时，改用吸附色调或白色
		if (push_color == nullptr && !impl->in_text_draw)
			push_color = impl->material_tint_active
				? impl->material_tint
				: WHITE_COLOR_RGBA;
		if (push_color != nullptr)
		{
			impl->pending_push_constants.emplace_back();
			std::array<float, 28>& push = impl->pending_push_constants.back();
			//前 64 字节 = 单位矩阵
			push.fill(0.0f);
			push[0] = 1.0f;
			push[5] = 1.0f;
			push[10] = 1.0f;
			push[15] = 1.0f;
			//后 16 字节 = 颜色
			for (std::size_t index = 0; index < 4; ++index)
				push[16 + index] = push_color[index];
			/*
			材质贴图参数（第一百二十四轮；offset 80 的 vec4）
			x = 法线强度（0 = 不启用），y = 粗糙度贴图启用标志（贴图恒绑定，默认白纹理即恒等），zw 保留。
			文字绘制时该 vec4 改作描边参数（第一百四十轮）：x = 描边半宽（UV）、yzw = 描边色。
			*/
			if (impl->in_text_draw)
			{
				push[20] = impl->text_outline_uv_width;
				push[21] = impl->text_outline_color[0];
				push[22] = impl->text_outline_color[1];
				push[23] = impl->text_outline_color[2];
			}
			else
			{
				push[20] = impl->material_normal_strength;
				push[21] = impl->material_normal_strength > 0.0f ? 1.0f : 0.0f;
				//Alpha 裁剪阈值与 UV 旋转（第一百八十三轮；默认 0 = 关闭 / 不旋转）
				push[22] = impl->material_alpha_cutoff;
				push[23] = impl->material_uv_rotation;
			}
			/*
			贴图 UV 变换（第一百七十一轮；offset 96 的 vec4）
			xy = 缩放、zw = 偏移；文字绘制不采样材质贴图，保持恒等变换（1,1,0,0）。
			*/
			if (impl->in_text_draw)
			{
				push[24] = 1.0f;
				push[25] = 1.0f;
			}
			else
			{
				push[24] = impl->material_uv_scale[0];
				push[25] = impl->material_uv_scale[1];
				push[26] = impl->material_uv_offset[0];
				push[27] = impl->material_uv_offset[1];
			}
			item.push_constants = push.data();
			item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		}

		//先登记描边项（外扩轮廓须先写入深度，主体几何随后按更近深度覆盖内部）
		impl->outline_item_push(item);
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
	}

	//实例化绘制（同一网格 × count 个模型矩阵，一次绘制调用提交）
	bool Renderer::mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//实例数必须为正
		if (count == 0)
		{
			logger.error("渲染器:: 实例化绘制的实例数必须为正");
			return false;
		}
		//取网格记录（句柄无效则拒绝）
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!impl->mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 实例化绘制的网格句柄无效");
			return false;
		}
		/*
		实例级视锥剔除（第一百五十三轮；第二十二轮起为整批判定）
		逐实例判可见性：不可见实例不写入实例缓冲（保留原顺序中的可见子集），
		全部不可见时整批按一个绘制项计入剔除并直接返回。
		*/
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		if (transforms != nullptr && impl->cull_enabled && impl->mesh.mesh_bounds_get(handle, bounds_min, bounds_max))
		{
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			impl->camera.view_proj_get(view_proj);
			/*
			可见性掩码（第一百七十七轮）
			逐实例只写自己的掩码槽（1 = 可见、0 = 剔除），故可分块并行且与单线程结果完全一致；
			实例数达到阈值且工作线程可用时走并行，否则由调用线程整段执行。
			*/
			impl->instanced_cull_mask.assign(count, 0);
			//本帧生效的并行阈值（默认 256；可由配置调整，第二百二十二轮）
			const std::size_t cull_parallel_min = impl->cull_parallel_min_instances;
			if (impl->thread_pool.active() && count >= cull_parallel_min)
			{
				//派发计数差值（第二百二十四轮：按用途归入剔除）
				const uint64_t dispatch_before = impl->thread_pool.dispatch_count_get();
				//并行判定（块之间无共享写入）
				impl->thread_pool.ranges_run(count, cull_parallel_min,
					[this, view_proj, transforms, bounds_min, bounds_max](std::size_t begin, std::size_t end)
					{
						for (std::size_t instance = begin; instance < end; ++instance)
							impl->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
								transforms + instance * 16, bounds_min, bounds_max) ? 0 : 1;
					});
				//累加剔除用途的派发次数
				impl->cull_parallel_dispatches += impl->thread_pool.dispatch_count_get() - dispatch_before;
			}
			else
			{
				//单线程判定（与引入并行前逐字节一致）
				for (uint32_t instance = 0; instance < count; ++instance)
					impl->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
						transforms + static_cast<std::size_t>(instance) * 16, bounds_min, bounds_max) ? 0 : 1;
			}
			//筛选可见实例（按原相对顺序压缩；整块拷贝 16 浮点）
			impl->instanced_cull_scratch.clear();
			impl->instanced_cull_scratch.reserve(static_cast<std::size_t>(count) * 16);
			for (uint32_t instance = 0; instance < count; ++instance)
				//仅可见实例写入压缩缓冲
				if (impl->instanced_cull_mask[instance] != 0)
				{
					const float* matrix = transforms + static_cast<std::size_t>(instance) * 16;
					impl->instanced_cull_scratch.insert(impl->instanced_cull_scratch.end(), matrix, matrix + 16);
				}
			//可见 / 剔除的实例数
			const uint32_t visible_count = static_cast<uint32_t>(impl->instanced_cull_scratch.size() / 16);
			const uint32_t culled_count = count - visible_count;
			impl->culled_instances += culled_count;
			impl->frame_culled_instances += culled_count;
			//若整批不可见则不登记（按一个绘制项计）
			if (visible_count == 0)
			{
				++impl->culled_items;
				++impl->frame_culled_items;
				return true;
			}
			//以筛选后的可见子集继续登记
			transforms = impl->instanced_cull_scratch.data();
			count = visible_count;
		}
		/*
		本帧实例矩阵存储容量校验
		不足则整体拒绝、不登记任何绘制项（避免留下「半个实例化绘制」）。
		*/
		if (impl->pending_instances.size() + count > impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储不足（需 {} 个，余 {} 个）", count,
				impl->instance_capacity - static_cast<uint32_t>(impl->pending_instances.size()));
			return false;
		}
		//追加本批模型矩阵与材质并取首个实例在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(impl->pending_instances, transforms, count,
			impl->material_pbr_active, impl->material_pbr_roughness, impl->material_pbr_metallic,
			impl->material_emissive);

		/*
		组装待绘制项
		描述符集与实例缓冲句柄延迟到 frame_render 时按「本帧帧槽」填装，故此处先置空。
		*/
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_get();
		item.layout = impl->pipeline.layout_get();
		item.scene_set = VK_NULL_HANDLE;
		item.sampler_set = VK_NULL_HANDLE;
		item.descriptor_set_count = 0;
		item.vertex_buffer = mesh_buffer;
		item.index_buffer = mesh_buffer;
		item.index_offset = index_offset;
		item.index_count = index_count;
		item.instance_buffer = VK_NULL_HANDLE;
		item.instance_offset = instance_offset;
		item.instance_count = count;
		//采样状态快照（第一百七十四轮；帧渲染阶段据此写专属采样器集）
		item.draw_sampler = impl->bound_sampler;
		item.draw_albedo = impl->bound_view;
		item.draw_normal = impl->material_views[0];
		item.draw_roughness = impl->material_views[1];
		item.draw_emissive = impl->material_views[2];
		item.draw_ao = impl->material_views[3];
		item.draw_metallic = impl->material_views[4];
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !impl->in_text_draw;
		//透明标记（第一百三十轮：非不透明混合项不参与合批）
		item.transparent = impl->pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		/*
		色调推送（第六十九轮）
		场景片元着色器恒读推送常量 offset 64 处的 tint，故实例化绘制也必须推送该块：
		吸附色调或白色（未设置时），否则未设置的推送常量取值未定义。
		*/
		impl->pending_push_constants.emplace_back();
		std::array<float, 28>& push = impl->pending_push_constants.back();
		push.fill(0.0f);
		push[0] = 1.0f;
		push[5] = 1.0f;
		push[10] = 1.0f;
		push[15] = 1.0f;
		for (std::size_t index = 0; index < 4; ++index)
			push[16 + index] = impl->material_tint_active ? impl->material_tint[index] : 1.0f;
		/*
		材质贴图参数（第一百二十四 / 一百八十四轮；offset 80 的 vec4）
		x = 法线强度、y = 粗糙度贴图启用标志、z = Alpha 裁剪阈值、w = UV 旋转角。
		实例化路径此前未写 x / y（法线映射与粗糙度贴图在实例化绘制下失效），本轮补齐。
		*/
		push[20] = impl->material_normal_strength;
		push[21] = impl->material_normal_strength > 0.0f ? 1.0f : 0.0f;
		push[22] = impl->material_alpha_cutoff;
		push[23] = impl->material_uv_rotation;
		//贴图 UV 变换（第一百七十一轮；实例化绘制同样使用吸附的材质 UV）
		push[24] = impl->material_uv_scale[0];
		push[25] = impl->material_uv_scale[1];
		push[26] = impl->material_uv_offset[0];
		push[27] = impl->material_uv_offset[1];
		item.push_constants = push.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		//先登记描边项（外扩轮廓须先写入深度，主体几何随后按更近深度覆盖内部）
		impl->outline_item_push(item);
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
	}

	//从文件加载纹理（解码由纹理模块的 stb 路径承担）
	bool Renderer::texture_load_file(uint64_t& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 从文件加载纹理失败：尚未初始化");
			return false;
		}
		//转交纹理模块解码并创建资源
		return impl->texture.texture_create_from_file(handle_out, path);
	}

	//纹理加载
	bool Renderer::texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块创建
		return impl->texture.texture_create_from_memory(handle_out, pixels, width, height);
	}

	//程序生成棋盘格纹理
	bool Renderer::texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 棋盘格纹理加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块生成
		return impl->texture.texture_create_checkerboard(handle_out, width, height, cell_size);
	}

	//程序生成恒等 3D LUT（第一百四十四轮；2D 条带布局）
	bool Renderer::texture_load_lut_identity(uint64_t& handle_out, uint32_t edge)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 恒等查找表加载失败：尚未初始化");
			return false;
		}
		//每轴纹素数须在 [2, 64]
		if (edge < 2 || edge > 64)
		{
			logger.warn("渲染器:: 恒等查找表边长非法（{}），忽略", edge);
			return false;
		}
		//条带尺寸（宽 = 边长²、高 = 边长）
		const uint32_t strip_width = edge * edge;
		const uint32_t strip_height = edge;
		std::vector<uint8_t> pixels(static_cast<std::size_t>(strip_width) * strip_height * 4);
		//每级线性值（0..1）与对应的 sRGB 存储值（纹理格式为 SRGB，采样时自动解码回线性）
		std::vector<uint8_t> 编码表(edge);
		for (uint32_t 级 = 0; 级 < edge; ++级)
		{
			//线性值（恒等映射的自变量）
			float 线性 = static_cast<float>(级) * (edge > 1 ? 1.0f / static_cast<float>(edge - 1) : 0.0f);
			//线性 → sRGB 编码（与交换链 / 纹理的 SRGB 格式配套）
			float 编码 = 线性 <= 0.0031308f ? 线性 * 12.92f
				: 1.055f * std::pow(线性, 1.0f / 2.4f) - 0.055f;
			编码表[级] = static_cast<uint8_t>(编码 * 255.0f + 0.5f);
		}
		for (uint32_t 层 = 0; 层 < edge; ++层)
		{
			for (uint32_t 绿 = 0; 绿 < edge; ++绿)
			{
				for (uint32_t 红 = 0; 红 < edge; ++红)
				{
					//条带坐标（X = 层 × 边长 ＋ 红、Y = 绿）
					std::size_t 偏移 = (static_cast<std::size_t>(绿) * strip_width + 层 * edge + 红) * 4;
					pixels[偏移] = 编码表[红];
					pixels[偏移 + 1] = 编码表[绿];
					pixels[偏移 + 2] = 编码表[层];
					pixels[偏移 + 3] = 255;
				}
			}
		}
		//转交纹理模块上传
		return impl->texture.texture_create_from_memory(handle_out, pixels.data(), strip_width, strip_height);
	}

	//从内存像素创建立方体贴图（第一百二十五轮；供天空盒与简易 IBL 使用）
	bool Renderer::texture_cube_load(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 立方体贴图加载失败：尚未初始化");
			return false;
		}
		//转交纹理模块创建
		return impl->texture.texture_create_cube(handle_out, six_faces, width, height);
	}

	//天空盒 / 简易 IBL 开关（第一百二十五轮）
	void Renderer::sky_enable_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->sky_enabled = enabled;
	}

	//当前是否启用天空盒
	bool Renderer::sky_enabled(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->sky_enabled;
	}

	//设置天空盒强度（负值按 0 处理）
	void Renderer::sky_params_set(float ambient, float specular)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->sky_ambient = ambient > 0.0f ? ambient : 0.0f;
		impl->sky_specular = specular > 0.0f ? specular : 0.0f;
	}

	//读取天空盒强度（出参可为空）
	void Renderer::sky_params_get(float* ambient_out, float* specular_out) const
	{
		//未初始化时返回默认值（均为 0）
		float ambient = impl != nullptr && impl->initialized ? impl->sky_ambient : 0.0f;
		float specular = impl != nullptr && impl->initialized ? impl->sky_specular : 0.0f;
		if (ambient_out != nullptr)
			*ambient_out = ambient;
		if (specular_out != nullptr)
			*specular_out = specular;
	}

	//绑定天空盒立方体贴图（0 = 内置纯白立方体）
	void Renderer::sky_texture_set(uint64_t texture_handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//目标立方体（0 → 内置纯白立方体）
		uint64_t target = texture_handle != 0 ? texture_handle : impl->default_sky_texture;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(target, view, sampler))
		{
			logger.error("渲染器:: 天空盒贴图绑定失败：立方体贴图句柄 {} 无效", texture_handle);
			return;
		}
		//先等尽在途帧，避免重写仍被在途命令缓冲引用的描述符集
		if (!impl->frame.frames_wait())
			return;
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			//把立方体贴图写入该帧槽场景采样集的 binding 5
			if (!impl->pipeline.sky_bind(slot, sampler, view))
			{
				logger.error("渲染器:: 天空盒贴图绑定失败（帧槽 {}）", slot);
				return;
			}
		impl->sky_texture = texture_handle;
	}

	//当前天空盒立方体贴图句柄（0 = 内置纯白立方体）
	uint64_t Renderer::sky_texture(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->sky_texture;
	}

	//卸载纹理
	void Renderer::texture_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//摘出纹理图像与其视图（不立即销毁，避免销毁仍被在途命令缓冲 / 描述符集引用的资源）
		Texture_Record record{};
		if (!impl->texture.texture_detach(handle, record))
			return;
		//若卸载的是当前绑定视图，则回落内置默认白纹理（避免采样器状态切换时写入已失效视图）
		if (record.view == impl->bound_view)
			texture_bind_set(0);
		//若卸载的是当前天空盒贴图，则回落内置纯白立方体（避免 binding 5 指向已失效视图）
		if (handle != 0 && handle == impl->sky_texture)
			sky_texture_set(0);
		//组装延迟条目（图像与视图经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.image = record.image;
		resource.image_allocation = record.allocation;
		resource.view = record.view;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		impl->frame.deferred_push(std::move(entry), impl->frame.in_flight_count_get() + 1);
	}

	//读回纹理第 0 级像素
	bool Renderer::texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽，避免与该纹理的采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（内存模块同步读回）
		return impl->texture.texture_readback(handle, pixels, capacity, width_out, height_out);
	}

	//读回纹理第 0 级的指定子区域
	bool Renderer::texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（子区域同步读回）
		return impl->texture.texture_readback_region(handle, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//更新纹理第 0 级的子区域（第一百五十一轮）
	bool Renderer::texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍在采样中的图像）
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（子区域同步上传）
		return impl->texture.texture_update_region(handle, x, y, width, height, pixels);
	}

	//更新纹理指定级的子区域（第一百五十八轮）
	bool Renderer::texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height, const uint8_t* pixels)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽（避免改写仍在采样中的图像）
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（分级子区域同步上传）
		return impl->texture.texture_update_region_level(handle, level, x, y, width, height, pixels);
	}

	//读回纹理指定级的子区域（第一百六十六轮）
	bool Renderer::texture_readback_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
		uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
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
		//先把在途帧等尽
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（该级子区域同步读回）
		return impl->texture.texture_readback_region_level(handle, level, x, y, region_width, region_height,
			pixels, capacity, width_out, height_out);
	}

	//读回纹理指定级整幅像素（第一百五十八轮）
	bool Renderer::texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels, uint32_t capacity,
		uint32_t* width_out, uint32_t* height_out)
	{
		//出参先归零（未初始化 / 等帧失败时也须满足文档口径）
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//先把在途帧等尽
		if (!impl->frame.frames_wait())
			return false;
		//转交纹理模块（该级整幅同步读回）
		return impl->texture.texture_readback_level(handle, level, pixels, capacity, width_out, height_out);
	}

	//查询纹理 mip 级数（第一百六十二轮）
	bool Renderer::texture_mip_levels(uint64_t handle, uint32_t* mip_levels_out) const
	{
		//出参先归零
		if (mip_levels_out != nullptr)
			*mip_levels_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交纹理模块
		uint32_t levels = 0;
		if (!impl->texture.texture_mip_levels_get(handle, levels))
			return false;
		if (mip_levels_out != nullptr)
			*mip_levels_out = levels;
		return true;
	}

	//查询纹理指定级尺寸（第一百六十一轮）
	bool Renderer::texture_size_level(uint64_t handle, uint32_t level, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先归零
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交纹理模块（按级换算尺寸）
		return impl->texture.texture_size_level(handle, level, width_out, height_out);
	}

	//查询纹理尺寸
	bool Renderer::texture_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const
	{
		//出参先复位
		if (width_out != nullptr)
			*width_out = 0;
		if (height_out != nullptr)
			*height_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交纹理模块取尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		if (!impl->texture.texture_size_get(handle, width, height))
			return false;
		if (width_out != nullptr)
			*width_out = width;
		if (height_out != nullptr)
			*height_out = height;
		return true;
	}

	//设置当前绘制所用纹理
	void Renderer::texture_bind_set(uint64_t texture_handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零时回落内置默认白纹理
		uint64_t target = texture_handle == 0 ? impl->default_texture : texture_handle;
		//取目标纹理的视图与采样器
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(target, view, sampler))
		{
			//目标无效则回落默认白纹理
			target = impl->default_texture;
			if (!impl->texture.texture_bind_get(target, view, sampler))
			{
				logger.error("渲染器:: 纹理绑定失败：默认白纹理不可用");
				return;
			}
		}
		//记录当前绑定视图与采样器（逐绘制项采样集按此快照写入）
		impl->bound_view = view;
		impl->bound_sampler = sampler;
		/*
		写满全部帧槽 set1 之前先等全部在途帧完成
		描述符集被在途命令缓冲引用时更新会触发校验告警；等栅栏保证无命令缓冲在途。
		*/
		impl->frame.frames_wait();
		//把目标纹理写入所有帧槽 set1（长命资源，一次写满全部槽）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, sampler, view);
		/*
		材质贴图回落（第一百二十四轮；第一百七十轮合并为一次批量写）
		单独切换反照率时把 set1 binding 3 / 4 / 6 / 7 / 8 复位为内置默认白纹理，
		避免上一材质的各类贴图残留；材质路径随后会再写自己的贴图。
		*/
		{
			VkImageView white_view = VK_NULL_HANDLE;
			VkSampler white_sampler = VK_NULL_HANDLE;
			if (impl->texture.texture_bind_get(impl->default_texture, white_view, white_sampler))
			{
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.material_textures_bind(slot, white_sampler,
						white_view, white_view, white_view, white_view, white_view);
				//材质贴图状态同步回落白纹理（逐绘制项采样集按该快照写入）
				for (VkImageView& material_view : impl->material_views)
					material_view = white_view;
			}
		}
	}

	//设置纹理采样器状态（切换后重新绑定当前纹理，使新采样器真正生效）
	void Renderer::texture_sampler_state_set(const Texture_Sampler_State& state)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
		按设备能力归一各向异性
		设备创建时未启用 samplerAnisotropy 特性时，采样器请求各向异性会创建失败，
		故此处先归一：不支持则置假并告警，倍数钳到 [1, 设备上限]。
		*/
		Texture_Sampler_State effective = state;
		//请求各向异性时按设备能力调整
		if (effective.anisotropy)
		{
			//若设备不支持该特性
			if (!impl->device.anisotropy_enabled_get())
			{
				logger.warn("渲染器:: 设备不支持采样器各向异性，本次请求已忽略");
				effective.anisotropy = false;
				effective.max_anisotropy = 1.0f;
			}
			else
			{
				//倍数钳到 [1, 设备上限]
				float limit = impl->device.max_anisotropy_get();
				if (effective.max_anisotropy > limit)
					effective.max_anisotropy = limit;
				if (effective.max_anisotropy < 1.0f)
					effective.max_anisotropy = 1.0f;
			}
		}
		/*
		LOD 偏置归一（第七十七轮；第九十七轮改按设备上限钳制）
		非数归 0；再钳到设备极限 `maxSamplerLodBias`（绝对值）以内，避免创建采样器时校验报错。
		*/
		if (!(effective.lod_bias == effective.lod_bias))
			effective.lod_bias = 0.0f;
		float lod_bias_limit = impl->device.max_sampler_lod_bias_get();
		if (effective.lod_bias < -lod_bias_limit)
			effective.lod_bias = -lod_bias_limit;
		if (effective.lod_bias > lod_bias_limit)
			effective.lod_bias = lod_bias_limit;
		/*
		切换共享采样器状态并以新采样器重写各帧槽 set1
		采样器被烘焙进各帧槽 set1，故切换后须重写 set1，否则采样仍按旧参数进行；
		切换会销毁并重建共享采样器，故先等全部在途帧完成，避免销毁仍被在途描述符集引用的旧采样器。
		重写以「当前绑定视图」为采样源（纹理与渲染目标共用该语义），未绑过任何源时回落内置默认白纹理。
		*/
		impl->frame.frames_wait();
		impl->texture.sampler_state_set(effective);
		//取重建后的共享采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 采样器状态切换失败：共享采样器不可用");
			return;
		}
		//当前绑定视图为空则回落内置默认白纹理视图
		VkImageView view = impl->bound_view != VK_NULL_HANDLE ? impl->bound_view : default_view;
		//记录当前绑定视图与重建后的共享采样器（逐绘制项集按此快照写入）
		impl->bound_view = view;
		impl->bound_sampler = sampler;
		//以新采样器重写全部帧槽 set1（binding 0）
		for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
			impl->pipeline.texture_bind(slot, sampler, view);
		/*
		同步重写阴影采样器（set1 binding 1；第九十七轮）
		旧共享采样器已销毁，若 binding 1 仍指向旧句柄，阴影绘制会触发「采样器已销毁」校验错误；
		故在切换后把阴影图以新采样器重新写入各帧槽。
		*/
		if (impl->shadow_target != 0)
		{
			VkImage shadow_image = VK_NULL_HANDLE;
			VkImageView shadow_view = VK_NULL_HANDLE;
			VkImageView shadow_depth_view = VK_NULL_HANDLE;
			VkExtent2D shadow_extent{};
			if (impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view, shadow_depth_view, shadow_extent))
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.shadow_bind(slot, sampler, shadow_view);
		}
	}

	//读取当前采样器状态（只读诊断；未初始化返回默认状态）
	Texture_Sampler_State Renderer::texture_sampler_state_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return Texture_Sampler_State{};
		return impl->texture.sampler_state_get();
	}

	//设置后续绘制所用混合档（切档触发图形管线重建）
	void Renderer::blend_mode_set(Blend_Mode mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//同档重复设置为空操作
		if (impl->blend_mode == mode)
			return;
		/*
		触发图形管线重建（内部先等设备空闲，确保旧管线不再被在途帧引用）
		重建成功才更新已记录档位；失败则记录错误并保持档位记录（此时图形管线可能已不可用，
		不改变帧循环时序：本帧仍会清屏与推进，仅绘制项跳过）。
		*/
		if (!impl->pipeline.pipeline_default_blend_set(mode))
		{
			logger.error("渲染器:: 混合档切换失败（保持已记录档位 {}）", static_cast<int32_t>(impl->blend_mode));
			return;
		}
		impl->blend_mode = mode;
		logger.info("渲染器:: 混合档已切换为 {}", static_cast<int32_t>(mode));
	}

	//登记具名图形管线
	bool Renderer::pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage)
	{
		//尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			handle_out = 0;
			logger.error("渲染器:: 管线登记失败：尚未初始化");
			return false;
		}
		//公共入口一律开启背面剔除
		return impl->pipeline_load_ex(handle_out, vert_shader, frag_shader, blend_mode, alpha_to_coverage, false);
	}

	//登记具名图形管线（内部实现；可指定是否关闭背面剔除与是否写深度）
	bool Renderer::Impl::pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
		const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
		bool depth_write, bool cull_front)
	{
		//默认无有效句柄
		handle_out = 0;
		//加载顶点着色器模块（顺带解析顶点输入位置，供管线裁剪属性）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		uint32_t vert_input_locations = 0;
		if (!shader.module_load(vert_module, vert_shader, &vert_input_locations))
		{
			logger.error("渲染器:: 管线登记失败：顶点着色器 {} 加载失败", vert_shader);
			return false;
		}
		//加载片元着色器模块
		VkShaderModule frag_module = VK_NULL_HANDLE;
		if (!shader.module_load(frag_module, frag_shader))
		{
			logger.error("渲染器:: 管线登记失败：片元着色器 {} 加载失败", frag_shader);
			//回收已加载的顶点着色器模块，避免泄漏
			shader.module_destroy(vert_module);
			return false;
		}
		//登记到管线模块（具名管线与默认场景管线共享同一管线布局）
		uint64_t handle = 0;
		if (!pipeline.pipeline_register(handle, vert_module, frag_module, blend_mode,
			VK_FORMAT_UNDEFINED, cull_none, 0, vert_input_locations, depth_write, false, alpha_to_coverage, cull_front))
		{
			logger.error("渲染器:: 管线登记失败：图形管线创建失败");
			//回收两个着色器模块，避免泄漏
			shader.module_destroy(vert_module);
			shader.module_destroy(frag_module);
			return false;
		}
		//记录具名管线（着色器模块所有权在门面）
		Extra_Pipeline_Record record{};
		record.handle = handle;
		record.vert_module = vert_module;
		record.frag_module = frag_module;
		extra_pipelines.push_back(record);
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 具名管线已登记（句柄 {}）", handle);
		return true;
	}

	/*
	登记反向外扩描边项（第二百二十八轮）
	把主体绘制项复制一份，换「剔除正面」的描边管线，并把宽度 / 颜色推入 offset 112 的推送块；
	描边关闭（宽度为 0）、描边管线未登记或正在绘制文字时为空操作。
	*/
	void Renderer::Impl::outline_item_push(const Frame_Render_Item& item)
	{
		//描边关闭或管线未登记则空操作
		if (outline_width <= 0.0f || outline_pipeline == 0 || in_text_draw)
			return;
		//推送块：x = 外扩宽度、yzw = 描边色（与后处理描边色共用 offset 112 槽位）
		outline_push[0] = outline_width;
		outline_push[1] = outline_color[0];
		outline_push[2] = outline_color[1];
		outline_push[3] = outline_color[2];
		//复制主体项（实例区与索引区原样复用），换描边管线与描边推送块
		Frame_Render_Item outline_item = item;
		outline_item.pipeline = pipeline.pipeline_by_handle_get(outline_pipeline);
		outline_item.push_constants = outline_push.data();
		outline_item.push_constant_offset = static_cast<uint32_t>(sizeof(float) * 28);
		outline_item.push_constant_size = static_cast<uint32_t>(sizeof(float) * outline_push.size());
		//描边项不投影阴影、不参与合批（管线与推送块均与主体项不同）
		outline_item.cast_shadow = false;
		outline_item.transparent = true;
		pending_items.push_back(outline_item);
	}

	//卸载具名图形管线
	void Renderer::pipeline_unload(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄 0 为默认场景管线，禁止卸载
		if (handle == 0)
			return;
		/*
		先等全部在途帧完成
		图形管线可能仍被在途命令缓冲引用（其绑定状态已被记录进该命令缓冲），
		未等在途帧用尽即销毁会触发校验告警且属未定义行为。
		*/
		impl->frame.frames_wait();
		//在门面记录中查找目标
		for (auto it = impl->extra_pipelines.begin(); it != impl->extra_pipelines.end(); ++it)
		{
			//跳过非目标记录
			if (it->handle != handle)
				continue;
			//注销管线模块中的图形管线
			impl->pipeline.pipeline_release(handle);
			//销毁该管线的两个着色器模块
			if (it->vert_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->vert_module);
			if (it->frag_module != VK_NULL_HANDLE)
				impl->shader.module_destroy(it->frag_module);
			//移除门面记录
			impl->extra_pipelines.erase(it);
			//若卸载的是当前所选管线则回落默认场景管线
			if (impl->bound_pipeline_request == handle)
			{
				impl->bound_pipeline_request = 0;
				impl->pipeline.pipeline_activate(0);
			}
			//若该管线在后处理链中则一并摘除（避免链残留失效句柄导致整帧回落）
			std::size_t chain_before = impl->post_chain.size();
			impl->post_chain.erase(std::remove(impl->post_chain.begin(), impl->post_chain.end(), handle), impl->post_chain.end());
			//若确有摘除则告警
			if (impl->post_chain.size() != chain_before)
			{
				/*
				级序已压缩（第二十五轮修复）
				逐级专设参数按旧下标存放，压缩后会错位到别的级，故整体清除（宁可回到全局参数，不可错配）。
				*/
				for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
				{
					impl->post_stage_param_valid[stage] = false;
					impl->post_stage_color_valid[stage] = false;
				}
				logger.warn("渲染器:: 后处理链已摘除被卸载管线（句柄 {}，剩余 {} 级）；逐级参数已整体清除",
					handle, impl->post_chain.size());
			}
			logger.info("渲染器:: 具名管线已卸载（句柄 {}）", handle);
			return;
		}
	}

	//设置后续绘制所用图形管线
	void Renderer::pipeline_bind_set(uint64_t handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//目标句柄（0 = 默认场景管线）
		uint64_t target = handle;
		//若句柄非零且尚未登记则回落默认
		if (target != 0 && !impl->pipeline.pipeline_has(target))
		{
			logger.warn("渲染器:: 管线句柄 {} 未登记，回落默认场景管线", handle);
			target = 0;
		}
		//激活目标管线（失败则保持原激活不变）
		if (!impl->pipeline.pipeline_activate(target))
			return;
		impl->bound_pipeline_request = target;
	}

	//登记材质（图形管线 ＋ 纹理 ＋ 混合档）
	bool Renderer::material_load(Material_Handle& handle_out, const Material_Data& data)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 材质登记失败：尚未初始化");
			return false;
		}
		//待登记记录
		Impl::Material_Record record{};
		record.pipeline = data.pipeline;
		record.texture = data.texture;
		/*
		逐物体 PBR（第六十八轮）
		与非数 / 越界字段一律按「不覆盖」处理，避免把非法参数写进实例属性；
		合法域：粗糙度 (0.05, 4]、金属度 [0, 1]。
		*/
		record.pbr_override = data.pbr_override
			&& data.pbr_roughness > 0.05f && data.pbr_roughness <= 4.0f
			&& data.pbr_metallic >= 0.0f && data.pbr_metallic <= 1.0f;
		if (record.pbr_override)
		{
			record.pbr_roughness = data.pbr_roughness;
			record.pbr_metallic = data.pbr_metallic;
		}
		//若声明覆盖但参数非法则告警（按不覆盖登记，保持默认画面）
		if (data.pbr_override && !record.pbr_override)
			logger.warn("渲染器:: 材质 PBR 参数非法（粗糙度 {}、金属度 {}），按不覆盖登记",
				data.pbr_roughness, data.pbr_metallic);
		/*
		逐物体反照率色调（第六十九轮）
		四个分量都在 [0, 1] 才视为覆盖；否则按「不覆盖」登记，不把脏值送进推送常量。
		*/
		record.tint_override = data.tint_override
			&& data.tint[0] >= 0.0f && data.tint[0] <= 1.0f
			&& data.tint[1] >= 0.0f && data.tint[1] <= 1.0f
			&& data.tint[2] >= 0.0f && data.tint[2] <= 1.0f
			&& data.tint[3] >= 0.0f && data.tint[3] <= 1.0f;
		if (record.tint_override)
			for (std::size_t index = 0; index < 4; ++index)
				record.tint[index] = data.tint[index];
		//若声明色调覆盖但参数非法则告警
		if (data.tint_override && !record.tint_override)
			logger.warn("渲染器:: 材质色调非法（{} / {} / {} / {}），按不覆盖登记",
				data.tint[0], data.tint[1], data.tint[2], data.tint[3]);
		/*
		逐物体自发光（第七十轮）
		合法域 [0, 8]；越界 / 非数按 0（无自发光）登记，不把脏值写进实例属性。
		*/
		record.emissive = (data.emissive >= 0.0f && data.emissive <= 8.0f) ? data.emissive : 0.0f;
		if (!(data.emissive >= 0.0f && data.emissive <= 8.0f))
			logger.warn("渲染器:: 材质自发光 {} 越界（合法域 [0, 8]），按 0 登记", data.emissive);
		/*
		PBR 贴图化（第一百二十四轮）
		法线强度合法域 (0, 4]；越界 / 非数按 0（不启用）登记。
		贴图句柄原样记录，绑定阶段无效句柄回落内置默认白纹理。
		*/
		record.normal_texture = data.normal_texture;
		record.roughness_texture = data.roughness_texture;
		record.emissive_texture = data.emissive_texture;
		record.ao_texture = data.ao_texture;
		record.metallic_texture = data.metallic_texture;
		/*
		贴图 UV 变换（第一百七十一轮）
		非有限值按默认登记（缩放 1、偏移 0），避免把脏值写进推送常量。
		*/
		for (std::size_t index = 0; index < 2; ++index)
		{
			record.uv_scale[index] = std::isfinite(data.uv_scale[index]) ? data.uv_scale[index] : 1.0f;
			record.uv_offset[index] = std::isfinite(data.uv_offset[index]) ? data.uv_offset[index] : 0.0f;
		}
		/*
		UV 旋转与 Alpha 裁剪（第一百八十三轮）
		旋转角非有限值按 0（不旋转）；裁剪阈值非有限值或越界按 0（关闭），避免脏值影响片元丢弃。
		*/
		record.uv_rotation = std::isfinite(data.uv_rotation) ? data.uv_rotation : 0.0f;
		record.alpha_cutoff = (std::isfinite(data.alpha_cutoff) && data.alpha_cutoff > 0.0f && data.alpha_cutoff <= 1.0f)
			? data.alpha_cutoff : 0.0f;
		record.normal_strength = (data.normal_strength > 0.0f && data.normal_strength <= 4.0f)
			? data.normal_strength : 0.0f;
		if (data.normal_strength != 0.0f && record.normal_strength == 0.0f)
			logger.warn("渲染器:: 材质法线强度 {} 越界（合法域 (0, 4]），按 0 登记", data.normal_strength);
		//覆盖率写入（第一百二十六轮；仅 MSAA > 1× 时由管线侧生效）
		record.alpha_to_coverage = data.alpha_to_coverage;
		//双面渲染（第一百七十二轮；true 时使用关闭背面剔除的共享管线）
		record.double_sided = data.double_sided;
		//深度写入（第一百七十三轮；false 时使用不写深度的共享管线）
		record.depth_write = data.depth_write;
		/*
		若未指定管线且需要共享管线：混合档非不透明，或显式要求覆盖率写入 / 双面渲染 / 关闭深度写入
		（覆盖率写入、双面渲染与关闭深度写入均不能沿用默认场景管线，故不透明档也须登记专用管线）
		*/
		if (record.pipeline == 0 && (data.blend != Blend_Mode::Opaque
			|| data.alpha_to_coverage || data.double_sided || !data.depth_write))
		{
			/*
			组合键缓存（第一百七十三轮扩为八组合）
			下标 = 档位序 × 8 + 组合位（bit2 = 关闭深度写入、bit1 = 覆盖率写入、bit0 = 双面渲染）；
			同组合材质共享一条具名管线，不足则扩容。
			*/
			std::size_t index = static_cast<std::size_t>(data.blend) * 8
				+ (data.depth_write ? 0 : 4) + (data.alpha_to_coverage ? 2 : 0) + (data.double_sided ? 1 : 0);
			std::vector<uint64_t>& table = impl->material_pipelines;
			if (table.size() <= index)
				table.resize(index + 1, 0);
			//该组合尚未登记则以默认场景着色器对登记一条（此后同组合材质共享之）
			if (table[index] == 0)
			{
				if (!impl->pipeline_load_ex(table[index], "场景.vert.spv", "场景.frag.spv",
					data.blend, data.alpha_to_coverage, data.double_sided, data.depth_write))
				{
					logger.error("渲染器:: 材质登记失败：具名管线登记失败");
					return false;
				}
			}
			//材质使用该组合的共享管线（所有权归门面，卸载不释放）
			record.pipeline = table[index];
		}
		//标记记录有效
		record.valid = true;
		//分配句柄：优先复用第一个空槽，否则追加
		Material_Handle handle = 0;
		for (std::size_t index = 0; index < impl->materials.size(); ++index)
		{
			//若该槽已空
			if (!impl->materials[index].valid)
			{
				impl->materials[index] = record;
				handle = static_cast<Material_Handle>(index) + 1;
				break;
			}
		}
		//若无空槽则追加
		if (handle == 0)
		{
			impl->materials.push_back(record);
			handle = static_cast<Material_Handle>(impl->materials.size());
		}
		//写出句柄
		handle_out = handle;
		logger.info("渲染器:: 材质已登记（句柄 {}，管线 {}，纹理 {}）", handle, record.pipeline, record.texture);
		return true;
	}

	//卸载材质
	void Renderer::material_unload(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零或越界即视为无需卸载（幂等）
		if (handle == 0 || handle > impl->materials.size())
			return;
		//取目标记录
		Impl::Material_Record& record = impl->materials[handle - 1];
		//已空则忽略
		if (!record.valid)
			return;
		//复位记录（标记槽为空；具名管线为门面共享资源，不随材质卸载释放）
		record = Impl::Material_Record{};
		logger.info("渲染器:: 材质已卸载（句柄 {}）", handle);
	}

	//设置后续绘制所用材质
	void Renderer::material_bind_set(Material_Handle handle)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//句柄为零则回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖
		if (handle == 0)
		{
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			material_emissive_clear();
			impl->material_normal_strength = 0.0f;
			impl->material_uv_scale[0] = 1.0f;
			impl->material_uv_scale[1] = 1.0f;
			impl->material_uv_offset[0] = 0.0f;
			impl->material_uv_offset[1] = 0.0f;
			impl->material_uv_rotation = 0.0f;
			impl->material_alpha_cutoff = 0.0f;
			return;
		}
		//句柄无效则回落默认并告警
		if (handle > impl->materials.size() || !impl->materials[handle - 1].valid)
		{
			logger.warn("渲染器:: 材质句柄 {} 无效，回落默认管线与默认纹理", handle);
			pipeline_bind_set(0);
			texture_bind_set(0);
			material_pbr_clear();
			material_tint_clear();
			material_emissive_clear();
			impl->material_normal_strength = 0.0f;
			impl->material_uv_scale[0] = 1.0f;
			impl->material_uv_scale[1] = 1.0f;
			impl->material_uv_offset[0] = 0.0f;
			impl->material_uv_offset[1] = 0.0f;
			impl->material_uv_rotation = 0.0f;
			impl->material_alpha_cutoff = 0.0f;
			return;
		}
		//取目标记录
		const Impl::Material_Record& record = impl->materials[handle - 1];
		//绑定材质管线与纹理（管线句柄未登记 / 纹理句柄无效时各自内部回落默认）
		pipeline_bind_set(record.pipeline);
		texture_bind_set(record.texture);
		/*
		PBR 贴图化（第一百二十四轮）
		法线 / 粗糙度贴图无效或未配置时回落内置默认白纹理（采样为 (1,1,1)，画面不变）；
		法线强度经推送常量 offset 80 的 x 分量下发给场景片元。
		*/
		impl->material_normal_strength = record.normal_strength;
		{
			//默认白纹理（兜底视图与采样器）
			VkImageView white_view = VK_NULL_HANDLE;
			VkSampler white_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, white_view, white_sampler);
			/*
			材质贴图采样器（第一百七十轮）
			纹理模块全部纹理共用同一采样器，故五个贴图只取用这一个采样器。
			*/
			VkSampler material_sampler = white_sampler;
			//法线贴图（无效则用默认白纹理）
			VkImageView normal_view = white_view;
			if (record.normal_texture != 0)
				impl->texture.texture_bind_get(record.normal_texture, normal_view, material_sampler);
			//粗糙度贴图（无效则用默认白纹理）
			VkImageView roughness_view = white_view;
			if (record.roughness_texture != 0)
				impl->texture.texture_bind_get(record.roughness_texture, roughness_view, material_sampler);
			//自发光颜色贴图（第一百六十八轮；无效则用默认白纹理）
			VkImageView emissive_view = white_view;
			if (record.emissive_texture != 0)
				impl->texture.texture_bind_get(record.emissive_texture, emissive_view, material_sampler);
			//环境光遮蔽贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView ao_view = white_view;
			if (record.ao_texture != 0)
				impl->texture.texture_bind_get(record.ao_texture, ao_view, material_sampler);
			//金属度贴图（第一百六十九轮；无效则用默认白纹理）
			VkImageView metallic_view = white_view;
			if (record.metallic_texture != 0)
				impl->texture.texture_bind_get(record.metallic_texture, metallic_view, material_sampler);
			/*
			一次性写入全部材质贴图（第一百七十轮）
			任一视图取用失败则整次跳过，避免把空句柄写进描述符集。
			*/
			if (normal_view != VK_NULL_HANDLE && roughness_view != VK_NULL_HANDLE
				&& emissive_view != VK_NULL_HANDLE && ao_view != VK_NULL_HANDLE && metallic_view != VK_NULL_HANDLE)
			{
				for (uint32_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					impl->pipeline.material_textures_bind(slot, material_sampler,
						normal_view, roughness_view, emissive_view, ao_view, metallic_view);
				//同步材质贴图状态（逐绘制项采样集按该快照写入）
				impl->material_views[0] = normal_view;
				impl->material_views[1] = roughness_view;
				impl->material_views[2] = emissive_view;
				impl->material_views[3] = ao_view;
				impl->material_views[4] = metallic_view;
			}
		}
		//按记录的 PBR 覆盖标志设置 / 清除逐物体材质（第六十八轮）
		if (record.pbr_override)
			impl->material_pbr_active = true;
		else
			impl->material_pbr_active = false;
		impl->material_pbr_roughness = record.pbr_override ? record.pbr_roughness : 1.0f;
		impl->material_pbr_metallic = record.pbr_override ? record.pbr_metallic : 0.0f;
		//按记录同步逐物体色调（第六十九轮）
		impl->material_tint_active = record.tint_override;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = record.tint_override ? record.tint[index] : 1.0f;
		//同步逐物体自发光（第七十轮）
		impl->material_emissive = record.emissive;
		//同步贴图 UV 变换（第一百七十一轮；材质默认 (1,1)/(0,0) 即恒等）
		for (std::size_t index = 0; index < 2; ++index)
		{
			impl->material_uv_scale[index] = record.uv_scale[index];
			impl->material_uv_offset[index] = record.uv_offset[index];
		}
		//同步 UV 旋转与 Alpha 裁剪（第一百八十三轮；默认 0 即关闭）
		impl->material_uv_rotation = record.uv_rotation;
		impl->material_alpha_cutoff = record.alpha_cutoff;
	}

	/*
	设置逐物体 PBR 材质（第六十八轮）
	粗糙度合法域 (0.05, 4]、金属度合法域 [0, 1]；任一项越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（实例属性在 mesh_draw 时写入），与绑定类接口的吸附语义一致。
	*/
	bool Renderer::material_pbr_set(float roughness, float metallic)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体材质失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(roughness > 0.05f && roughness <= 4.0f))
		{
			logger.warn("渲染器:: 逐物体材质粗糙度 {} 越界（合法域 (0.05, 4]），已忽略", roughness);
			return false;
		}
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体材质金属度 {} 越界（合法域 [0, 1]），已忽略", metallic);
			return false;
		}
		//写入吸附状态
		impl->material_pbr_active = true;
		impl->material_pbr_roughness = roughness;
		impl->material_pbr_metallic = metallic;
		return true;
	}

	//清除逐物体 PBR 材质（第六十八轮）
	void Renderer::material_pbr_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落全局参数
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
	}

	//当前是否启用逐物体 PBR 材质覆盖（第六十八轮）
	bool Renderer::material_pbr_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_pbr_active;
	}

	//当前逐物体材质粗糙度缩放（第六十八轮）
	float Renderer::material_pbr_roughness(void) const
	{
		//未初始化返回默认 1
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->material_pbr_roughness;
	}

	//当前逐物体材质金属度（第六十八轮）
	float Renderer::material_pbr_metallic(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_pbr_metallic;
	}

	/*
	设置逐物体反照率色调（第六十九轮）
	四个分量须都在 [0, 1]；任一分量越界 / 非数则整次拒绝（状态不变）。
	设置只影响**之后登记**的绘制项（推送常量在 mesh_draw 时写入），与绑定类接口的吸附语义一致；
	mesh_draw 显式给出的 color 形参优先于本状态。
	*/
	bool Renderer::material_tint_set(float red, float green, float blue, float alpha)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体色调失败：尚未初始化");
			return false;
		}
		//各分量合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(red >= 0.0f && red <= 1.0f) || !(green >= 0.0f && green <= 1.0f)
			|| !(blue >= 0.0f && blue <= 1.0f) || !(alpha >= 0.0f && alpha <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体色调分量越界（{} / {} / {} / {}），已忽略", red, green, blue, alpha);
			return false;
		}
		//写入吸附状态
		impl->material_tint_active = true;
		impl->material_tint[0] = red;
		impl->material_tint[1] = green;
		impl->material_tint[2] = blue;
		impl->material_tint[3] = alpha;
		return true;
	}

	//清除逐物体反照率色调（第六十九轮）
	void Renderer::material_tint_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落白色（画面与未设置时一致）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
	}

	//当前是否启用逐物体反照率色调（第六十九轮）
	bool Renderer::material_tint_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_tint_active;
	}

	//读取当前逐物体色调（第六十九轮）
	void Renderer::material_tint_get(float out[4]) const
	{
		//空指针直接忽略
		if (out == nullptr)
			return;
		//未初始化返回白色
		if (impl == nullptr || !impl->initialized)
		{
			for (std::size_t index = 0; index < 4; ++index)
				out[index] = 1.0f;
			return;
		}
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = impl->material_tint[index];
	}

	/*
	设置逐物体自发光强度（第七十轮）
	合法域 [0, 8]；越界 / 非数整次拒绝（状态不变）。自发光恒生效，无需覆盖标志。
	*/
	bool Renderer::material_emissive_set(float emissive)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置自发光失败：尚未初始化");
			return false;
		}
		//合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(emissive >= 0.0f && emissive <= 8.0f))
		{
			logger.warn("渲染器:: 自发光 {} 越界（合法域 [0, 8]），已忽略", emissive);
			return false;
		}
		impl->material_emissive = emissive;
		return true;
	}

	//清除逐物体自发光（第七十轮）
	void Renderer::material_emissive_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->material_emissive = 0.0f;
		impl->material_normal_strength = 0.0f;
	}

	//当前逐物体自发光强度（第七十轮）
	float Renderer::material_emissive(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_emissive;
	}

	//创建离屏渲染目标
	bool Renderer::render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height)
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
	bool Renderer::render_target_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const
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
	bool Renderer::render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
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
	bool Renderer::render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
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
	bool Renderer::render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
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
	bool Renderer::render_target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
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
	void Renderer::render_target_destroy(uint64_t handle)
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
	void Renderer::render_target_set(uint64_t handle)
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
	void Renderer::render_target_present_set(uint64_t handle)
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
	bool Renderer::render_target_present_chain_set(const uint64_t* handles, uint32_t count)
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
		if (handles == nullptr || count > Impl::PRESENT_TARGET_MAX)
		{
			logger.error("渲染器:: 后处理目标组设置失败：句柄数组为空或数量 {} 越界（上限 {}）",
				count, Impl::PRESENT_TARGET_MAX);
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
	bool Renderer::post_chain_set(const uint64_t* handles, uint32_t count)
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
			for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
			{
				impl->post_stage_param_valid[stage] = false;
				impl->post_stage_color_valid[stage] = false;
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
		if (count > Impl::POST_CHAIN_MAX_STAGES)
		{
			logger.error("渲染器:: 后处理链设置失败：级数 {} 超过上限 {}", count, Impl::POST_CHAIN_MAX_STAGES);
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
		for (std::size_t stage = 0; stage < Impl::POST_CHAIN_MAX_STAGES; ++stage)
		{
			impl->post_stage_param_valid[stage] = false;
			impl->post_stage_color_valid[stage] = false;
		}

		logger.info("渲染器:: 后处理链已设置（{} 级）", count);
		return true;
	}

	//读取当前后处理链级数（0 表未设链）
	uint32_t Renderer::post_chain_count(void) const
	{
		//未初始化则视为空链
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->post_chain.size());
	}

	//读回当前后处理链的各级管线句柄（只读诊断入口）
	uint32_t Renderer::post_chain_get(uint64_t* out, uint32_t capacity) const
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
	uint32_t Renderer::post_chain_sample_counts(uint32_t* out, uint32_t capacity) const
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
	void Renderer::texture_bind_target(uint64_t handle)
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
	void Renderer::post_lut_bind(uint64_t texture_handle)
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
	uint64_t Renderer::post_lut_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->lut_texture;
	}

	//从 TTF 文件加载字体（字号 = 字形像素高度）
	bool Renderer::font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height)
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
	void Renderer::font_unload(uint64_t handle)
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
	bool Renderer::text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y)
	{
		//默认布局即单行左对齐（不换行、行高取字体像素高度）
		return text_draw_layout(font, text, pixel_x, pixel_y, Text_Layout{});
	}

	//度量单行文本（像素；宽不含尾随空白，高恒为字体像素高度）
	bool Renderer::text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const
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
	bool Renderer::text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
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
	void Renderer::bloom_param_set(float threshold, float knee, float intensity)
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
	void Renderer::post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= Impl::POST_CHAIN_MAX_STAGES)
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
	void Renderer::post_stage_param4_set(uint32_t stage, float x, float y, float z, float w)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= Impl::POST_CHAIN_MAX_STAGES)
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

	//设置某一级后处理链的描边色（第一百八十一轮）
	void Renderer::post_stage_color_set(uint32_t stage, float r, float g, float b)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//级序须落在当前链内（链外下标无意义）
		if (stage >= impl->post_chain.size() || stage >= Impl::POST_CHAIN_MAX_STAGES)
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

	//启用 / 关闭视锥剔除
	void Renderer::cull_enabled_set(bool enabled)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->cull_enabled = enabled;
	}

	//查询视锥剔除是否启用
	bool Renderer::cull_enabled(void) const
	{
		//未初始化时返回默认值（开启）
		if (impl == nullptr || !impl->initialized)
			return true;
		return impl->cull_enabled;
	}

	//启用 / 关闭静态合批（第一百二十七轮）
	void Renderer::static_batching_set(bool enabled)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->static_batching = enabled;
	}

	//查询静态合批是否启用
	bool Renderer::static_batching(void) const
	{
		//未初始化时返回默认值（开启）
		if (impl == nullptr || !impl->initialized)
			return true;
		return impl->static_batching;
	}

	//设置渲染工作线程数（第一百七十七轮）
	void Renderer::render_threads_set(uint32_t count)
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
	uint32_t Renderer::render_threads(void) const
	{
		//未初始化时返回默认值（0 = 单线程）
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->thread_pool.thread_count_get();
	}

	//设置实例级剔除的并行阈值（第二百二十二轮）
	void Renderer::cull_parallel_min_instances_set(uint32_t count)
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
	uint32_t Renderer::cull_parallel_min_instances(void) const
	{
		//未初始化时返回 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->cull_parallel_min_instances);
	}

	//设置实例缓冲差异扫描 / 缓存回写的并行阈值（第二百二十六轮）
	void Renderer::instance_scan_parallel_min_set(uint32_t count)
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
	uint32_t Renderer::instance_scan_parallel_min(void) const
	{
		//未初始化时返回 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return static_cast<uint32_t>(impl->instance_scan_parallel_min);
	}

	//读取某一级的泛光专设参数（只读诊断）
	uint32_t Renderer::post_stage_param_get(uint32_t stage, float out[4]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return 0;
		//默认写零
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = 0.0f;
		//未初始化 / 级序不在链内 / 该级未设专设值 → 0
		if (impl == nullptr || !impl->initialized || stage >= impl->post_chain.size()
			|| stage >= Impl::POST_CHAIN_MAX_STAGES || !impl->post_stage_param_valid[stage])
			return 0;
		//写出该级专设值
		for (std::size_t each = 0; each < 4; ++each)
			out[each] = impl->post_stage_param[stage][each];
		return 1;
	}

	//批量读回当前链各级的专设参数（只读诊断）
	uint32_t Renderer::post_chain_params_get(float* out, uint32_t capacity) const
	{
		//出参为空或容量不足一级则无内容可写
		if (out == nullptr || capacity < 4)
			return 0;
		//实际可写的级数（受链级数与容量共同限制）
		uint32_t stages = capacity / 4;
		//未初始化时链为空
		uint32_t chain_size = (impl == nullptr || !impl->initialized)
			? 0u : static_cast<uint32_t>(impl->post_chain.size());
		if (chain_size > Impl::POST_CHAIN_MAX_STAGES)
			chain_size = Impl::POST_CHAIN_MAX_STAGES;
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
	void Renderer::text_color_set(const float color[4])
	{
		//未初始化或出参为空则静默返回
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//转发文字绘制子系统（逐分量钳制）
		impl->text.color_set(color);
	}

	//设置文字描边（第一百四十轮；UV 半宽 ＋ 描边色）
	void Renderer::text_outline_set(float uv_width, const float color[3])
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
	uint64_t Renderer::font_skipped_codepoints(uint64_t font) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->font.font_skipped_codepoints_get(font);
	}

	//文字网格缓存命中次数（第七十五轮）
	uint64_t Renderer::text_cache_hits(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_hits();
	}

	//设置文本网格缓存条目上限（第八十四轮）
	void Renderer::text_cache_capacity_set(uint32_t capacity)
	{
		//未初始化则静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->text.mesh_cache_capacity_set(capacity);
	}

	//文本网格缓存条目上限（第八十四轮）
	uint32_t Renderer::text_cache_capacity(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_capacity();
	}

	//当前文本网格缓存条目数（第八十四轮）
	uint32_t Renderer::text_cache_size(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_size();
	}

	//文字网格缓存未命中次数（第七十五轮）
	uint64_t Renderer::text_cache_misses(void) const
	{
		//未初始化则视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->text.mesh_cache_misses();
	}

	//相机：设置位置
	void Renderer::camera_position_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.position_set(x, y, z);
	}

	//相机：设置注视点
	void Renderer::camera_target_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.target_set(x, y, z);
	}

	//相机：设置视场角（弧度）
	void Renderer::camera_fov_set(float radians)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.fov_set(radians);
	}

	//相机：设置近远平面
	void Renderer::camera_near_far_set(float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.near_far_set(near_plane, far_plane);
	}

	//切换正交 / 透视投影（第一百六十四轮）
	void Renderer::camera_ortho_set(bool enabled, float ortho_height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.ortho_set(enabled, ortho_height);
	}

	//设置相机上方向（第一百八十六轮）
	void Renderer::camera_up_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//全零向量会使视图矩阵退化，整次忽略
		if (x == 0.0f && y == 0.0f && z == 0.0f)
		{
			logger.warn("渲染器:: 相机上方向全零，整次忽略");
			return;
		}
		//非有限分量按忽略处理
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
			return;
		impl->camera.up_set(x, y, z);
	}

	//设置宽高比覆盖（第一百八十六轮）
	void Renderer::camera_aspect_set(float aspect)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.aspect_override_set(aspect);
	}

	//查询当前生效宽高比（第一百八十六轮）
	float Renderer::camera_aspect_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->camera.aspect_effective_get();
	}

	//查询视场角（第一百六十四轮）
	bool Renderer::camera_fov_get(float* radians_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.fov_get(radians_out);
		return true;
	}

	//查询投影模式与正交高度（第一百六十四轮）
	bool Renderer::camera_ortho_get(bool* enabled_out, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.ortho_get(enabled_out, height_out);
		return true;
	}

	//查询指定视深处可见的垂直世界高度（第一百六十五轮）
	bool Renderer::camera_world_height_at(float view_depth, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.world_height_at_get(view_depth, height_out);
		return true;
	}

	//世界点 → 屏幕坐标（第一百四十九轮）
	bool Renderer::camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.project_to_screen(world, u_out, v_out, depth_out);
	}

	//屏幕坐标 → 世界点（第一百四十九轮）
	bool Renderer::camera_unproject(float u, float v, float depth, float world_out[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.unproject_from_screen(u, v, depth, world_out);
	}

	//世界空间 AABB 是否与视锥相交（第一百六十轮）
	bool Renderer::camera_aabb_visible(const float min[3], const float max[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交相机模块（六面保守判定）
		return impl->camera.aabb_visible(min, max);
	}

	//导出相机「视图 × 投影」矩阵（第一百六十一轮）
	bool Renderer::camera_view_proj(float out[16]) const
	{
		//未初始化或出参为空则拒绝
		if (impl == nullptr || !impl->initialized || out == nullptr)
			return false;
		//写出列主序矩阵
		impl->camera.view_proj_get(out);
		return true;
	}

	//应用场景描述（三组入参；第四十七轮）
	void Renderer::scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
		相机参数校验（处置 R-FACADE-10）
		视场角须落在 (0, π)、近平面须为正、远平面须大于近平面；任一非法即整次拒绝，
		保持既有相机与清屏色不变。用「取反比较」写法使 NaN 同样被判为非法。
		*/
		bool camera_valid = (camera.fov > 0.0f) && (camera.fov < 3.14159265f)
			&& (camera.near_plane > 0.0f) && (camera.far_plane > camera.near_plane)
			//正交模式额外要求垂直可见高度为正（否则投影矩阵退化）
			&& (!camera.ortho || camera.ortho_height > 0.0f);
		//若相机参数非法
		if (!camera_valid)
		{
			logger.warn("渲染器:: 场景相机非法（视场角 {}、近平面 {}、远平面 {}），整次拒绝",
				camera.fov, camera.near_plane, camera.far_plane);
			return;
		}
		//相机位置 / 注视点
		impl->camera.position_set(camera.position[0], camera.position[1], camera.position[2]);
		impl->camera.target_set(camera.target[0], camera.target[1], camera.target[2]);
		//视场角与近远平面
		impl->camera.fov_set(camera.fov);
		impl->camera.near_far_set(camera.near_plane, camera.far_plane);
		//投影模式（正交 / 透视；高度合法性已在上方整体校验）
		impl->camera.ortho_set(camera.ortho, camera.ortho_height);
		//上方向与宽高比覆盖（第一百八十六轮；非法值由各自入口归一）
		impl->camera.up_set(camera.up[0], camera.up[1], camera.up[2]);
		impl->camera.aspect_override_set(camera.aspect);
		//清屏色（统一走 clear_color_set：逐分量钳制到 [0,1]）
		clear_color_set(camera.clear_color);
		//光照（统一走 light_set：非法参数由后者整次拒绝）
		light_set(light.direction, light.color, light.intensity, light.ambient);
		/*
		点光源组与镜面高光
		先按方向光重置点光源（light_set 已清空），再逐条追加；单条非法只跳过该条并告警，
		数量超过上限（第六十二轮起 8）的部分同样忽略。
		*/
		for (uint32_t index = 0; index < light.point_count; ++index)
			//超过上限的部分不再登记
			if (index >= Impl::POINT_LIGHT_MAX)
				break;
			else
				light_add_point(light.point_position[index], light.point_color[index],
					light.point_radius[index], light.point_intensity[index], light.ambient,
					light.point_attenuation[index], light.point_specular[index], light.point_shininess[index]);
		/*
		聚光灯组
		在点光源之后逐条追加；两类光源共用 8 盏上限，单条非法（含锥角非法）或已满时只跳过该条。
		*/
		for (uint32_t index = 0; index < light.spot_count; ++index)
			//超过上限的部分不再登记
			if (index >= Impl::POINT_LIGHT_MAX)
				break;
			else
				light_add_spot(light.spot_position[index], light.spot_direction[index],
					light.spot_color[index], light.spot_radius[index], light.spot_intensity[index],
					light.spot_inner_deg[index], light.spot_outer_deg[index], light.ambient,
					light.spot_attenuation[index], light.spot_specular[index], light.spot_shininess[index]);
		specular_set(light.specular_intensity, light.specular_shininess);
		//距离衰减模式（非法值由 light_attenuation_set 拒绝并保持当前模式）
		light_attenuation_set(light.attenuation_mode);
		//距离雾（按模式分派；非法参数由 fog_set / fog_exp2_set 整次忽略）
		if (fog.mode == 1)
			fog_exp2_set(fog.color, fog.density, fog.start);
		else
			fog_set(fog.color, fog.density, fog.start, fog.end);
		//雾高度衰减（非法值由 fog_height_set 整次忽略）
		fog_height_set(fog.height_falloff);
		//环境光颜色（非法分量由 ambient_color_set 整次忽略）
		ambient_color_set(light.ambient_color);
		//半球环境光（地面颜色 ＋ 混合强度；非法颜色由 ambient_ground_set 整次忽略）
		ambient_ground_set(light.ambient_ground, light.ambient_hemisphere);
		//补光（第二盏方向光；强度 0 = 关闭；第五十轮含逐光高光缩放）
		light_fill_set(light.fill_direction, light.fill_color, light.fill_intensity, light.fill_specular);
		//方向光逐光高光缩放（第五十轮；非法值由 setter 忽略）
		directional_specular_set(light.directional_specular);
		//方向光阴影开关与偏置（第五十二轮）
		shadow_enable_set(light.shadow_enabled != 0);
		shadow_bias_set(light.shadow_bias);
		//阴影软度与强度（第五十三轮；非法值由 setter 整次忽略）
		shadow_softness_set(light.shadow_softness);
		shadow_strength_set(light.shadow_strength);
		//阴影图分辨率（第五十四轮；非法值由 setter 整次忽略）
		shadow_resolution_set(static_cast<uint32_t>(light.shadow_resolution));
		//PBR-lite 镜面（第六十四轮；非法缩放由 setter 整次忽略）
		pbr_enable_set(light.pbr_enabled != 0);
		pbr_roughness_set(light.pbr_roughness);
		//PBR 金属度（第六十五轮；非法值由 setter 整次忽略）
		pbr_metallic_set(light.pbr_metallic);
		//二次元风格化（第一百七十四轮；非法参数由 setter 归一）
		toon_set(light.toon_steps, light.toon_rim);
		//二次元暗部色调（第一百七十八轮）
		toon_shadow_tint_set(light.toon_shadow_tint[0], light.toon_shadow_tint[1], light.toon_shadow_tint[2]);
		//二次元卡通高光（第一百七十九轮；非法值由 setter 归一）
		toon_specular_set(light.toon_specular);
		//反向外扩描边（第二百二十八轮；非法值由 setter 归一）
		outline_set(light.outline_width, light.outline_color);
		//若点光源数量超过上限则告警（已登记前 8 盏）
		if (light.point_count > Impl::POINT_LIGHT_MAX)
			logger.warn("渲染器:: 场景点光源数量 {} 超过上限 {}，仅登记前 {} 盏",
				light.point_count, Impl::POINT_LIGHT_MAX, Impl::POINT_LIGHT_MAX);
		//若聚光灯数量超过上限则告警（点光源与聚光灯共用 8 盏上限）
		if (light.spot_count > Impl::POINT_LIGHT_MAX)
			logger.warn("渲染器:: 场景聚光灯数量 {} 超过上限 {}，仅登记前 {} 盏（与点光源共用上限）",
				light.spot_count, Impl::POINT_LIGHT_MAX, Impl::POINT_LIGHT_MAX);
	}

	//应用场景描述（兼容扁平聚合；内部拆成三组后转发）
	void Renderer::scene_apply(const Scene_Data& scene)
	{
		//相机组
		Camera_Data camera{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			camera.position[index] = scene.position[index];
			camera.target[index] = scene.target[index];
		}
		camera.fov = scene.fov;
		camera.near_plane = scene.near_plane;
		camera.far_plane = scene.far_plane;
		camera.ortho = scene.ortho;
		camera.ortho_height = scene.ortho_height;
		//上方向与宽高比覆盖（第一百八十六轮）
		for (std::size_t index = 0; index < 3; ++index)
			camera.up[index] = scene.up[index];
		camera.aspect = scene.aspect;
		for (std::size_t index = 0; index < 4; ++index)
			camera.clear_color[index] = scene.clear_color[index];
		//光照组
		Light_Data light{};
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.direction[index] = scene.light_direction[index];
			light.color[index] = scene.light_color[index];
			light.ambient_color[index] = scene.ambient_color[index];
			light.ambient_ground[index] = scene.ambient_ground[index];
			light.fill_direction[index] = scene.fill_direction[index];
			light.fill_color[index] = scene.fill_color[index];
		}
		light.ambient_hemisphere = scene.ambient_hemisphere;
		light.fill_intensity = scene.fill_intensity;
		light.directional_specular = scene.directional_specular;
		light.fill_specular = scene.fill_specular;
		light.shadow_enabled = scene.shadow_enabled;
		light.shadow_bias = scene.shadow_bias;
		light.shadow_softness = scene.shadow_softness;
		light.shadow_strength = scene.shadow_strength;
		light.shadow_resolution = scene.shadow_resolution;
		//PBR-lite 镜面（第六十四轮）
		light.pbr_enabled = scene.pbr_enabled;
		light.pbr_roughness = scene.pbr_roughness;
		//二次元风格化（第一百七十四轮）
		light.toon_steps = scene.toon_steps;
		light.toon_rim = scene.toon_rim;
		//二次元暗部色调（第一百七十八轮）
		for (std::size_t index = 0; index < 3; ++index)
			light.toon_shadow_tint[index] = scene.toon_shadow_tint[index];
		//二次元卡通高光（第一百七十九轮）
		light.toon_specular = scene.toon_specular;
		//二次元反向外扩描边（第二百二十八轮）
		light.outline_width = scene.outline_width;
		for (std::size_t index = 0; index < 3; ++index)
			light.outline_color[index] = scene.outline_color[index];
		//PBR 金属度（第六十五轮）
		light.pbr_metallic = scene.pbr_metallic;
		light.intensity = scene.light_intensity;
		light.ambient = scene.light_ambient;
		light.point_count = scene.light_count;
		for (uint32_t index = 0; index < Impl::POINT_LIGHT_MAX; ++index)
		{
			for (std::size_t each = 0; each < 3; ++each)
			{
				light.point_position[index][each] = scene.point_light_position[index][each];
				light.point_color[index][each] = scene.point_light_color[index][each];
				light.spot_position[index][each] = scene.spot_light_position[index][each];
				light.spot_direction[index][each] = scene.spot_light_direction[index][each];
				light.spot_color[index][each] = scene.spot_light_color[index][each];
			}
			light.point_radius[index] = scene.point_light_radius[index];
			light.point_intensity[index] = scene.point_light_intensity[index];
			light.point_attenuation[index] = scene.point_light_attenuation[index];
			light.point_specular[index] = scene.point_light_specular[index];
			light.point_shininess[index] = scene.point_light_shininess[index];
			light.spot_radius[index] = scene.spot_light_radius[index];
			light.spot_intensity[index] = scene.spot_light_intensity[index];
			light.spot_inner_deg[index] = scene.spot_light_inner_deg[index];
			light.spot_outer_deg[index] = scene.spot_light_outer_deg[index];
			light.spot_attenuation[index] = scene.spot_light_attenuation[index];
			light.spot_specular[index] = scene.spot_light_specular[index];
			light.spot_shininess[index] = scene.spot_light_shininess[index];
		}
		light.specular_intensity = scene.specular_intensity;
		light.specular_shininess = scene.specular_shininess;
		light.spot_count = scene.spot_light_count;
		light.attenuation_mode = scene.attenuation_mode;
		//雾组
		Fog_Data fog{};
		for (std::size_t index = 0; index < 3; ++index)
			fog.color[index] = scene.fog_color[index];
		fog.density = scene.fog_density;
		fog.start = scene.fog_start;
		fog.end = scene.fog_end;
		fog.mode = scene.fog_mode;
		fog.height_falloff = scene.fog_height_falloff;
		//转发三组入参
		scene_apply(camera, light, fog);
	}

	//设置方向光与环境光（第二十九轮新增）
	void Renderer::light_set(const float direction[3], const float color[3], float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验
		方向长度须非零（否则无法归一化）、颜色各分量须落在 [0,1]、强度须非负且有限、环境光须落在 [0,1]；
		用「取反比较」写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 光照参数非法（方向 ({}, {}, {})、强度 {}、环境光 {}），整次拒绝",
				direction[0], direction[1], direction[2], intensity, ambient);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->light_direction[index] = direction[index] * inverse_length;
			impl->light_color[index] = color[index];
		}
		impl->light_intensity = intensity;
		impl->light_ambient = ambient;
		//切回「仅方向光」（清空点光源；第三十二轮）
		impl->point_light_count = 0;
	}

	//设置点光源（第三十一轮新增）
	void Renderer::light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return;
		/*
		参数校验（与 light_set 同口径，另加「半径须为正且有限」）
		用取反比较写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、环境光 {}），整次拒绝",
				position[0], position[1], position[2], radius, intensity, ambient);
			return;
		}
		//写入点光源参数（位置即「光位置」，由着色器按模式选择语义）
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->point_lights[0].position[index] = position[index];
			impl->point_lights[0].color[index] = color[index];
			//方向光颜色保持既有值
		}
		impl->point_lights[0].radius = radius;
		impl->point_lights[0].intensity = intensity;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		impl->point_lights[0].is_spot = false;
		impl->point_light_count = 1;
		impl->light_ambient = ambient;
		//关闭方向光（第三十二轮：单点光源入口等价于「仅一个点光源」）
		impl->light_intensity = 0.0f;
	}

	//追加一个点光源（第三十二轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer::light_add_point(const float position[3], const float color[3], float radius, float intensity,
		float ambient, uint32_t attenuation, float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return false;
		/*
		参数校验（与 light_point_set 同口径）＋ 容量校验
		任一非法或已满即返回假并告警，既有光源保持不变。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 追加点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}），拒绝",
				position[0], position[1], position[2], radius, intensity);
			return false;
		}
		//若已达上限
		if (impl->point_light_count >= Impl::POINT_LIGHT_MAX)
		{
			logger.warn("渲染器:: 点光源数量已达上限（{}），拒绝追加", Impl::POINT_LIGHT_MAX);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		light.is_spot = false;
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//追加一个聚光灯（第三十四轮新增；第四十六轮逐光源衰减；第四十九轮逐盏高光缩放；第五十一轮逐盏光泽度）
	bool Renderer::light_add_spot(const float position[3], const float direction[3], const float color[3],
		float radius, float intensity, float inner_deg, float outer_deg, float ambient, uint32_t attenuation,
		float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || direction == nullptr || color == nullptr)
			return false;
		/*
		参数校验（点光源同口径 ＋ 方向非退化 ＋ 锥角关系）
		方向长度须非零；锥角须满足 0 < 内角 < 外角 ≤ 90；
		用取反比较写法使 NaN 同样被判为非法，任一非法即拒绝并保持既有光源。
		*/
		float direction_length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (direction_length_squared > 1e-12f) && (radius > 0.0f) && (radius <= 1e6f)
			&& (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			&& (inner_deg > 0.0f) && (inner_deg < outer_deg) && (outer_deg <= 90.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 聚光灯参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、内角 {}、外角 {}），拒绝",
				position[0], position[1], position[2], radius, intensity, inner_deg, outer_deg);
			return false;
		}
		//若已达上限
		if (impl->point_light_count >= Impl::POINT_LIGHT_MAX)
		{
			logger.warn("渲染器:: 光源数量已达上限（{}），拒绝追加聚光灯", Impl::POINT_LIGHT_MAX);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		float inverse_length = 1.0f / std::sqrt(direction_length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
			//方向归一化（内部恒存单位向量）
			light.spot_direction[index] = direction[index] * inverse_length;
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		light.is_spot = true;
		//锥角转余弦（度 → 弧度；内角更小故其余弦更大）
		constexpr float degrees_to_radians = 0.017453292519943295f;
		light.spot_inner_cos = std::cos(inner_deg * degrees_to_radians);
		light.spot_outer_cos = std::cos(outer_deg * degrees_to_radians);
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	//清空全部点光源（第三十二轮新增）
	void Renderer::lights_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//清空计数并恢复方向光默认强度（方向 / 颜色 / 环境光保持既有值）
		impl->point_light_count = 0;
		impl->light_intensity = 1.0f;
	}

	//已登记点光源数量（第九十二轮）
	uint32_t Renderer::point_light_count(void) const
	{
		//未初始化视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->point_light_count;
	}

	//读取第 index 盏点光源的状态快照（第九十二轮）
	bool Renderer::point_light_state_get(uint32_t index, Point_Light_State& state_out) const
	{
		//未初始化或越界返回假（出参保持不变）
		if (impl == nullptr || !impl->initialized || index >= impl->point_light_count
			|| index >= Impl::POINT_LIGHT_MAX)
			return false;
		//内部记录 → 公开快照
		const Impl::Point_Light& light = impl->point_lights[index];
		state_out = Point_Light_State{};
		for (std::size_t each = 0; each < 3; ++each)
		{
			state_out.position[each] = light.position[each];
			state_out.color[each] = light.color[each];
			state_out.spot_direction[each] = light.spot_direction[each];
		}
		state_out.radius = light.radius;
		state_out.intensity = light.intensity;
		state_out.attenuation = light.attenuation;
		state_out.specular_scale = light.specular_scale;
		state_out.shininess = light.shininess;
		state_out.is_spot = light.is_spot;
		state_out.spot_inner_cos = light.spot_inner_cos;
		state_out.spot_outer_cos = light.spot_outer_cos;
		return true;
	}

	//设置镜面高光参数（第三十一轮新增）
	void Renderer::specular_set(float intensity, float shininess)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验（取反比较使 NaN 判非法）
		if (!(intensity >= 0.0f && intensity <= 1e6f) || !(shininess >= 1.0f && shininess <= 1e6f))
		{
			logger.warn("渲染器:: 高光参数非法（强度 {}、光泽度 {}），整次拒绝", intensity, shininess);
			return;
		}
		impl->specular_intensity = intensity;
		impl->specular_shininess = shininess;
	}

	//设置距离衰减模式（第三十六轮新增）
	bool Renderer::light_attenuation_set(uint32_t mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//仅接受线性（0）与平方反比（1）
		if (mode > 1)
		{
			logger.warn("渲染器:: 距离衰减模式非法（{}），仅接受 0（线性）/ 1（平方反比）", mode);
			return false;
		}
		impl->attenuation_mode = mode;
		return true;
	}

	//读取当前距离衰减模式（第三十六轮新增）
	uint32_t Renderer::light_attenuation_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->attenuation_mode;
	}

	//设置距离雾（第三十八轮新增）
	void Renderer::fog_set(const float color[3], float density, float start, float end)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始非负、结束大于起始；
		任一非法即整次忽略并告警（保持既有雾参数）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f) && (end > start) && (end <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 雾参数非法（密度 {}、起始 {}、结束 {}），整次忽略", density, start, end);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			impl->fog_color[index] = color[index];
		impl->fog_density = density;
		impl->fog_start = start;
		impl->fog_end = end;
		//切回线性区间模式（第一百四十三轮）
		impl->fog_mode = 0;
	}

	//设置指数平方雾（第一百四十三轮新增）
	void Renderer::fog_exp2_set(const float color[3], float density, float start)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始须非负且有限；
		任一非法即整次忽略并告警（保持既有雾参数与模式）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 指数平方雾参数非法（密度 {}、起始 {}），整次忽略", density, start);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			impl->fog_color[index] = color[index];
		impl->fog_density = density;
		impl->fog_start = start;
		//切到指数平方模式
		impl->fog_mode = 1;
	}

	//读取当前雾模式（第一百四十三轮新增）
	uint32_t Renderer::fog_mode_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->fog_mode;
	}

	//设置雾的高度衰减（第一百四十三轮新增）
	void Renderer::fog_height_set(float falloff)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//系数须在 [0, 10]（取反比较使 NaN 判非法）
		if (!(falloff >= 0.0f && falloff <= 10.0f))
		{
			logger.warn("渲染器:: 雾高度衰减系数非法（{}），整次忽略", falloff);
			return;
		}
		impl->fog_height_falloff = falloff;
	}

	//关闭距离雾（第三十八轮新增）
	void Renderer::fog_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//密度归零即关闭（颜色与区间保持既有值，便于再次开启）
		impl->fog_density = 0.0f;
	}

	//设置环境光颜色（第四十轮新增）
	void Renderer::ambient_color_set(const float color[3])
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 环境光颜色非法（{}），整次忽略", color[index]);
				return;
			}
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_color[index] = color[index];
	}

	//设置半球环境光（第四十七轮新增）
	void Renderer::ambient_ground_set(const float ground_color[3], float hemisphere)
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || ground_color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(ground_color[index] >= 0.0f && ground_color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 半球环境光颜色非法（{}），整次忽略", ground_color[index]);
				return;
			}
		//强度按 0..1 钳制（非数按 0 = 关闭处理）
		float blend = 0.0f;
		//仅有限值参与混合
		if (hemisphere >= 0.0f && hemisphere <= 1.0f)
			blend = hemisphere;
		else if (hemisphere > 1.0f)
			blend = 1.0f;
		//写入地面颜色与混合强度
		for (std::size_t index = 0; index < 3; ++index)
			impl->ambient_ground[index] = ground_color[index];
		impl->ambient_ground[3] = blend;
	}

	//设置补光（第四十七轮新增；第二盏方向光；第五十轮支持逐光高光缩放）
	void Renderer::light_fill_set(const float direction[3], const float color[3], float intensity, float specular)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验（口径同 light_set）
		方向长度须非零、颜色各分量须落在 [0,1]、强度须非负且有限、高光缩放须落在 0..100；任一非法即整次拒绝。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool fill_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (specular >= 0.0f) && (specular <= 100.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fill_valid = false;
		//若补光参数非法
		if (!fill_valid)
		{
			logger.warn("渲染器:: 补光参数非法（强度 {}、高光缩放 {}），整次拒绝", intensity, specular);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->fill_direction[index] = direction[index] * inverse_length;
			impl->fill_color[index] = color[index];
		}
		impl->fill_intensity = intensity;
		impl->fill_specular = specular;
	}

	//设置方向光的逐光高光缩放（第五十轮新增）
	void Renderer::directional_specular_set(float scale)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//缩放须落在 0..100（取反比较使 NaN 判非法）
		if (!(scale >= 0.0f && scale <= 100.0f))
		{
			logger.warn("渲染器:: 方向光高光缩放非法（{}），整次忽略", scale);
			return;
		}
		impl->directional_specular = scale;
	}

	//开关方向光阴影（第五十二轮新增）
	void Renderer::shadow_enable_set(bool enabled)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		/*
	首次开启时懒登记阴影管线
	未启用阴影的进程不承担管线创建开销；登记失败则保持关闭并告警（不改变其余状态）。
	阴影目标为 R8G8B8A8_UNORM（见 init ⑳ ①），故该管线必须按同一颜色格式创建，
	不能经公开 pipeline_load（它固定跟随交换链格式，sRGB 会破坏打包深度）。
	*/
	if (enabled && impl->shadow_pipeline == 0)
	{
		//加载阴影着色器模块（顺带解析顶点输入位置）
		VkShaderModule shadow_vert = VK_NULL_HANDLE;
		VkShaderModule shadow_frag = VK_NULL_HANDLE;
		uint32_t shadow_input_locations = 0;
		if (!impl->shader.module_load(shadow_vert, "阴影.vert.spv", &shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：顶点着色器加载失败，阴影保持关闭");
			impl->shadow_enabled = false;
			return;
		}
		if (!impl->shader.module_load(shadow_frag, "阴影.frag.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：片元着色器加载失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shadow_enabled = false;
			return;
		}
		//按阴影目标的 UNORM 颜色格式登记具名管线
		uint64_t shadow_pipeline = 0;
		//阴影目标固定单采样（第九十六轮），故该管线采样数固定 1，不跟随 MSAA
		if (!impl->pipeline.pipeline_register(shadow_pipeline, shadow_vert, shadow_frag,
			Blend_Mode::Opaque, VK_FORMAT_R8G8B8A8_UNORM, true, 1, shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：图形管线创建失败，阴影保持关闭");
			impl->shader.module_destroy(shadow_vert);
			impl->shader.module_destroy(shadow_frag);
			impl->shadow_enabled = false;
			return;
		}
		//记录具名管线（着色器模块所有权在门面，随 pipeline_unload / shutdown 回收）
		Impl::Extra_Pipeline_Record record{};
		record.handle = shadow_pipeline;
		record.vert_module = shadow_vert;
		record.frag_module = shadow_frag;
		impl->extra_pipelines.push_back(record);
		impl->shadow_pipeline = shadow_pipeline;
	}
	impl->shadow_enabled = enabled;
	}

	//阴影是否启用（第五十二轮）
	bool Renderer::shadow_enabled(void) const
	{
		//未初始化一律按关闭处理
		return impl != nullptr && impl->initialized && impl->shadow_enabled;
	}

	//设置阴影深度偏置（第五十二轮）
	void Renderer::shadow_bias_set(float bias)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//偏置须落在 0..0.05（取反比较使 NaN 判非法）
		if (!(bias >= 0.0f && bias <= 0.05f))
		{
			logger.warn("渲染器:: 阴影深度偏置非法（{}），整次忽略", bias);
			return;
		}
		impl->shadow_bias = bias;
	}

	//当前阴影深度偏置（第五十二轮）
	float Renderer::shadow_bias(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_bias : 0.003f;
	}

	//设置阴影软度（第五十三轮新增）
	void Renderer::shadow_softness_set(float softness)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//软度须落在 0..1（取反比较使 NaN 判非法）
		if (!(softness >= 0.0f && softness <= 1.0f))
		{
			logger.warn("渲染器:: 阴影软度非法（{}），整次忽略", softness);
			return;
		}
		impl->shadow_softness = softness;
	}

	//当前阴影软度（第五十三轮）
	float Renderer::shadow_softness(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_softness : 0.0f;
	}

	//设置阴影强度（第五十三轮新增）
	void Renderer::shadow_strength_set(float strength)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//强度须落在 0..1（取反比较使 NaN 判非法）
		if (!(strength >= 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 阴影强度非法（{}），整次忽略", strength);
			return;
		}
		impl->shadow_strength = strength;
	}

	//当前阴影强度（第五十三轮）
	float Renderer::shadow_strength(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_strength : 1.0f;
	}

	/*
	设置阴影图分辨率（第五十四轮新增）
	仅接受 512 / 1024 / 2048；切换时先等尽在途帧，再建新目标、重新绑定各帧槽 binding 1，
	全部成功后才销毁旧目标（任一步失败均保持旧目标可用）。
	*/
	void Renderer::shadow_resolution_set(uint32_t resolution)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//仅接受三档边长
		if (resolution != 512 && resolution != 1024 && resolution != 2048)
		{
			logger.warn("渲染器:: 阴影分辨率非法（{}），整次忽略（仅支持 512 / 1024 / 2048）", resolution);
			return;
		}
		//与原值一致则无需重建
		if (resolution == impl->shadow_size)
			return;
		//等尽在途帧，避免重建期间仍被采样
		if (!impl->frame.frames_wait())
		{
			logger.error("渲染器:: 阴影分辨率切换失败：等待在途帧失败");
			return;
		}
		//旧阴影图视图（失败回绑用）
		VkImage old_image = VK_NULL_HANDLE;
		VkImageView old_view = VK_NULL_HANDLE;
		VkImageView old_depth_view = VK_NULL_HANDLE;
		VkExtent2D old_extent{};
		if (!impl->render_target.target_get(impl->shadow_target, old_image, old_view,
			old_depth_view, old_extent))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：旧阴影图查询失败");
			return;
		}
		//先建新目标（失败则旧目标原样保留）；颜色格式固定 UNORM，理由见 init ⑳ ①
		uint64_t candidate = 0;
		//阴影目标固定单采样（第九十六轮）
		if (!impl->render_target.target_create(candidate, resolution, resolution,
			VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影目标创建失败（保持 {}）", impl->shadow_size);
			return;
		}
		//取新目标颜色视图与默认采样器
		VkImage new_image = VK_NULL_HANDLE;
		VkImageView new_view = VK_NULL_HANDLE;
		VkImageView new_depth_view = VK_NULL_HANDLE;
		VkExtent2D new_extent{};
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!impl->render_target.target_get(candidate, new_image, new_view, new_depth_view, new_extent)
			|| !impl->texture.texture_bind_get(impl->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影图查询失败");
			impl->render_target.target_destroy(candidate);
			return;
		}
		//逐帧槽改绑新阴影图
		for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
		{
			//若某槽绑定失败，则把已改绑的槽全部回绑旧图并放弃新目标
			if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, new_view))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 绑定失败，回绑旧阴影图", slot);
				for (std::size_t revert = 0; revert <= slot; ++revert)
					impl->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
				impl->render_target.target_destroy(candidate);
				return;
			}
		}
		//全部改绑成功后销毁旧目标并更新状态
		impl->render_target.target_destroy(impl->shadow_target);
		impl->shadow_target = candidate;
		impl->shadow_size = resolution;
		//新目标尚无有效内容，下一帧必须重绘阴影图
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影图分辨率已切换为 {}", resolution);
	}

	//当前阴影图分辨率（第五十四轮）
	uint32_t Renderer::shadow_resolution(void) const
	{
		//未初始化返回默认值
		return impl != nullptr && impl->initialized ? impl->shadow_size : Impl::SHADOW_SIZE;
	}

	//设置阴影光源正交视锥范围（第一百零七轮）
	void Renderer::shadow_range_set(float half_extent, float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：半宽为正、近平面为正、远平面大于近平面（非数一并拒绝）
		if (!(half_extent > 0.0f) || !(near_plane > 0.0f) || !(far_plane > near_plane))
		{
			logger.warn("渲染器:: 阴影视锥参数非法（半宽 {}，近 {}，远 {}），本次忽略",
				half_extent, near_plane, far_plane);
			return;
		}
		impl->shadow_half_extent = half_extent;
		impl->shadow_near = near_plane;
		impl->shadow_far = far_plane;
		//光源空间矩阵随下一帧更新，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 阴影视锥已设为 半宽 {}，近 {}，远 {}", half_extent, near_plane, far_plane);
	}

	//当前阴影正交范围（第一百零七轮）
	void Renderer::shadow_range_get(float* half_extent_out, float* near_out, float* far_out) const
	{
		//默认值与未初始化一致
		float half_extent = 12.0f;
		float near_plane = 1.0f;
		float far_plane = 60.0f;
		//已初始化则取当前值
		if (impl != nullptr && impl->initialized)
		{
			half_extent = impl->shadow_half_extent;
			near_plane = impl->shadow_near;
			far_plane = impl->shadow_far;
		}
		if (half_extent_out != nullptr)
			*half_extent_out = half_extent;
		if (near_out != nullptr)
			*near_out = near_plane;
		if (far_out != nullptr)
			*far_out = far_plane;
	}

	//设置级联阴影配置（第一百二十一轮）
	void Renderer::shadow_cascade_set(uint32_t count, float far_scale, float split_distance)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验：级联数仅 1 / 2 / 3、远区倍数 > 1、切换距离 > 0（非数一并拒绝）
		if ((count != 1 && count != 2 && count != 3) || !(far_scale > 1.0f) || !(split_distance > 0.0f))
		{
			logger.warn("渲染器:: 级联阴影参数非法（级联数 {}，远区倍数 {}，切换距离 {}），本次忽略",
				count, far_scale, split_distance);
			return;
		}
		impl->cascade_count = count;
		impl->cascade_far_scale = far_scale;
		impl->cascade_split_distance = split_distance;
		//分片方式或范围改变，旧阴影图不再可信
		impl->shadow_pass_ready = false;
		logger.info("渲染器:: 级联阴影已设为 {} 级（远区倍数 {}，切换距离 {}，单片边长 {}）",
			count, far_scale, split_distance,
			count > 1 ? impl->shadow_size / count : impl->shadow_size);
	}

	//当前级联阴影配置（第一百二十一轮）
	void Renderer::shadow_cascade_get(uint32_t* count_out, float* far_scale_out, float* split_distance_out) const
	{
		//默认值与未初始化一致
		uint32_t count = 1;
		float far_scale = 3.0f;
		float split_distance = 12.0f;
		//已初始化则取当前值
		if (impl != nullptr && impl->initialized)
		{
			count = impl->cascade_count;
			far_scale = impl->cascade_far_scale;
			split_distance = impl->cascade_split_distance;
		}
		if (count_out != nullptr)
			*count_out = count;
		if (far_scale_out != nullptr)
			*far_scale_out = far_scale;
		if (split_distance_out != nullptr)
			*split_distance_out = split_distance;
	}

	//设置级联接缝淡化比例（第一百四十八轮）
	void Renderer::shadow_cascade_blend_set(float blend)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//比例须在 [0, 0.5]（取反比较使 NaN 判非法）
		if (!(blend >= 0.0f && blend <= 0.5f))
		{
			logger.warn("渲染器:: 级联接缝淡化比例非法（{}），本次忽略", blend);
			return;
		}
		impl->cascade_blend = blend;
	}

	//当前级联接缝淡化比例（第一百四十八轮）
	float Renderer::shadow_cascade_blend(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->cascade_blend;
	}

	//累计实际执行的阴影 pass 次数（第五十五轮）
	uint64_t Renderer::shadow_pass_count(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->shadow_pass_count : 0;
	}

	//当前聚光灯数量（第一百一十五轮）
	uint32_t Renderer::spot_light_count(void) const
	{
		//未初始化返回 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		//统计 is_spot 为真的光源
		uint32_t count = 0;
		for (uint32_t index = 0; index < impl->point_light_count; ++index)
			if (impl->point_lights[index].is_spot)
				++count;
		return count;
	}

	//资源计数快照（第一百一十三 / 一百一十四轮）
	Renderer_Resource_Counts Renderer::resource_counts(void) const
	{
		//默认全零
		Renderer_Resource_Counts counts{};
		//未初始化直接返回
		if (impl == nullptr || !impl->initialized)
			return counts;
		//各模块有效记录数
		counts.meshes = impl->mesh.mesh_count();
		counts.textures = impl->texture.texture_count();
		counts.pipelines = impl->pipeline.pipeline_count();
		counts.fonts = impl->font.font_count();
		counts.render_targets = impl->render_target.target_count();
		//材质按有效记录计
		for (const Impl::Material_Record& record : impl->materials)
			if (record.valid)
				++counts.materials;
		return counts;
	}

	//累计场景 UBO 上传字节数（第六十轮新增）
	uint64_t Renderer::scene_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->scene_upload_bytes : 0;
	}

	//PBR-lite 镜面开关（第六十四轮新增）
	void Renderer::pbr_enable_set(bool enabled)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		impl->pbr_enabled = enabled;
	}

	//PBR-lite 是否启用（第六十四轮新增）
	bool Renderer::pbr_enabled(void) const
	{
		//未初始化返回默认假
		return impl != nullptr && impl->initialized ? impl->pbr_enabled : false;
	}

	//PBR-lite 粗糙度缩放（第六十四轮新增）
	void Renderer::pbr_roughness_set(float scale)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//合法区间 0.05..4；越界或非数整次忽略
		if (!(scale >= 0.05f && scale <= 4.0f))
		{
			logger.warn("渲染器:: PBR 粗糙度缩放 {} 越界（0.05..4），已忽略", scale);
			return;
		}
		impl->pbr_roughness = scale;
	}

	//当前 PBR-lite 粗糙度缩放（第六十四轮新增）
	float Renderer::pbr_roughness(void) const
	{
		//未初始化返回默认 1
		return impl != nullptr && impl->initialized ? impl->pbr_roughness : 1.0f;
	}

	//PBR 金属度（第六十五轮新增）
	void Renderer::pbr_metallic_set(float metallic)
	{
		//未初始化静默返回
		if (impl == nullptr || !impl->initialized)
			return;
		//合法区间 0..1；越界或非数整次忽略
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: PBR 金属度 {} 越界（0..1），已忽略", metallic);
			return;
		}
		impl->pbr_metallic = metallic;
	}

	//当前 PBR 金属度（第六十五轮新增）
	float Renderer::pbr_metallic(void) const
	{
		//未初始化返回默认 0
		return impl != nullptr && impl->initialized ? impl->pbr_metallic : 0.0f;
	}

	/*
	二次元风格化 · 光照色阶量化与边缘光（第一百七十四轮新增）
	steps：0 = 关闭；1..8 = 量化档数（越界钳制）。
	rim：0 = 关闭；0..1（越界钳制，非有限值按 0）。
	两者均只写场景 UBO 的保留分量，默认值下画面逐字节不变。
	*/
	void Renderer::toon_set(uint32_t steps, float rim)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//色阶数钳到 [0, 8]
		impl->toon_steps = steps > 8 ? 8 : steps;
		//边缘光强度钳到 [0, 1]（非有限值按 0）
		float effective_rim = std::isfinite(rim) ? rim : 0.0f;
		impl->toon_rim = effective_rim > 1.0f ? 1.0f : (effective_rim > 0.0f ? effective_rim : 0.0f);
	}

	//当前色阶数（第一百七十四轮新增）
	uint32_t Renderer::toon_steps(void) const
	{
		//未初始化返回默认 0（关闭）
		return impl != nullptr && impl->initialized ? impl->toon_steps : 0;
	}

	//当前边缘光强度（第一百七十四轮新增）
	float Renderer::toon_rim(void) const
	{
		//未初始化返回默认 0（关闭）
		return impl != nullptr && impl->initialized ? impl->toon_rim : 0.0f;
	}

	/*
	设置反向外扩描边（第二百二十八轮）
	宽度钳到 [0, 1]（非有限值按 0），颜色逐分量钳到 [0, 1]（非有限值按 0）；
	描边管线未登记时仅记录参数（绘制项不会额外登记描边项，画面不变）。
	*/
	void Renderer::outline_set(float width, const float color[3])
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//外扩宽度钳到 [0, 1]（非有限值按 0 = 关闭）
		float effective_width = std::isfinite(width) ? width : 0.0f;
		impl->outline_width = effective_width > 1.0f ? 1.0f : (effective_width > 0.0f ? effective_width : 0.0f);
		//描边色逐分量钳到 [0, 1]（非有限值按 0）
		for (std::size_t index = 0; index < 3; ++index)
		{
			float channel = color != nullptr ? color[index] : 0.0f;
			if (!std::isfinite(channel))
				channel = 0.0f;
			impl->outline_color[index] = channel > 1.0f ? 1.0f : (channel > 0.0f ? channel : 0.0f);
		}
	}

	//关闭反向外扩描边（第二百二十八轮新增）
	void Renderer::outline_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->outline_width = 0.0f;
	}

	//当前外扩宽度（第二百二十八轮新增）
	float Renderer::outline_width(void) const
	{
		//未初始化返回默认 0（关闭）
		return impl != nullptr && impl->initialized ? impl->outline_width : 0.0f;
	}

	//读取当前描边色（第二百二十八轮新增）
	void Renderer::outline_color(float out[3]) const
	{
		//出参为空则忽略
		if (out == nullptr)
			return;
		//未初始化写全零（黑）
		if (impl == nullptr || !impl->initialized)
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = impl->outline_color[index];
	}

	//设置暗部色调（第一百七十八轮新增）
	void Renderer::toon_shadow_tint_set(float r, float g, float b)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//逐分量归一：非有限值按 1（白 = 关闭），否则钳到 [0, 1]
		const float channels[3] = { r, g, b };
		for (std::size_t index = 0; index < 3; ++index)
		{
			float value = std::isfinite(channels[index]) ? channels[index] : 1.0f;
			impl->toon_shadow_tint[index] = value > 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
		}
	}

	//读取暗部色调（第一百七十八轮新增）
	void Renderer::toon_shadow_tint(float out[3]) const
	{
		//出参为空则无内容可写
		if (out == nullptr)
			return;
		//未初始化时写默认白（关闭）
		const bool ready = impl != nullptr && impl->initialized;
		for (std::size_t index = 0; index < 3; ++index)
			out[index] = ready ? impl->toon_shadow_tint[index] : 1.0f;
	}

	//设置卡通高光阈值（第一百七十九轮新增）
	void Renderer::toon_specular_set(float threshold)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//阈值钳到 [0, 1]（非有限值按 0 = 关闭）
		const float effective = std::isfinite(threshold) ? threshold : 0.0f;
		impl->toon_specular = effective > 1.0f ? 1.0f : (effective > 0.0f ? effective : 0.0f);
	}

	//读取卡通高光阈值（第一百七十九轮新增）
	float Renderer::toon_specular(void) const
	{
		//未初始化返回默认 0（关闭）
		return impl != nullptr && impl->initialized ? impl->toon_specular : 0.0f;
	}

	//累计实例缓冲上传字节数（第六十一轮新增）
	uint64_t Renderer::instance_upload_bytes(void) const
	{
		//未初始化返回 0
		return impl != nullptr && impl->initialized ? impl->instance_upload_bytes : 0;
	}

	//读回阴影图（第五十七轮新增）
	bool Renderer::shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out)
	{
		//前置：已初始化、阴影已开启、阴影目标存在且参数齐备
		if (impl == nullptr || !impl->initialized || !impl->shadow_enabled || impl->shadow_target == 0
			|| pixels == nullptr || capacity == 0)
			return false;
		//等尽在途帧，避免与该目标的写入 / 采样冲突
		if (!impl->frame.frames_wait())
			return false;
		//转交渲染目标模块（同步拷贝并读回打包深度）
		return impl->render_target.target_readback(impl->shadow_target, pixels, capacity, width_out, height_out);
	}

	//设置清屏色（RGBA）
	void Renderer::clear_color_set(const float color[4])
	{
		//若尚未初始化或颜色指针为空
		if (impl == nullptr || !impl->initialized || color == nullptr)
			return;
		/*
		逐分量钳制到 [0,1] 后拷贝（自下一次 frame_render 起生效）
		越界与非数（NaN）均被归一：负 / 非数 → 0，大于 1 → 1；故清屏色恒落在合法色域内。
		*/
		for (std::size_t index = 0; index < 4; ++index)
		{
			//当前分量
			float value = color[index];
			//若为负或非数则归 0
			if (!(value >= 0.0f))
				value = 0.0f;
			//若大于 1 则归 1
			else if (value > 1.0f)
				value = 1.0f;
			impl->clear_color[index] = value;
		}
	}

	//UI 通道
	Ui_Channel_Context* Renderer::ui_channel(void)
	{
		//若实现体未分配
		if (impl == nullptr)
			return nullptr;
		return &impl->ui_channel;
	}
}
