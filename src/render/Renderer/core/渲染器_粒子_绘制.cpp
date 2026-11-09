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
	/*
	粒子绘制门面（第三百一十轮；默认关闭）
	模拟语义（发射 / 力场 / 碰撞 / 生命周期）已迁出渲染层：状态数组由应用层绑定，
	本层只做 GPU 上传与 billboard 绘制登记。关闭时不登记任何绘制项。
	*/
	//粒子路径是否可用（第三百一十一轮：改由计算路径可用性决定）

	bool Renderer_Particle::particle_blend_set(Blend_Mode mode)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled)
			return false;
		//仅接受两个半透明档
		if (mode != Blend_Mode::Alpha && mode != Blend_Mode::Additive)
			return false;
		//同值重复设置为空操作
		if (impl->particle_blend_mode == mode)
			return true;
		impl->particle_blend_mode = mode;
		//释放旧管线，下次绘制按新档重建（含 GPU 绘制管线；第三百四十一轮）
		if (impl->particle_draw_pipeline != 0)
		{
			self().pipeline_unload(impl->particle_draw_pipeline);
			impl->particle_draw_pipeline = 0;
		}
		if (impl->particle_gpu_pipeline != 0)
		{
			self().pipeline_unload(impl->particle_gpu_pipeline);
			impl->particle_gpu_pipeline = 0;
		}
		return true;
	}

	Blend_Mode Renderer_Particle::particle_blend_get(void) const
	{
		if (impl == nullptr || !impl->initialized)
			return Blend_Mode::Alpha;
		return impl->particle_blend_mode;
	}

	bool Renderer_Particle::particle_depth_write_set(bool enabled)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled)
			return false;
		//同值重复设置为空操作
		if (impl->particle_depth_write == enabled)
			return true;
		impl->particle_depth_write = enabled;
		//释放旧管线，下次绘制按新设置重建（含 GPU 绘制管线；第三百四十一轮）
		if (impl->particle_draw_pipeline != 0)
		{
			self().pipeline_unload(impl->particle_draw_pipeline);
			impl->particle_draw_pipeline = 0;
		}
		if (impl->particle_gpu_pipeline != 0)
		{
			self().pipeline_unload(impl->particle_gpu_pipeline);
			impl->particle_gpu_pipeline = 0;
		}
		return true;
	}

	bool Renderer_Particle::particle_depth_write_get(void) const
	{
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->particle_depth_write;
	}

	bool Renderer_Particle::particle_sort_buckets_set(uint32_t buckets)
	{
		//未初始化
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled)
			return false;
		//钳制到 [0, 16]（0 = 关闭）
		impl->particle_sort_buckets = buckets > 16u ? 16u : buckets;
		return true;
	}

	uint32_t Renderer_Particle::particle_sort_buckets_get(void) const
	{
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->particle_sort_buckets;
	}

	bool Renderer_Compute::particle_depth_source(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const
	{
		//默认无有效句柄
		view_out = 0;
		sampler_out = 0;
		//档位关闭 / HiZ 未启用 / 资源未建 / 该槽无历史
		if (impl == nullptr || impl->particle_params.soft_depth_range <= 0.0f
			|| !impl->hiz_enabled || !impl->hiz.valid() || !impl->hiz.history_valid(slot))
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

	void Renderer_Compute::particle_style_push_fill(std::array<float, 32>& push) const
	{
		const Particle_Params& params = impl->particle_params;
		//颜色覆盖开关（0 = 片元着色器回落内置曲线）
		push[24] = params.color_enabled ? 1.0f : 0.0f;
		//软边宽度（0 = 片元着色器回落 0.65 内半径）
		push[25] = params.edge_softness;
		for (uint32_t channel = 0; channel < 3; ++channel)
		{
			//新生端颜色
			push[16 + channel] = params.color_start[channel];
			//衰老端颜色
			push[20 + channel] = params.color_end[channel];
		}
	}

	bool Renderer_Particle::particle_draw(float size)
	{
		//未开启粒子或未初始化
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled)
			return false;
		//尺寸非法
		if (!std::isfinite(size) || size <= 0.0f || size > 1e3f)
			return false;
		//未绑定数据源
		if (impl->particle_states == nullptr || impl->particle_alive == nullptr
			|| impl->particle_source_capacity == 0)
			return false;
		uint32_t capacity = impl->particle_source_capacity;
		const Particle_State* states = impl->particle_states;
		const uint8_t* alive = impl->particle_alive;
		/*
		常驻模式绘制（第三百三十轮）
		回读活跃位图与状态缓冲后按位图展开活跃标记；容量上限 65536（百万级整缓冲回读不可接受，
		大容量绘制留待间接绘制路径）。回读失败时不登记绘制项。
		临时数组取自 Impl 复用缓冲（第三百六十四轮；每帧 clear 后重填，语义与局部 vector 一致）。
		*/
		std::vector<uint8_t>& resident_bitmap = impl->particle_draw_resident_bitmap;
		std::vector<Particle_State>& resident_states = impl->particle_draw_resident_states;
		std::vector<uint8_t>& resident_alive = impl->particle_draw_resident_alive;
		resident_bitmap.clear();
		if (impl->particle_resident)
		{
			//大容量 / 资源未就绪
			if (capacity > 65536u || !self().particle_gpu_available())
				return false;
			uint32_t words = (capacity + 31u) / 32u;
			resident_bitmap.resize(static_cast<std::size_t>(words) * 4u, 0u);
			resident_states.resize(capacity);
			//位图位于控制缓冲偏移 8
			if (!self().compute_buffer_read(impl->particle_control_buffer, resident_bitmap.data(), words * 4u, 8u))
				return false;
			uint32_t bytes = capacity * static_cast<uint32_t>(PARTICLE_STRIDE);
			if (!self().compute_buffer_read(impl->particle_buffer_front, resident_states.data(), bytes))
				return false;
			//按位展开活跃标记
			resident_alive.assign(capacity, 0u);
			for (uint32_t index = 0; index < capacity; ++index)
			{
				uint32_t word = 0;
				std::memcpy(&word, resident_bitmap.data() + static_cast<std::size_t>(index / 32u) * 4u, 4u);
				if ((word & (1u << (index % 32u))) != 0u)
					resident_alive[index] = 1u;
			}
			states = resident_states.data();
			alive = resident_alive.data();
		}
		//统计活跃粒子
		uint32_t alive_count = 0;
		for (uint32_t index = 0; index < capacity; ++index)
			if (alive[index] != 0u)
				++alive_count;
		//无活跃粒子不登记绘制项
		if (alive_count == 0)
			return false;
		//绘制资源
		if (!self().particle_draw_ensure())
			return false;
		//相机世界位置（billboard 朝向基准）
		float camera[3] = {};
		impl->camera.position_get(camera);
		//单粒子模型矩阵构造（列主序 16 浮点；billboard 基 ＋ 年龄收缩尺寸）
		auto build_matrix = [&](const Particle_State& state, float* matrix)
		{
			//归一化年龄（0 = 刚发射、1 = 寿命尽头；寿命非正按 0）
			float age_ratio = state.lifetime > 0.0f ? state.age / state.lifetime : 0.0f;
			age_ratio = age_ratio < 0.0f ? 0.0f : (age_ratio > 1.0f ? 1.0f : age_ratio);
			//尺寸随年龄收缩（最多 35%；第三百一十五轮）
			float half = size * 0.5f * (1.0f - 0.35f * age_ratio);
			//视线方向（相机 → 粒子）
			float forward[3] = { state.position[0] - camera[0], state.position[1] - camera[1],
				state.position[2] - camera[2] };
			float forward_length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]
				+ forward[2] * forward[2]);
			//退化（粒子与相机重合）时回落到 -Z
			if (!(forward_length > 1e-6f))
			{
				forward[0] = 0.0f;
				forward[1] = 0.0f;
				forward[2] = -1.0f;
				forward_length = 1.0f;
			}
			for (uint32_t axis = 0; axis < 3; ++axis)
				forward[axis] /= forward_length;
			//右向量 = forward × 世界上方向（0,1,0）→ (-z, 0, x)
			float right[3] = { -forward[2], 0.0f, forward[0] };
			float right_length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
			//视线与世界上方向平行时回落到 +X
			if (!(right_length > 1e-6f))
			{
				right[0] = 1.0f;
				right[1] = 0.0f;
				right[2] = 0.0f;
				right_length = 1.0f;
			}
			for (uint32_t axis = 0; axis < 3; ++axis)
				right[axis] /= right_length;
			//上向量 = right × forward
			float up[3] = {
				right[1] * forward[2] - right[2] * forward[1],
				right[2] * forward[0] - right[0] * forward[2],
				right[0] * forward[1] - right[1] * forward[0] };
			//基向量 × 半尺寸（quad 顶点为 ±0.5）
			matrix[0] = right[0] * half;
			matrix[1] = right[1] * half;
			matrix[2] = right[2] * half;
			matrix[4] = up[0] * half;
			matrix[5] = up[1] * half;
			matrix[6] = up[2] * half;
			//局部 Z 轴（四边形为平面；w 分量承载归一化年龄，第三百一十五轮）
			matrix[11] = age_ratio;
			//中心平移
			matrix[12] = state.position[0];
			matrix[13] = state.position[1];
			matrix[14] = state.position[2];
			matrix[15] = 1.0f;
		};
		/*
		深度分桶近似排序（第三百一十七轮；0 = 关闭）
		关闭时按池索引顺序单次实例化；开启时按视线距离分桶、桶间由远及近各登记一次实例化绘制。
		*/
		uint32_t buckets = impl->particle_sort_buckets;
		//视线深度临时数组（第三百六十四轮；仅分桶需要时填充）
		std::vector<float>& depths = impl->particle_draw_depths;
		float min_depth = 0.0f;
		float max_depth = 0.0f;
		if (buckets > 0)
		{
			depths.assign(static_cast<std::size_t>(capacity), 0.0f);
			//逐活跃粒子记录视线距离并求范围
			bool first = true;
			for (uint32_t index = 0; index < capacity; ++index)
			{
				//跳过非活跃索引
				if (alive[index] == 0u)
					continue;
				const Particle_State& state = states[index];
				float delta[3] = { state.position[0] - camera[0], state.position[1] - camera[1],
					state.position[2] - camera[2] };
				float depth = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
				depths[index] = depth;
				if (first)
				{
					min_depth = depth;
					max_depth = depth;
					first = false;
				}
				else
				{
					if (depth < min_depth)
						min_depth = depth;
					if (depth > max_depth)
						max_depth = depth;
				}
			}
		}
		//切换粒子管线登记实例化绘制
		uint64_t previous_pipeline = impl->bound_pipeline_request;
		self().pipeline_bind_set(impl->particle_draw_pipeline);
		/*
		软粒子深度源（第三百六十六轮）
		本帧槽 HiZ 历史（约两帧前的场景深度）；未启用 / 未就绪时档位清零，画面回落旧行为。
		*/
		uint64_t depth_view_handle = 0;
		uint64_t depth_sampler_handle = 0;
		const bool depth_bound = self().particle_depth_source(impl->frame.slot_index_get(),
			depth_view_handle, depth_sampler_handle);
		VkImageView depth_view = depth_bound ? reinterpret_cast<VkImageView>(depth_view_handle) : VK_NULL_HANDLE;
		VkSampler depth_sampler = depth_bound ? reinterpret_cast<VkSampler>(depth_sampler_handle) : VK_NULL_HANDLE;
		//本次绘制推送的深度档位（未绑定深度时为 0）
		const float soft_range = depth_bound ? impl->particle_params.soft_depth_range : 0.0f;
		bool drawn = false;
		//分桶关闭：单次实例化（池索引顺序）
		if (buckets == 0)
		{
			std::vector<float>& transforms = impl->particle_draw_transforms;
			transforms.assign(static_cast<std::size_t>(alive_count) * 16, 0.0f);
			uint32_t written = 0;
			for (uint32_t index = 0; index < capacity; ++index)
			{
				//跳过非活跃索引
				if (alive[index] == 0u)
					continue;
				build_matrix(states[index], transforms.data() + static_cast<std::size_t>(written) * 16);
				++written;
			}
			//登记前的计数（用于判定本批是否真的产生了绘制项）
			const std::size_t items_before = impl->pending_items.size();
			const std::size_t push_before = impl->pending_push_constants.size();
			drawn = self().mesh_draw_instanced(impl->particle_quad_mesh, transforms.data(), written);
			/*
			刚登记项为粒子实例化项
			关闭阴影视图投影，并写入颜色覆盖推送（第三百五十四轮）；
			整批被剔除时不会产生新绘制项，此时不改写任何块。
			*/
			if (drawn && impl->pending_items.size() > items_before
				&& impl->pending_push_constants.size() > push_before)
			{
				impl->pending_items.back().cast_shadow = false;
				self().particle_style_push_fill(impl->pending_push_constants.back());
				//软粒子：挂历史深度视图并写深度档位（未绑定时为 0）
				impl->pending_items.back().draw_depth = depth_view;
				impl->pending_items.back().draw_depth_sampler = depth_sampler;
				impl->pending_push_constants.back()[28] = soft_range;
				/*
				专属采样集（第三百六十六轮）
				粒子片元静态声明 set1 binding 2，共享采样器集未写该绑定，故必须落一条专属集；
				调用方若未绑定纹理，则以默认白纹理补齐（binding 0/2 均不被粒子片元采样）。
				*/
				if (impl->pending_items.back().draw_albedo == VK_NULL_HANDLE
					|| impl->pending_items.back().draw_sampler == VK_NULL_HANDLE)
				{
					VkImageView default_view = VK_NULL_HANDLE;
					VkSampler default_sampler = VK_NULL_HANDLE;
					impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
					impl->pending_items.back().draw_albedo = default_view;
					impl->pending_items.back().draw_sampler = default_sampler;
				}
			}
		}
		else
		{
			float span = max_depth - min_depth;
			//桶间由远及近（桶索引自大到小）
			for (uint32_t bucket = buckets; bucket-- > 0;)
			{
				//统计本桶粒子数
				uint32_t bucket_count = 0;
				for (uint32_t index = 0; index < capacity; ++index)
				{
					//跳过非活跃索引
					if (alive[index] == 0u)
						continue;
					float ratio = span > 1e-6f ? (depths[index] - min_depth) / span : 0.0f;
					uint32_t target = static_cast<uint32_t>(ratio * static_cast<float>(buckets));
					if (target >= buckets)
						target = buckets - 1;
					if (target == bucket)
						++bucket_count;
				}
				//空桶跳过
				if (bucket_count == 0)
					continue;
				std::vector<float>& transforms = impl->particle_draw_transforms;
				transforms.assign(static_cast<std::size_t>(bucket_count) * 16, 0.0f);
				uint32_t written = 0;
				for (uint32_t index = 0; index < capacity; ++index)
				{
					//跳过非活跃索引
					if (alive[index] == 0u)
						continue;
					float ratio = span > 1e-6f ? (depths[index] - min_depth) / span : 0.0f;
					uint32_t target = static_cast<uint32_t>(ratio * static_cast<float>(buckets));
					if (target >= buckets)
						target = buckets - 1;
					if (target != bucket)
						continue;
					build_matrix(states[index], transforms.data() + static_cast<std::size_t>(written) * 16);
					++written;
				}
				//登记前的计数（用于判定本桶是否真的产生了绘制项）
				const std::size_t items_before = impl->pending_items.size();
				const std::size_t push_before = impl->pending_push_constants.size();
				bool registered = self().mesh_draw_instanced(impl->particle_quad_mesh, transforms.data(), written);
				/*
				刚登记项为粒子实例化项
				关闭阴影视图投影，并写入颜色覆盖推送（第三百五十四轮；每桶一次）。
				*/
				if (registered && impl->pending_items.size() > items_before
					&& impl->pending_push_constants.size() > push_before)
				{
					impl->pending_items.back().cast_shadow = false;
					self().particle_style_push_fill(impl->pending_push_constants.back());
					//软粒子：每桶同样挂历史深度并写档位
					impl->pending_items.back().draw_depth = depth_view;
					impl->pending_items.back().draw_depth_sampler = depth_sampler;
					impl->pending_push_constants.back()[28] = soft_range;
					//专属采样集（同单次分支：共享集未写 binding 2）
					if (impl->pending_items.back().draw_albedo == VK_NULL_HANDLE
						|| impl->pending_items.back().draw_sampler == VK_NULL_HANDLE)
					{
						VkImageView default_view = VK_NULL_HANDLE;
						VkSampler default_sampler = VK_NULL_HANDLE;
						impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
						impl->pending_items.back().draw_albedo = default_view;
						impl->pending_items.back().draw_sampler = default_sampler;
					}
				}
				drawn = drawn || registered;
			}
		}
		//恢复调用前的管线选择
		self().pipeline_bind_set(previous_pipeline);
		//登记路径观测（第三百四十八轮；1 = CPU 参考）
		if (drawn)
			impl->particle_draw_path_last = 1;
		return drawn;
	}

	bool Renderer_Particle::particle_gpu_draw(float size)
	{
		//前置条件
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled || !impl->particle_resident)
			return false;
		//尺寸非法
		if (!std::isfinite(size) || size <= 0.0f || size > 1e3f)
			return false;
		if (!self().particle_gpu_available())
			return false;
		//绘制资源与管线
		if (!self().particle_gpu_draw_ensure())
			return false;
		//每帧前置：紧凑索引
		if (!self().particle_gpu_compact())
			return false;
		uint32_t count = impl->particle_compact_count;
		//无活跃粒子不登记绘制项
		if (count == 0)
			return false;
		//状态与索引的原生缓冲
		VkBuffer state_buffer = impl->compute_buffer_native(impl->particle_buffer_front);
		VkBuffer index_buffer = impl->compute_buffer_native(impl->particle_index_buffer);
		if (state_buffer == VK_NULL_HANDLE || index_buffer == VK_NULL_HANDLE)
			return false;
		//逐帧槽刷新粒子专用集（front 在常驻推进后会交换）
		for (uint32_t slot = 0; slot < impl->scene_ubos.size(); ++slot)
			if (!impl->pipeline.particle_buffers_bind(slot, impl->scene_ubos[slot], state_buffer, index_buffer))
				return false;
		//四边形网格记录（顶点与索引共用一个缓冲）
		VkBuffer quad_buffer = VK_NULL_HANDLE;
		VkDeviceSize quad_index_offset = 0;
		uint32_t quad_index_count = 0;
		if (!impl->mesh.mesh_record_get(impl->particle_quad_mesh, quad_buffer, quad_index_offset, quad_index_count))
			return false;
		//相机世界位置（着色器内 billboard 基准）
		float camera[3] = {};
		impl->camera.position_get(camera);
		/*
		推送块（128 B）
		首位 = 相机位置 ＋ 四边形世界边长；64 / 80 / 96 为颜色覆盖（第三百五十四轮），其余留零。
		*/
		impl->pending_push_constants.emplace_back();
		std::array<float, 32>& push = impl->pending_push_constants.back();
		push.fill(0.0f);
		push[0] = camera[0];
		push[1] = camera[1];
		push[2] = camera[2];
		push[3] = size;
		//颜色覆盖推送（第三百五十四轮；与 CPU 路径同一推送块口径）
		self().particle_style_push_fill(push);
		/*
		软粒子（第三百六十六轮）
		绑定本帧槽历史深度＋最近邻采样器；GPU 路径本无逐项纹理快照，故以默认白纹理占位补齐采样集条件。
		*/
		uint64_t depth_view_handle = 0;
		uint64_t depth_sampler_handle = 0;
		const bool depth_bound = self().particle_depth_source(impl->frame.slot_index_get(),
			depth_view_handle, depth_sampler_handle);
		VkImageView depth_view = depth_bound ? reinterpret_cast<VkImageView>(depth_view_handle) : VK_NULL_HANDLE;
		VkSampler depth_sampler = depth_bound ? reinterpret_cast<VkSampler>(depth_sampler_handle) : VK_NULL_HANDLE;
		//未绑定深度时档位为 0（画面回落旧行为）
		push[28] = depth_bound ? impl->particle_params.soft_depth_range : 0.0f;
		//组装绘制项（管线延迟到 frame_render 才绑定；此处只登记状态）
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_by_handle_get(impl->particle_gpu_pipeline);
		item.layout = impl->pipeline.layout_get();
		item.scene_set = VK_NULL_HANDLE;
		item.sampler_set = VK_NULL_HANDLE;
		item.descriptor_set_count = 2;
		/*
		专属采样集（第三百六十六轮）
		粒子片元静态声明 set1 binding 2，共享采样器集未写该绑定；以默认白纹理补齐快照，
		历史深度仅在可用时挂上（未挂时 binding 2 回落反照率占位，档位为 0 不采样）。
		*/
		{
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
			item.draw_albedo = default_view;
			item.draw_sampler = default_sampler;
			item.draw_depth = depth_view;
			item.draw_depth_sampler = depth_sampler;
		}
		item.vertex_buffer = quad_buffer;
		item.index_buffer = quad_buffer;
		item.index_offset = quad_index_offset;
		item.index_count = quad_index_count;
		//不消费实例属性（第三百四十一轮）
		item.instance_input = false;
		item.particle_scene_set = true;
		item.instance_buffer = VK_NULL_HANDLE;
		item.instance_offset = 0;
		item.instance_count = count;
		item.push_constants = push.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		//粒子不参与阴影视图投影
		item.cast_shadow = false;
		//透明标记（非不透明混合项不参与合批）
		item.transparent = impl->particle_blend_mode != Blend_Mode::Opaque;
		//登记待绘制项
		impl->pending_items.push_back(item);
		//登记路径观测（第三百四十八轮；2 = GPU 直接绘制）
		impl->particle_draw_path_last = 2;
		return true;
	}

	bool Renderer_Particle::particle_indirect_draw(float size)
	{
		//前置条件
		if (impl == nullptr || !impl->initialized || !impl->particle_enabled || !impl->particle_resident)
			return false;
		//尺寸非法
		if (!std::isfinite(size) || size <= 0.0f || size > 1e3f)
			return false;
		if (!self().particle_gpu_available())
			return false;
		//绘制资源与管线（单位四边形 ＋ 粒子GPU.vert / 粒子.frag）
		if (!self().particle_gpu_draw_ensure())
			return false;
		//本帧槽与对应命令缓冲
		uint32_t slot = impl->frame.slot_index_get();
		if (slot >= impl->particle_indirect_buffers.size())
			return false;
		//等待该槽上一轮帧完成（其间接命令不再被读取）
		if (!impl->frame.slot_wait())
			return false;
		//四边形网格记录（索引数写入命令）
		VkBuffer quad_buffer = VK_NULL_HANDLE;
		VkDeviceSize quad_index_offset = 0;
		uint32_t quad_index_count = 0;
		if (!impl->mesh.mesh_record_get(impl->particle_quad_mesh, quad_buffer, quad_index_offset, quad_index_count))
			return false;
		//命令缓冲原生句柄
		VkBuffer command_buffer = impl->compute_buffer_native(impl->particle_indirect_buffers[slot]);
		if (command_buffer == VK_NULL_HANDLE)
			return false;
		/*
		紧凑索引 ＋ 写命令合并为一次提交（第三百四十九轮；优化）
		两次调度在同一一次性命令缓冲内完成（计算→计算屏障分隔），只等一次围栏：
		此前为「紧凑一次提交 ＋ 写命令一次提交」，每帧多一次提交与围栏等待（约 20–40 µs）。
		*/
		const auto compact_begin = std::chrono::high_resolution_clock::now();
		uint32_t capacity = impl->particle_buffer_capacity;
		//清零紧凑计数
		uint32_t zero = 0;
		if (!self().compute_buffer_write(impl->particle_compact_count_buffer, &zero, static_cast<uint32_t>(sizeof(zero))))
			return false;
		/*
		两次调度用**不同的计算描述符集槽**（第三百四十九轮）
		描述符集内容在提交时读取、而非录制时快照，故同一次提交内切换绑定必须换集：
		槽 0 = 紧凑（控制 / 索引 / 计数），槽 1 = 写命令（计数 / 命令缓冲）；
		两个集的绑定都在提交前写好，录制时只切槽。
		*/
		const uint32_t previous_slot = impl->compute.slot_get();
		if (!impl->compute.slot_set(0))
			return false;
		if (!self().compute_buffer_bind(impl->particle_control_buffer, 0)
			|| !self().compute_buffer_bind(impl->particle_index_buffer, 1)
			|| !self().compute_buffer_bind(impl->particle_compact_count_buffer, 2))
			return false;
		//槽 1：紧凑计数（只读）/ 该槽命令缓冲（只写）
		VkBuffer count_buffer = impl->compute_buffer_native(impl->particle_compact_count_buffer);
		if (!impl->compute.storage_bind(1, 0, count_buffer, 0, static_cast<VkDeviceSize>(sizeof(uint32_t)))
			|| !impl->compute.storage_bind(1, 1, command_buffer, 0,
				static_cast<VkDeviceSize>(Renderer_Impl_Holder::Impl::INDIRECT_COMMAND_BYTES)))
			return false;
		//两次调度各自的计算上下文句柄
		uint64_t compact_context = 0;
		uint64_t indirect_context = 0;
		for (const Renderer_Impl_Holder::Impl::Compute_Pipeline_Record& record : impl->compute_pipelines)
		{
			if (record.handle == impl->particle_compact_pipeline)
				compact_context = record.context_handle;
			//写命令管线句柄
			else if (record.handle == impl->particle_indirect_pipeline)
				indirect_context = record.context_handle;
		}
		if (compact_context == 0 || indirect_context == 0)
			return false;
		const uint32_t compact_push[4] = { capacity, (capacity + 31u) / 32u, 0u, 0u };
		const uint32_t indirect_push[4] = { quad_index_count, 0u, 0u, 0u };
		const bool dispatched = impl->compute.single_submit([&](VkCommandBuffer command) -> bool
		{
			if (!impl->compute.timestamp_begin(command))
				return false;
			//切到槽 0 并录制第一次调度：活跃位图 → 紧凑索引 ＋ 计数
			if (!impl->compute.slot_set(0))
				return false;
			if (!impl->compute.dispatch_ceil(command, compact_context, capacity, 64u,
				compact_push, static_cast<uint32_t>(sizeof(compact_push))))
				return false;
			if (!impl->compute.barrier(command, Compute_Barrier::ComputeToCompute))
				return false;
			//切到槽 1 并录制第二次调度：紧凑计数 → 间接命令（单线程）
			if (!impl->compute.slot_set(1))
				return false;
			if (!impl->compute.dispatch_ceil(command, indirect_context, 1u, 1u,
				indirect_push, static_cast<uint32_t>(sizeof(indirect_push))))
				return false;
			return impl->compute.barrier(command, Compute_Barrier::ComputeToHost);
		});
		//恢复调用前的计算帧槽
		impl->compute.slot_set(previous_slot);
		//围栏等待之后读回计时（不支持时间戳时为空操作）
		if (dispatched)
			impl->compute.timestamp_refresh();
		//回读索引数（防御越界）
		uint32_t count = 0;
		if (!dispatched || !self().compute_buffer_read(impl->particle_compact_count_buffer, &count,
			static_cast<uint32_t>(sizeof(count))))
			return false;
		if (count > capacity)
			count = capacity;
		impl->particle_compact_count = count;
		const auto compact_end = std::chrono::high_resolution_clock::now();
		impl->particle_compact_ms_last = std::chrono::duration<double, std::milli>(compact_end - compact_begin).count();
		//无活跃粒子不登记绘制项
		if (count == 0)
			return false;
		impl->particle_indirect_last_slot = slot;
		impl->particle_indirect_written = true;
		//状态与索引的原生缓冲（逐帧槽刷新粒子专用集：front 在常驻推进后会交换）
		VkBuffer state_buffer = impl->compute_buffer_native(impl->particle_buffer_front);
		VkBuffer index_buffer = impl->compute_buffer_native(impl->particle_index_buffer);
		if (state_buffer == VK_NULL_HANDLE || index_buffer == VK_NULL_HANDLE)
			return false;
		for (uint32_t frame_slot = 0; frame_slot < impl->scene_ubos.size(); ++frame_slot)
			if (!impl->pipeline.particle_buffers_bind(frame_slot, impl->scene_ubos[frame_slot],
				state_buffer, index_buffer))
				return false;
		//相机世界位置（着色器内 billboard 基准）
		float camera[3] = {};
		impl->camera.position_get(camera);
		//推送块（128 B；首位 = 相机位置 ＋ 四边形世界边长，64 / 80 / 96 为颜色覆盖，其余留零）
		impl->pending_push_constants.emplace_back();
		std::array<float, 32>& push_spatial = impl->pending_push_constants.back();
		push_spatial.fill(0.0f);
		push_spatial[0] = camera[0];
		push_spatial[1] = camera[1];
		push_spatial[2] = camera[2];
		push_spatial[3] = size;
		//颜色覆盖推送（第三百五十四轮；与 CPU / GPU 直接路径同一推送块口径）
		self().particle_style_push_fill(push_spatial);
		/*
		软粒子（第三百六十六轮；与 GPU 直接路径同口径）
		绑定本帧槽历史深度＋最近邻采样器，并以默认白纹理快照补齐专属采样集条件。
		*/
		uint64_t depth_view_handle = 0;
		uint64_t depth_sampler_handle = 0;
		const bool depth_bound = self().particle_depth_source(impl->frame.slot_index_get(),
			depth_view_handle, depth_sampler_handle);
		VkImageView depth_view = depth_bound ? reinterpret_cast<VkImageView>(depth_view_handle) : VK_NULL_HANDLE;
		VkSampler depth_sampler = depth_bound ? reinterpret_cast<VkSampler>(depth_sampler_handle) : VK_NULL_HANDLE;
		//未绑定深度时档位为 0（画面回落旧行为）
		push_spatial[28] = depth_bound ? impl->particle_params.soft_depth_range : 0.0f;
		//组装绘制项（实例数取自命令，instance_count 仅用于统计口径）
		Frame_Render_Item item{};
		item.pipeline = impl->pipeline.pipeline_by_handle_get(impl->particle_gpu_pipeline);
		item.layout = impl->pipeline.layout_get();
		item.descriptor_set_count = 2;
		/*
		专属采样集（第三百六十六轮；与 GPU 直接路径同口径）
		粒子片元静态声明 set1 binding 2，以默认白纹理补齐快照，历史深度仅在可用时挂上。
		*/
		{
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler default_sampler = VK_NULL_HANDLE;
			impl->texture.texture_bind_get(impl->default_texture, default_view, default_sampler);
			item.draw_albedo = default_view;
			item.draw_sampler = default_sampler;
			item.draw_depth = depth_view;
			item.draw_depth_sampler = depth_sampler;
		}
		item.vertex_buffer = quad_buffer;
		item.index_buffer = quad_buffer;
		item.index_offset = quad_index_offset;
		item.index_count = quad_index_count;
		item.instance_input = false;
		item.particle_scene_set = true;
		item.gpu_indirect = true;
		item.indirect_command_buffer = command_buffer;
		item.indirect_command_offset = 0;
		item.instance_count = count;
		item.push_constants = push_spatial.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push_spatial.size());
		//粒子不参与阴影视图投影
		item.cast_shadow = false;
		//透明标记（非不透明混合项不参与合批）
		item.transparent = impl->particle_blend_mode != Blend_Mode::Opaque;
		//登记待绘制项
		impl->pending_items.push_back(item);
		//登记路径观测（第三百四十八轮；3 = GPU 间接绘制）
		impl->particle_draw_path_last = 3;
		return true;
	}

	bool Renderer_Particle::particle_indirect_command_get(uint32_t command_out[5])
	{
		//输出指针为空
		if (command_out == nullptr)
			return false;
		//未初始化 / 未开启常驻 / 尚未写入
		if (impl == nullptr || !impl->initialized || !impl->particle_resident || !impl->particle_indirect_written)
			return false;
		//最近一次写入的帧槽
		uint32_t slot = impl->particle_indirect_last_slot;
		if (slot >= impl->particle_indirect_buffers.size())
			return false;
		return self().compute_buffer_read(impl->particle_indirect_buffers[slot], command_out,
			static_cast<uint32_t>(sizeof(uint32_t) * 5u));
	}
}
