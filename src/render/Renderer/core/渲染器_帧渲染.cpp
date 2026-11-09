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
	namespace
	{
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
					//逐级额外四参是否一致（第四百零一轮；场景项恒全零）
					if (post_same)
						post_same = memcmp(last.post_extra, item.post_extra, sizeof(last.post_extra)) == 0;
					//实例区是否首尾相接
					bool instance_adjacent = last.instance_buffer == item.instance_buffer
						&& last.instance_offset + static_cast<VkDeviceSize>(last.instance_count) * INSTANCE_STRIDE_BYTES
							== item.instance_offset;
					can_merge = push_same && post_same && instance_adjacent
						&& last.instance_input && item.instance_input
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
		阴影输入签名（第五十五轮；第一百二十一轮加级联信息；第六百四十四轮只混参与投影的实例）
		把光源空间矩阵（含远区级联矩阵与聚光瓦片）、**仅参与投影的绘制项实例矩阵**、投影项数、
		阴影图边长与级联数混入 64 位 FNV-1a；用于判定阴影图能否跨帧复用（完全一致才跳过重绘）。
		非投影项（文字 / 粒子等）的实例变化不再使签名变化，避免无谓的整张阴影重绘。
		*/
		uint64_t shadow_signature_build(const float light_view_proj[16], const float light_view_proj_far[16],
			const float light_view_proj_far2[16], const float spot_view_proj_tiles[128],
			const std::vector<Frame_Render_Item>& items,
			const std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& instances,
			std::size_t shadow_item_count, uint32_t shadow_size, uint32_t cascade_count)
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
			//聚光灯阴影矩阵（第四百八十四轮；第五百零七轮扩为 4 瓦片 64 浮点；第五百一十三轮扩为 8 瓦片 128 浮点）
			for (std::size_t index = 0; index < 128; ++index)
				mix_float(spot_view_proj_tiles[index]);
			/*
			实例矩阵（每实例 INSTANCE_FLOAT_COUNT 个 float）
			只遍历参与投影的绘制项，并按 instance_offset 定位其连续实例区间；
			越界（防御性）即停止该区间的混入，不影响已混内容。
			*/
			for (const Frame_Render_Item& item : items)
			{
				//非投影项跳过
				if (!item.cast_shadow)
					continue;
				//该项首个实例下标与其实例数
				const std::size_t first = static_cast<std::size_t>(item.instance_offset / INSTANCE_STRIDE_BYTES);
				const std::size_t count = static_cast<std::size_t>(item.instance_count);
				for (std::size_t instance = 0; instance < count; ++instance)
				{
					//实例下标越界则停止
					const std::size_t base = first + instance;
					if (base >= instances.size())
						break;
					//逐分量混入该实例矩阵
					for (std::size_t index = 0; index < INSTANCE_FLOAT_COUNT; ++index)
						mix_float(instances[base][index]);
				}
			}
			//投影项数与阴影图边长
			hash ^= static_cast<uint64_t>(shadow_item_count);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(shadow_size);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint64_t>(cascade_count);
			hash *= 1099511628211ull;
			return hash;
		}
	}

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

		//① 泵事件
		impl->platform.events_pump();
		/*
		①.5 帧输入锁存与原生 UI 交互
		事件泵已把 SDL 事件喂入输入聚合器；此处推进为「本帧快照」，再交给 UI 做命中测试与文本编辑。
		原生 UI 与外部 UI 共用同一 UI 阶段回调（见下方 ui_callback）。
		*/
		{
			//窗口客户区尺寸（UI 坐标与命中测试的基准）
			uint32_t input_width = 0;
			uint32_t input_height = 0;
			impl->platform.window_size_get(input_width, input_height);
			//锁存本帧输入（UI 不使用 dt，按 0 传）
			impl->input.frame_advance(0.0, input_width, input_height, impl->platform.display_scale_get());
			/*
			帧前准备：按上一帧系统焦点决定控件键盘总闸（不在 UI 主对象时不响应 Tab/回车），
			并按系统文本模式决定本地拼接或系统回传落文本（必须在 frame_begin 之前）。
			*/
			impl->ui_event.text_route_prepare(impl->ui);
			//控件树消费本帧输入
			impl->ui.frame_begin(impl->input.frame_get());
			//系统文本模式或 UI 文本框焦点都启停平台文本输入
			impl->input.text_input_enable(impl->ui.text_focus_active() || impl->ui_event.system_text_mode());
			/*
			UI / 渲染层事件发布（已接入时）
			Raw → 焦点系统；UI 命中 → MouseFocus；模态切换 → ModalPush/Pop；
			系统文本模式下的文本框提交 → TextSession。
			*/
			impl->ui_event.frame_publish(impl->input.frame_get(), impl->ui);
			/*
			应用已收到的系统事件
			FocusChanged → 控件系统焦点环；Command → 绑定控件激活；
			Text → 焦点文本框落文本。放在发布之后，使同帧收发即时生效。
			*/
			impl->ui_event.frame_apply(impl->ui);
		}
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
			//本帧生效纹素世界尺寸（多级取各片最大；第六百六十一轮）
			impl->shadow_texel_world = 0.0f;
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
					/*
					纹素尺寸钳制（第六百六十一轮）
					半径上限 = 纹素上限 × 分片边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
					*/
					const float tile_pixels = static_cast<float>(impl->shadow_size) / static_cast<float>(cascade_total);
					if (impl->shadow_texel_max > 0.0f)
					{
						const float radius_cap = impl->shadow_texel_max * tile_pixels * 0.5f;
						if (radius > radius_cap)
							radius = radius_cap;
					}
					//光源机位：沿逆光向退到「球半径 + 近平面」处，使整球都落在近平面之后
					glm::vec3 eye = center - light_dir * (radius + impl->shadow_near);
					glm::mat4 light_view = glm::lookAt(eye, center, up);
					//深度跨度：球直径 + 两端近平面余量
					float depth_far = 2.0f * radius + 2.0f * impl->shadow_near;
					glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, impl->shadow_near, depth_far);
					proj[1][1] *= -1.0f;
					glm::mat4 vp = proj * light_view;
					/*
					级联纹素对齐（第六百四十四轮）
					阴影视锥以相机为球心，相机移动会使整张阴影图逐帧平移（CSM 游泳 / 边缘闪烁）。
					故把光源空间裁剪坐标的平移量（X / Y）吸附到本片纹素网格：NDC 步长 = 2 / 分片边长；
					Z（深度）不吸附，避免改变深度精度分布。
					*/
					const float texel_ndc = 2.0f / (tile_pixels > 1.0f ? tile_pixels : 1.0f);
					vp[3][0] = std::floor(vp[3][0] / texel_ndc) * texel_ndc;
					vp[3][1] = std::floor(vp[3][1] / texel_ndc) * texel_ndc;
					//按片序写入对应矩阵（第四片不存在，级联数上限为 3）
					float* target_matrix = cascade == 0 ? impl->light_view_proj
						: (cascade == 1 ? impl->light_view_proj_far : impl->light_view_proj_far2);
					std::memcpy(target_matrix, glm::value_ptr(vp), sizeof(float) * 16);
					//记录本片深度跨度（着色器法线偏移换算用）
					impl->cascade_depth_span[cascade] = depth_far - impl->shadow_near;
					/*
					记录本片纹素世界尺寸（第六百六十一轮）
					多级取各片最大值供诊断；钳制开启时该值不超过 shadow_texel_max。
					*/
					const float texel_world = 2.0f * radius / (tile_pixels > 1.0f ? tile_pixels : 1.0f);
					if (texel_world > impl->shadow_texel_world)
						impl->shadow_texel_world = texel_world;
				}
			}
			else
			{
				/*
				单级：以相机为中心的包围球（第六百五十五轮；修复单级固定原点）
				旧实现把光源机位固定在世界原点的逆光侧，相机离开原点后阴影整片丢失 / 复现；
				现改为「与多级同一拟合口径」：球心 = 相机位置、半径 = shadow_half_extent，
				深度跨度 = 球直径 ＋ 两端近平面余量，并同样做纹素对齐（相机平移不再重采样）。
				*/
				float camera_position[3] = {};
				impl->camera.position_get(camera_position);
				glm::vec3 center(camera_position[0], camera_position[1], camera_position[2]);
				float radius = impl->shadow_half_extent;
				/*
				纹素尺寸钳制（第六百六十一轮）
				半径上限 = 纹素上限 × 阴影图边长 / 2；上限为 0（默认）时不干预，矩阵逐字节不变。
				*/
				const float tile_pixels = impl->shadow_size > 1 ? static_cast<float>(impl->shadow_size) : 1.0f;
				if (impl->shadow_texel_max > 0.0f)
				{
					const float radius_cap = impl->shadow_texel_max * tile_pixels * 0.5f;
					if (radius > radius_cap)
						radius = radius_cap;
				}
				glm::vec3 eye = center - light_dir * (radius + impl->shadow_near);
				glm::mat4 light_view = glm::lookAt(eye, center, up);
				float depth_far = 2.0f * radius + 2.0f * impl->shadow_near;
				glm::mat4 light_proj = glm::ortho(-radius, radius, -radius, radius, impl->shadow_near, depth_far);
				light_proj[1][1] *= -1.0f;
				glm::mat4 light_vp = light_proj * light_view;
				/*
				纹素对齐（与多级同口径）
				NDC 步长 = 2 / 阴影图边长；Z（深度）不吸附。
				*/
				const float texel_ndc = 2.0f / tile_pixels;
				light_vp[3][0] = std::floor(light_vp[3][0] / texel_ndc) * texel_ndc;
				light_vp[3][1] = std::floor(light_vp[3][1] / texel_ndc) * texel_ndc;
				std::memcpy(impl->light_view_proj, glm::value_ptr(light_vp), sizeof(float) * 16);
				//单级：远区矩阵为零，三处深度跨度均取本片跨度（着色器按级联数取用）
				std::memset(impl->light_view_proj_far, 0, sizeof(impl->light_view_proj_far));
				std::memset(impl->light_view_proj_far2, 0, sizeof(impl->light_view_proj_far2));
				impl->cascade_depth_span[0] = depth_far - impl->shadow_near;
				impl->cascade_depth_span[1] = impl->cascade_depth_span[0];
				impl->cascade_depth_span[2] = impl->cascade_depth_span[0];
				//单级纹素世界尺寸（第六百六十一轮；钳制开启时不超过 shadow_texel_max）
				impl->shadow_texel_world = 2.0f * radius / tile_pixels;
			}
			/*
			级联切换带自动淡化（第六百六十一轮）
			显式比例为 0、级联数 ≥ 2 且开关开启时，按「切换带半宽 ≥ 2 纹素」给出建议比例 4 / 分片边长，
			钳到 [0.005, 0.1]；显式比例 > 0 或开关关闭（默认）时保持显式值，画面逐字节不变。
			*/
			impl->cascade_blend_effective = impl->cascade_blend;
			if (impl->cascade_blend_auto && impl->cascade_blend <= 0.0f && impl->cascade_count >= 2)
			{
				const float cascade_total_f = static_cast<float>(impl->cascade_count == 3 ? 3u : 2u);
				const float tile_pixels_f = impl->shadow_size > 1
					? static_cast<float>(impl->shadow_size) / cascade_total_f : 1.0f;
				float recommended = 4.0f / tile_pixels_f;
				impl->cascade_blend_effective = recommended < 0.005f ? 0.005f : (recommended > 0.1f ? 0.1f : recommended);
			}
		}
		/*
		聚光灯阴影矩阵（第四百八十四轮；第五百零七轮扩为多盏）
		两路径：
		① spot_shadow_all 开启：前 4 盏聚光灯按声明顺序各占瓦片 0..3，各写一份矩阵与跨度；
		② 关闭：仅 spot_shadow_index 所选槽位写入瓦片 0（与旧单盏行为逐字节一致）。
		同时把「前向槽位 → 瓦片号」写入槽位映射，着色器按灯查找对应阴影图。
		*/
		for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
		{
			std::memset(impl->spot_view_proj_tiles[tile], 0, sizeof(impl->spot_view_proj_tiles[tile]));
			impl->spot_shadow_spans[tile] = 0.0f;
			//逐瓦片偏置默认取全局值（第五百二十一轮；无自校准时与引入前一致；第六百五十八轮乘逐盏缩放）
			float slot_scale = 1.0f;
			if (impl->spot_shadow_index >= 0 && impl->spot_shadow_index < 8)
				slot_scale = impl->spot_shadow_bias_scale[impl->spot_shadow_index];
			impl->spot_shadow_bias_tiles[tile] = impl->spot_shadow_bias * slot_scale;
			//逐瓦片质量缩放默认 1（第七百零八轮；无逐盏设置时与全局参数相同）
			impl->spot_shadow_softness_tiles[tile] = 1.0f;
			impl->spot_shadow_strength_tiles[tile] = 1.0f;
		}
		for (std::size_t slot = 0; slot < 32; ++slot)
			impl->spot_shadow_slot_tiles[slot] = -1.0f;
		//是否存在至少一盏有效聚光阴影
		bool spot_shadow_light_valid = false;
		//可投影的光源槽位（最多 8 盏）与数量
		int candidates[Impl::SPOT_SHADOW_TILES] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		//本帧有效瓦片数
		std::size_t candidate_count = 0;
		if (impl->spot_shadow_enabled)
		{
			if (impl->spot_shadow_all)
			{
				/*
				多盏：优先「瓦片号 = 光源槽位」（第六百四十四轮；第七百零九轮加优先级）
				可投影聚光灯全部落在槽位 0..7 时按槽位固定，光源增减 / 重排不再使其它灯的瓦片整体前移；
				否则按优先级降序、同分槽位升序取前 8 盏，再按槽位升序分配到瓦片 0..7（默认优先级全 0 → 取登记顺序最前的 8 盏，
				与引入优先级前口径一致）。
				*/
				std::size_t spot_pool[32] = {};
				std::size_t spot_pool_count = 0;
				bool slot_mapping = true;
				for (std::size_t index = 0; index < impl->point_light_count && index < 32; ++index)
				{
					//非聚光灯跳过
					if (!impl->point_lights[index].is_spot)
						continue;
					//该槽位的聚光阴影被关闭则跳过（第六百四十八轮；槽位 ≥ 8 不适用）
					if (index < 8 && !impl->spot_shadow_light_enabled[index])
						continue;
					//槽位超出 8 时无法按槽位固定瓦片号
					if (index >= Impl::SPOT_SHADOW_TILES)
						slot_mapping = false;
					spot_pool[spot_pool_count++] = index;
				}
				if (slot_mapping)
				{
					for (std::size_t entry = 0; entry < spot_pool_count; ++entry)
						candidates[spot_pool[entry]] = static_cast<int>(spot_pool[entry]);
				}
				else
				{
					//超过 8 盏：按优先级降序、同分槽位升序取前 8（简单选择排序，池 ≤ 32）
					const std::size_t keep = spot_pool_count < Impl::SPOT_SHADOW_TILES
						? spot_pool_count : Impl::SPOT_SHADOW_TILES;
					for (std::size_t pick = 0; pick < keep; ++pick)
					{
						std::size_t best = pick;
						for (std::size_t probe = pick + 1; probe < spot_pool_count; ++probe)
						{
							const float probe_priority = impl->spot_shadow_priority[spot_pool[probe]];
							const float best_priority = impl->spot_shadow_priority[spot_pool[best]];
							//优先级更高者优先；同分保持较小槽位在前
							if (probe_priority > best_priority)
								best = probe;
						}
						if (best != pick)
						{
							const std::size_t swap = spot_pool[pick];
							spot_pool[pick] = spot_pool[best];
							spot_pool[best] = swap;
						}
					}
					//选中集合按槽位升序分配到瓦片 0..keep-1
					for (std::size_t first = 0; first < keep; ++first)
						for (std::size_t second = first + 1; second < keep; ++second)
							if (spot_pool[second] < spot_pool[first])
							{
								const std::size_t swap = spot_pool[first];
								spot_pool[first] = spot_pool[second];
								spot_pool[second] = swap;
							}
					for (std::size_t tile = 0; tile < keep; ++tile)
						candidates[tile] = static_cast<int>(spot_pool[tile]);
				}
				//统计本帧有效瓦片数
				for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
					if (candidates[tile] >= 0)
						++candidate_count;
			}
			//单盏：仅所选槽位且必须是聚光灯（瓦片固定为 0）
			else if (impl->spot_shadow_index >= 0 && impl->spot_shadow_index < static_cast<int>(impl->point_light_count)
				&& impl->point_lights[impl->spot_shadow_index].is_spot
				//该槽位的聚光阴影被关闭则不投影（第六百四十八轮）
				&& (impl->spot_shadow_index >= 8 || impl->spot_shadow_light_enabled[impl->spot_shadow_index]))
			{
				candidates[0] = impl->spot_shadow_index;
				candidate_count = 1;
			}
			for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
			{
				//该瓦片无对应光源则跳过
				if (candidates[tile] < 0)
					continue;
				const Impl::Point_Light& spot = impl->point_lights[static_cast<std::size_t>(candidates[tile])];
				glm::vec3 spot_direction(spot.spot_direction[0], spot.spot_direction[1], spot.spot_direction[2]);
				//方向退化时取默认传播方向
				if (glm::dot(spot_direction, spot_direction) < 1e-12f)
					spot_direction = glm::vec3(0.0f, 0.0f, -1.0f);
				spot_direction = glm::normalize(spot_direction);
				//上向量与光向平行时改用 X 轴（避免 lookAt 退化）
				glm::vec3 spot_up = glm::abs(spot_direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
				glm::vec3 spot_position(spot.position[0], spot.position[1], spot.position[2]);
				//外锥余弦反解半锥角（钳到 (0.02, 1.5) 弧度）
				float outer_cos = spot.spot_outer_cos;
				outer_cos = outer_cos < -0.999f ? -0.999f : (outer_cos > 0.999f ? 0.999f : outer_cos);
				float half_angle = std::acos(outer_cos);
				half_angle = half_angle < 0.02f ? 0.02f : (half_angle > 1.5f ? 1.5f : half_angle);
				//衰减半径即远平面（钳到 ≥ 0.2）
				float far_plane = spot.radius > 0.2f ? spot.radius : 0.2f;
				glm::mat4 spot_view = glm::lookAt(spot_position, spot_position + spot_direction, spot_up);
				glm::mat4 spot_proj = glm::perspective(2.0f * half_angle, 1.0f, 0.1f, far_plane);
				spot_proj[1][1] *= -1.0f;
				glm::mat4 spot_vp = spot_proj * spot_view;
				std::memcpy(impl->spot_view_proj_tiles[tile], glm::value_ptr(spot_vp), sizeof(float) * 16);
				//该瓦片深度跨度（着色器法线偏移换算用）
				impl->spot_shadow_spans[tile] = far_plane - 0.1f;
				/*
				逐瓦片有效偏置（第五百二十一轮；第六百五十八轮加逐盏缩放）
				自校准开启时按「参考跨度 9 / 本瓦片跨度」缩放，保持世界法线偏移近似恒定；
				关闭时恒为全局值（与引入前逐字节一致）；再乘该槽位的逐盏缩放（默认 1）。
				*/
				float tile_scale = 1.0f;
				if (candidates[tile] >= 0 && candidates[tile] < 8)
					tile_scale = impl->spot_shadow_bias_scale[candidates[tile]];
				impl->spot_shadow_bias_tiles[tile] = (impl->spot_shadow_bias_auto
					? impl->spot_shadow_bias * (Impl::SPOT_BIAS_REFERENCE_SPAN / impl->spot_shadow_spans[tile])
					: impl->spot_shadow_bias) * tile_scale;
				//前向槽位 → 瓦片号（第七百零九轮扩到 32 槽；未登记到映射表的槽位恒为 -1）
				if (candidates[tile] >= 0 && candidates[tile] < 32)
				{
					impl->spot_shadow_slot_tiles[candidates[tile]] = static_cast<float>(tile);
					/*
					逐盏质量缩放（第七百零八轮；仅槽位 0..7 有逐盏配置）
					软度 / 强度按该槽位取值（默认 1 = 跟随全局），供着色器按瓦片读取。
					*/
					if (candidates[tile] < 8)
					{
						impl->spot_shadow_softness_tiles[tile] = impl->spot_shadow_softness_scale[candidates[tile]];
						impl->spot_shadow_strength_tiles[tile] = impl->spot_shadow_strength_scale[candidates[tile]];
					}
				}
				spot_shadow_light_valid = true;
			}
		}
		//本帧参与投影的聚光灯阴影瓦片数（第五百二十轮）
		impl->spot_shadow_tiles = static_cast<uint32_t>(candidate_count);
		/*
		⑤b 阴影 pass（第五十二轮；第五十五轮按需重绘）
		以光源视角把「参与投影的几何」渲染进阴影目标（RGBA8 打包深度）；
		该 pass 走独立的离屏提交（推进帧槽），故必须在取主 pass 帧槽之前完成；
		随后等尽在途帧，保证主 pass 采样时阴影图已写完。
		若本帧输入签名与上一帧完全一致则跳过整个 pass（阴影图原样复用），静态场景省一次离屏提交与等待。
		*/
		//本帧阴影 pass 计数清零（第六百四十七轮；重绘路径内重新累计）
		impl->last_shadow_passes = 0;
		impl->last_spot_shadow_passes = 0;
		//本帧阴影提交次数归零（第六百六十五轮；C2 基线）
		impl->last_shadow_submits = 0;
		if ((impl->shadow_enabled || spot_shadow_light_valid) && impl->shadow_pipeline != 0
			&& impl->shadow_target != 0 && !impl->pending_items.empty())
		{
			//本帧参与投影的项数
			std::size_t shadow_item_count = 0;
			for (const auto& item : impl->pending_items)
			{
				//跳过文字等非投影项
				if (item.cast_shadow)
					++shadow_item_count;
			}
			//本帧阴影输入签名（只混参与投影的实例矩阵；第六百四十四轮）
			uint64_t shadow_signature = shadow_signature_build(impl->light_view_proj,
				impl->light_view_proj_far, impl->light_view_proj_far2, &impl->spot_view_proj_tiles[0][0],
				impl->pending_items, impl->pending_instances,
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
				//方向光阴影关闭（仅聚光阴影）时跳过级联 pass，保留方向光图原样（第四百八十四轮）
				const uint32_t directional_cascade_total = impl->shadow_enabled ? cascade_total : 0u;
				/*
				批量提交规划（第六百六十九轮；C2）
				先统计本帧阴影段数（级联 ＋ 有效聚光瓦片）；开启且段数 > 1 时收集目标 / 矩阵 / 清屏标志，
				循环内只填表不提交，循环后一次批量提交；否则走逐 pass 旧路径。
				*/
				uint32_t spot_tile_total = 0;
				//图集实际生效时瓦片是否可渲染不再取决于独立目标是否存在
				const bool spot_atlas_active = impl->spot_shadow_atlas && impl->spot_shadow_atlas_target != 0;
				if (spot_shadow_light_valid)
				{
					for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
						if (impl->spot_shadow_spans[tile] > 0.0f
							&& (spot_atlas_active || impl->spot_shadow_targets[tile] != 0))
							++spot_tile_total;
				}
				const bool batch_path = impl->shadow_batch_submit
					&& (directional_cascade_total + spot_tile_total) > 1;
				//批量段表（级联上限 3 ＋ 聚光瓦片 8）
				Frame_Render_Target batch_targets[3 + Impl::SPOT_SHADOW_TILES] = {};
				const float* batch_matrices[3 + Impl::SPOT_SHADOW_TILES] = {};
				bool batch_clear[3 + Impl::SPOT_SHADOW_TILES] = {};
				uint32_t batch_count = 0;
				for (uint32_t cascade = 0; cascade < directional_cascade_total; ++cascade)
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
					//第 1 片负责清屏，后续片保留已有内容
					const float* cascade_clear = cascade == 0 ? shadow_clear : nullptr;
					if (batch_path)
					{
						batch_targets[batch_count] = cascade_target;
						batch_matrices[batch_count] = cascade_matrix;
						batch_clear[batch_count] = cascade == 0;
						++batch_count;
					}
					else
					{
						for (auto& shadow_item : shadow_items)
							shadow_item.push_constants = cascade_matrix;
						if (!impl->frame.render_to_target(cascade_target, shadow_items.data(),
							static_cast<uint32_t>(shadow_items.size()), cascade_clear, nullptr, nullptr, true))
						{
							logger.error("渲染器:: 阴影 pass 提交失败（级联 {}）", cascade);
							return false;
						}
						//累计本帧阴影提交次数（第六百六十五轮；现口径一片一次）
						++impl->last_shadow_submits;
					}
				}
				/*
				聚光灯阴影 pass（第四百八十四轮；第五百零七轮扩为多瓦片）
				方向光级联之外，按有效瓦片逐张追加：默认写入独立的 spot_shadow_targets[tile]，单级整幅（无分片）；
				图集开启时改为写入同一张 4×2 图集的对应子视口（第六百七十轮）。
				复用同一阴影管线与实例缓冲，推送常量换成该瓦片的矩阵；首张清屏，其后保留已有内容。
				*/
				//聚光灯阴影 pass 计数（第六百四十七轮）
				uint32_t spot_passes = 0;
				/*
				图集目标（第六百七十轮；C1）
				实际生效时所有瓦片共用一张 4S × 2S 目标，按瓦片号收缩视口到对应小格；
				首瓦片清整张图（颜色 ＋ 深度），其余瓦片保留。
				*/
				Frame_Render_Target atlas_target{};
				if (spot_atlas_active)
				{
					if (!impl->render_target.target_get(impl->spot_shadow_atlas_target, atlas_target.color_image,
						atlas_target.color_view, atlas_target.depth_view, atlas_target.extent))
					{
						logger.error("渲染器:: 聚光阴影图集附件查询失败");
						return false;
					}
					atlas_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
					atlas_target.depth_format = impl->swapchain.depth_format_get();
					atlas_target.sample_count = 1;
				}
				if (spot_shadow_light_valid)
				{
					for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
					{
						//该瓦片无有效聚光灯则跳过
						if (impl->spot_shadow_spans[tile] <= 0.0f)
							continue;
						//图集未生效且该瓦片目标缺失则跳过
						if (!spot_atlas_active && impl->spot_shadow_targets[tile] == 0)
							continue;
						Frame_Render_Target spot_target{};
						if (spot_atlas_active)
						{
							//复用图集目标并收缩到本瓦片小格（渲染区同步收缩，逐片独立清屏）
							spot_target = atlas_target;
							spot_target.viewport_rect.offset = { static_cast<int32_t>((tile % 4) * impl->shadow_size),
								static_cast<int32_t>((tile / 4) * impl->shadow_size) };
							spot_target.viewport_rect.extent = { impl->shadow_size, impl->shadow_size };
							spot_target.render_area_from_viewport = true;
						}
						else if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_target.color_image,
							spot_target.color_view, spot_target.depth_view, spot_target.extent))
						{
							logger.error("渲染器:: 聚光灯阴影目标附件查询失败（瓦片 {}）", tile);
							return false;
						}
						else
						{
							spot_target.color_format = VK_FORMAT_R8G8B8A8_UNORM;
							spot_target.depth_format = impl->swapchain.depth_format_get();
							spot_target.sample_count = 1;
						}
						//本段清屏标志（图集逐瓦片独立清屏；独立图恒清）
						const float* spot_clear = shadow_clear;
						if (batch_path)
						{
							batch_targets[batch_count] = spot_target;
							batch_matrices[batch_count] = impl->spot_view_proj_tiles[tile];
							batch_clear[batch_count] = spot_clear != nullptr;
							++batch_count;
						}
						else
						{
							for (auto& shadow_item : shadow_items)
								shadow_item.push_constants = impl->spot_view_proj_tiles[tile];
							if (!impl->frame.render_to_target(spot_target, shadow_items.data(),
								static_cast<uint32_t>(shadow_items.size()), spot_clear, nullptr, nullptr, true))
							{
								logger.error("渲染器:: 聚光灯阴影 pass 提交失败（瓦片 {}）", tile);
								return false;
							}
							//累计本帧阴影提交次数（第六百六十五轮；现口径一瓦片一次）
							++impl->last_shadow_submits;
						}
						//图集段已逐片清屏（无需首段整图清屏标记）
						++spot_passes;
					}
				}
				/*
				批量提交（第六百六十九轮；C2）
				全部段已填表后一次提交；成功即记为 1 次提交，失败按硬错误返回（调用方回落旧路径由上帧开关决定）。
				*/
				if (batch_path)
				{
					if (!impl->frame.render_to_targets_batch(batch_targets, batch_matrices, batch_count,
						shadow_items.data(), static_cast<uint32_t>(shadow_items.size()), shadow_clear, batch_clear, true))
					{
						logger.error("渲染器:: 阴影批量提交失败（{} 段）", batch_count);
						return false;
					}
					impl->last_shadow_submits = 1;
				}
				//等尽在途帧：确保阴影图写入完成后再由主 pass 采样
				if (!impl->frame.frames_wait())
				{
					logger.error("渲染器:: 阴影 pass 等待完成失败");
					return false;
				}
				//记录本帧阴影 pass 数（方向光级联 ＋ 聚光瓦片；第六百四十七轮）
				impl->last_shadow_passes = directional_cascade_total + spot_passes;
				impl->last_spot_shadow_passes = spot_passes;
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
		//点光源：旧 8 槽沿用原偏移；8..15 槽追加到块尾（位置段起点 = 高段基址）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t base = light < Impl::UBO_LEGACY_SLOTS
				? 32 + static_cast<std::size_t>(light) * 4
				: Impl::SCENE_HIGH_SLOTS_BASE
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) / 8) * Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[base + 0] = impl->point_lights[light].position[0];
			scene_block[base + 1] = impl->point_lights[light].position[1];
			scene_block[base + 2] = impl->point_lights[light].position[2];
			scene_block[base + 3] = impl->point_lights[light].radius;
			//颜色段起点 = 同段位置 32 浮点之后
			scene_block[base + 32 + 0] = impl->point_lights[light].color[0];
			scene_block[base + 32 + 1] = impl->point_lights[light].color[1];
			scene_block[base + 32 + 2] = impl->point_lights[light].color[2];
			scene_block[base + 32 + 3] = impl->point_lights[light].intensity;
		}
		//聚光参数：旧 8 槽前 32 浮点为「方向 ＋ 有效标志」、后 32 浮点为锥参数；8..15 槽追加到块尾
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t base = light < Impl::UBO_LEGACY_SLOTS
				? 96 + static_cast<std::size_t>(light) * 4
				: Impl::SCENE_HIGH_SLOTS_BASE + 64
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) / 8) * Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[base + 0] = impl->point_lights[light].spot_direction[0];
			scene_block[base + 1] = impl->point_lights[light].spot_direction[1];
			scene_block[base + 2] = impl->point_lights[light].spot_direction[2];
			scene_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
			//锥参数段起点 = 同段方向 32 浮点之后
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
		//逐盏光泽度（第五十一轮；旧 8 槽在偏移 184 + light × 4；8..15 槽追加到块尾 588 起）
		for (uint32_t light = 0; light < Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t offset = light < Impl::UBO_LEGACY_SLOTS
				? 184 + static_cast<std::size_t>(light) * 4
				: Impl::SCENE_HIGH_SLOTS_BASE + 128
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) / 8) * Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[offset] = impl->point_lights[light].shininess;
		}
		/*
		光源空间矩阵沿用 ⑤a 结果（第五十二轮；第四百八十四轮起不再被聚光灯覆盖）
		写入 UBO 供场景片元解包比较；阴影关闭时字段保留但不参与计算。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[216 + index] = impl->light_view_proj[index];
		//阴影参数（第五十二轮：x = 启用、y = 偏置、z = 阴影图边长；第五十三轮 w = 软度）
		/*
		阴影总开关（第四百七十四 / 484 轮）
		方向光阴影与聚光阴影各自独立：任一开启即 shadow_params.x = 1；
		cascade_params.x 仅表示方向光阴影是否启用（0 = 方向光阴影关闭，着色器让位）。
		*/
		scene_block[232] = (impl->shadow_enabled || spot_shadow_light_valid) ? 1.0f : 0.0f;
		scene_block[233] = impl->shadow_bias;
		scene_block[234] = static_cast<float>(impl->shadow_size);
		scene_block[235] = impl->shadow_softness;
		//阴影强度（第五十三轮：0 = 阴影无效、1 = 全遮蔽）
		scene_block[236] = impl->shadow_strength;
		//曝光补偿（第六百六十轮；EV，0 = 关闭）
		scene_block[237] = impl->exposure;
		//PBR-lite 参数（第六十四轮：x = 启用、y = 粗糙度缩放；第六十五轮 z = 金属度）
		scene_block[240] = impl->pbr_enabled ? 1.0f : 0.0f;
		scene_block[241] = impl->pbr_roughness;
		scene_block[242] = impl->pbr_metallic;
		/*
		场景缓冲尾部扩展（第六百七十二轮；D1 第一步）
		456 = PBR 掠射高光能量修正开关；457..459 保留（保持 vec4 对齐）。
		*/
		scene_block[456] = impl->pbr_energy ? 1.0f : 0.0f;
		//PBR 粗糙度绝对口径（第七百轮；1 = 材质 / 全局粗糙度直接作为 GGX 粗糙度）
		scene_block[457] = impl->pbr_roughness_absolute ? 1.0f : 0.0f;
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
		/*
		级联参数 x（第四百七十四 / 484 轮）
		仅表示方向光阴影是否启用：方向光关闭时写 0（着色器 shadow_factor 让位）；
		聚光阴影的开关与序号由 spot_shadow_params.w 表达，两图互不依赖。
		*/
		scene_block[260] = impl->shadow_enabled ? static_cast<float>(impl->cascade_count) : 0.0f;
		scene_block[261] = impl->cascade_split_distance;
		scene_block[262] = impl->cascade_depth_span[0];
		scene_block[263] = impl->cascade_depth_span[1];
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[272 + index] = impl->light_view_proj_far2[index];
		scene_block[288] = impl->cascade_split_distance * impl->cascade_far_scale;
		scene_block[289] = impl->cascade_depth_span[2];
		//级联接缝淡化带宽比例（第一百四十八轮；z 槽；第六百六十一轮改用「生效比例」）
		scene_block[290] = impl->cascade_blend_effective;
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
		PCSS 软阴影参数（第六百五十七轮；复用卡通高光参数的空闲分量）
		293 = 开关（> 0.5 生效）、294 = 阻塞物搜索尺度（纹素）、295 = PCF 半径上限（纹素）；
		293 为 0（默认）时片元走旧硬边 / 3×3 软度口径，画面逐字节不变。
		*/
		scene_block[293] = impl->shadow_pcss_enabled ? 1.0f : 0.0f;
		scene_block[294] = impl->shadow_pcss_search;
		scene_block[295] = impl->shadow_pcss_max;
		/*
		簇参数（第二百八十五轮写入；第二百八十九轮由场景片元读取）
		296 = Cluster 启用（> 0.5 时片元改读逐簇光源列表）、297..299 = 网格 x / y / z；
		296 为 0（默认）时着色器走原 8 盏前向循环，画面逐字节不变。
		*/
		scene_block[296] = impl->cluster_enabled ? 1.0f : 0.0f;
		scene_block[297] = static_cast<float>(impl->cluster_grid.x);
		scene_block[298] = static_cast<float>(impl->cluster_grid.y);
		scene_block[299] = static_cast<float>(impl->cluster_grid.z);
		/*
		聚光灯阴影参数（第四百七十六 / 479 轮；x = 图集开关（第六百七十轮）、y = 软度、z = 强度、w = 开关）
		x > 0.5 时着色器按瓦片号换算图集像素原点；w > 0.5 表示本帧存在有效聚光阴影。
		*/
		scene_block[300] = impl->spot_shadow_atlas ? 1.0f : 0.0f;
		scene_block[301] = impl->spot_shadow_softness;
		scene_block[302] = impl->spot_shadow_strength;
		//w = 开关（第五百零七轮起不再编码序号，序号改由槽位映射表达）
		scene_block[303] = spot_shadow_light_valid ? 1.0f : 0.0f;
		/*
		聚光灯阴影矩阵 / 跨度 / 槽位映射（第五百零七轮；第五百一十三轮扩为 8 瓦片；第五百二十一轮追加逐瓦片偏置）
		304..431 = 8 份透视矩阵；432..439 = 8 份深度跨度；440..447 = 8 个前向槽位的瓦片号（-1 = 不投影）；448..455 = 8 份有效偏置。
		*/
		for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
			for (std::size_t index = 0; index < 16; ++index)
				scene_block[304 + tile * 16 + index] = impl->spot_view_proj_tiles[tile][index];
		for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
			scene_block[432 + tile] = impl->spot_shadow_spans[tile];
		for (std::size_t slot = 0; slot < 8; ++slot)
			scene_block[440 + slot] = impl->spot_shadow_slot_tiles[slot];
		//槽位 8..31 的瓦片映射（第七百零九轮；956..979）
		for (std::size_t slot = 0; slot < 24; ++slot)
			scene_block[Impl::SCENE_SPOT_SLOTS_HI_BASE + slot] = impl->spot_shadow_slot_tiles[8 + slot];
		//逐瓦片有效偏置（第五百二十一轮；448..455）
		for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
			scene_block[448 + tile] = impl->spot_shadow_bias_tiles[tile];
		/*
		逐瓦片质量缩放（第七百零八轮）
		940..947 = 软度缩放、948..955 = 强度缩放；默认 1（与全局参数相同，画面逐字节不变）。
		*/
		for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
		{
			scene_block[Impl::SCENE_SPOT_SOFTNESS_BASE + tile] = impl->spot_shadow_softness_tiles[tile];
			scene_block[Impl::SCENE_SPOT_STRENGTH_BASE + tile] = impl->spot_shadow_strength_tiles[tile];
		}
		/*
		场景 UBO 按需写入（第六十轮）
		逐帧槽缓存上一次写入的全部浮点（300），按 16 字节（4 浮点）单元比较；
		连续变化单元合并为一段，仅对变化区间调用 buffer_write；
		首次访问某帧槽（缓存未建立）仍整块写入 1200 字节，保证内容完整。
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
			//聚光灯阴影图视图（第四百八十三轮；第五百零七轮扩为 4 张；未建立时以默认白纹理占位）
			VkImageView spot_shadow_views[Impl::SPOT_SHADOW_TILES] = { default_view, default_view,
				default_view, default_view, default_view, default_view, default_view, default_view };
			/*
			图集（第六百七十轮；C1）
			实际生效时 8 个数组元素全部写入同一张图集视图，瓦片定位由图集内像素原点完成；
			否则沿用逐瓦片视图。
			*/
			if (impl->spot_shadow_atlas && impl->spot_shadow_atlas_target != 0)
			{
				VkImage atlas_image = VK_NULL_HANDLE;
				VkImageView atlas_view = VK_NULL_HANDLE;
				VkImageView atlas_depth_view = VK_NULL_HANDLE;
				VkExtent2D atlas_extent{};
				if (impl->render_target.target_get(impl->spot_shadow_atlas_target, atlas_image, atlas_view,
					atlas_depth_view, atlas_extent))
					for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
						spot_shadow_views[tile] = atlas_view;
			}
			else
			{
				for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
				{
					//该瓦片未建立则保持默认白纹理
					if (impl->spot_shadow_targets[tile] == 0)
						continue;
					VkImage spot_shadow_image = VK_NULL_HANDLE;
					VkImageView spot_shadow_depth_view = VK_NULL_HANDLE;
					VkExtent2D spot_shadow_extent{};
					impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_shadow_image,
						spot_shadow_views[tile], spot_shadow_depth_view, spot_shadow_extent);
				}
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
				//本帧槽的实例缓冲（模型矩阵来源；不消费实例属性的项留空，第三百四十一轮）
				item.instance_buffer = item.instance_input ? impl->instance_buffers[slot] : VK_NULL_HANDLE;
				//本帧槽的场景集（set0）：粒子专用项取粒子集，其余取场景集
				item.scene_set = item.particle_scene_set
					? impl->pipeline.particle_set_get(slot) : impl->pipeline.scene_set_get(slot);
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
					//聚光灯阴影图（第四百八十三轮；binding 9；第五百一十三轮写入全部 8 个数组元素）
					for (std::size_t tile = 0; tile < Impl::SPOT_SHADOW_TILES; ++tile)
						views.spot_shadow[tile] = spot_shadow_views[tile];
					//逐项场景深度（粒子软粒子：HiZ 历史视图；未提供时不写，回落反照率占位）
					views.depth = item.draw_depth;
					views.depth_sampler = item.draw_depth_sampler;
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
