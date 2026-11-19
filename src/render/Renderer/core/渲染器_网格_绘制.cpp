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
	//把一批模型矩阵追加入待绘制实例存储（列主序 16 浮点 / 个，材质 4 ＋ 卡通 4 ＋ 暗部色调 4 ＋ PBR 特性 4 浮点 / 个），返回首个实例的字节偏移
	//（文件内辅助函数；容量是否足够由调用方先行校验，此处不重复检查）
	static VkDeviceSize instance_matrices_append(std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& store,
		const float* transforms, uint32_t count, bool material_active, float roughness, float metallic,
		float emissive, bool toon_active, float toon_steps, float toon_rim, float toon_specular,
		float toon_tint_r, float toon_tint_g, float toon_tint_b,
		bool pbr_features_active, uint32_t pbr_features, float tone_operator)
	{
		//首个实例的字节偏移（追加前的元素个数 × 实例步长）
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
			/*
			逐物体卡通参数（第四百零八轮）
			覆盖标志置于第 4 分量（w）：0 = 不覆盖，片元着色器回落场景 UBO 的全局卡通参数；
			故未带 toon_override 的绘制项与旧版逐字节一致。
			*/
			target[INSTANCE_TOON_OFFSET + 0] = toon_active ? toon_steps : 0.0f;
			target[INSTANCE_TOON_OFFSET + 1] = toon_active ? toon_rim : 0.0f;
			target[INSTANCE_TOON_OFFSET + 2] = toon_active ? toon_specular : 0.0f;
			target[INSTANCE_TOON_OFFSET + 3] = toon_active ? 1.0f : 0.0f;
			/*
			逐物体卡通暗部色调（第四百零九轮）
			xyz = 色调；仅当卡通覆盖标志为真时被片元读取（默认 (1,1,1) 即不色偏）；
			w 保留为 0。
			*/
			target[INSTANCE_TOON_TINT_OFFSET + 0] = toon_tint_r;
			target[INSTANCE_TOON_TINT_OFFSET + 1] = toon_tint_g;
			target[INSTANCE_TOON_TINT_OFFSET + 2] = toon_tint_b;
			target[INSTANCE_TOON_TINT_OFFSET + 3] = 0.0f;
			/*
			逐物体 PBR 特性覆盖（第 777 轮）
			x = 覆盖标志（0 = 不覆盖，片元回落场景 UBO 的全局开关）、y = 位掩码（仅覆盖标志为真时被读取）；
			zw 保留为 0，故未设置覆盖的绘制项与引入前逐字节一致。
			*/
			target[INSTANCE_PBR_FEATURES_OFFSET + 0] = pbr_features_active ? 1.0f : 0.0f;
			target[INSTANCE_PBR_FEATURES_OFFSET + 1] = pbr_features_active ? static_cast<float>(pbr_features) : 0.0f;
			//z = 逐材质色调算子（第 779 轮；0..4 = 具体曲线、255 = 继承全局默认）
			target[INSTANCE_PBR_FEATURES_OFFSET + 2] = tone_operator;
			target[INSTANCE_PBR_FEATURES_OFFSET + 3] = 0.0f;
		}
		return offset;
	}

	namespace
	{
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


	//生成实例追加所需的材质值快照
	static Instance_Material_Values instance_material_values_make(const Resource_State& state)
	{
		Instance_Material_Values values{};
		values.pbr_active = state.material_pbr_active;
		values.pbr_roughness = state.material_pbr_roughness;
		values.pbr_metallic = state.material_pbr_metallic;
		values.pbr_features_active = state.material_pbr_features_active;
		values.pbr_features = state.material_pbr_features;
		//逐材质色调算子（第 779 轮；0xFF = 继承全局默认）
		values.tone_operator = state.material_tone_operator <= 4
			? static_cast<float>(state.material_tone_operator) : 255.0f;
		values.emissive = state.material_emissive;
		values.toon_active = state.material_toon_active;
		values.toon_steps = state.material_toon_steps;
		values.toon_rim = state.material_toon_rim;
		values.toon_specular = state.material_toon_specular;
		for (std::size_t channel = 0; channel < 3; ++channel)
			values.toon_tint[channel] = state.material_toon_tint[channel];
		return values;
	}

	//单项网格的视锥剔除、容量检查与实例追加
	bool Render_Device::mesh_single_prepare(const float* transform, bool bounds_valid,
		const float bounds_min[3], const float bounds_max[3], const Instance_Material_Values& material,
		bool& registered, uint64_t& instance_offset)
	{
		registered = false;
		instance_offset = 0;
		if (state_ptr->cull_enabled && bounds_valid)
		{
			float view_proj[16] = {};
			owner->scene().camera_view_proj(view_proj);
			const float* model = transform != nullptr ? transform : IDENTITY_MATRIX4;
			if (frustum_cull_aabb(view_proj, model, bounds_min, bounds_max))
			{
				++state_ptr->culled_items;
				++state_ptr->frame_culled_items;
				return true;
			}
		}
		if (state_ptr->pending_instances.size() >= state_ptr->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储已满（容量 {}），绘制被拒", state_ptr->instance_capacity);
			return false;
		}
		instance_offset = static_cast<uint64_t>(instance_matrices_append(state_ptr->pending_instances, transform, 1,
			material.pbr_active, material.pbr_roughness, material.pbr_metallic, material.emissive,
			material.toon_active, material.toon_steps, material.toon_rim, material.toon_specular,
			material.toon_tint[0], material.toon_tint[1], material.toon_tint[2],
			material.pbr_features_active, material.pbr_features, material.tone_operator));
		registered = true;
		return true;
	}

	//实例批次的 GPU 优先 / CPU 回落剔除与可见实例追加
	bool Render_Device::mesh_instances_prepare(const float* transforms, uint32_t count, bool bounds_valid,
		const float bounds_min[3], const float bounds_max[3], const Instance_Material_Values& material,
		uint32_t& visible_count, uint64_t& instance_offset)
	{
		visible_count = count;
		instance_offset = 0;
		const bool has_cull_input = transforms != nullptr && state_ptr->cull_enabled && bounds_valid;
		const float* visible_transforms = transforms;
		bool gpu_compacted = false;
		if (has_cull_input && owner->compute().gpu_cull_enable_get())
		{
			std::vector<float> compacted(static_cast<std::size_t>(count) * 16, 0.0f);
			uint32_t gpu_visible = 0;
			if (owner->compute().gpu_instance_compact(transforms, count, bounds_min, bounds_max,
				compacted.data(), &gpu_visible))
			{
				const uint32_t culled_count = count - gpu_visible;
				state_ptr->culled_instances += culled_count;
				state_ptr->frame_culled_instances += culled_count;
				if (gpu_visible == 0)
				{
					++state_ptr->culled_items;
					++state_ptr->frame_culled_items;
					visible_count = 0;
					return true;
				}
				state_ptr->instanced_cull_scratch.assign(compacted.begin(),
					compacted.begin() + static_cast<std::ptrdiff_t>(gpu_visible) * 16);
				visible_transforms = state_ptr->instanced_cull_scratch.data();
				visible_count = gpu_visible;
				gpu_compacted = true;
			}
		}
		if (!gpu_compacted && has_cull_input)
		{
			const auto cull_begin = std::chrono::high_resolution_clock::now();
			float view_proj[16] = {};
			owner->scene().camera_view_proj(view_proj);
			state_ptr->instanced_cull_mask.assign(count, 0);
			const std::size_t cull_parallel_min = state_ptr->cull_parallel_min_instances;
			if (state_ptr->thread_pool.active() && count >= cull_parallel_min)
			{
				const uint64_t dispatch_before = state_ptr->thread_pool.dispatch_count_get();
				state_ptr->thread_pool.ranges_run(count, cull_parallel_min,
					[this, view_proj, transforms, bounds_min, bounds_max](std::size_t begin, std::size_t end)
					{
						for (std::size_t instance = begin; instance < end; ++instance)
							state_ptr->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
								transforms + instance * 16, bounds_min, bounds_max) ? 0 : 1;
					});
				state_ptr->cull_parallel_dispatches += state_ptr->thread_pool.dispatch_count_get() - dispatch_before;
			}
			else
			{
				for (uint32_t instance = 0; instance < count; ++instance)
					state_ptr->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
						transforms + static_cast<std::size_t>(instance) * 16, bounds_min, bounds_max) ? 0 : 1;
			}
			state_ptr->instanced_cull_scratch.clear();
			state_ptr->instanced_cull_scratch.reserve(static_cast<std::size_t>(count) * 16);
			for (uint32_t instance = 0; instance < count; ++instance)
				if (state_ptr->instanced_cull_mask[instance] != 0)
				{
					const float* matrix = transforms + static_cast<std::size_t>(instance) * 16;
					state_ptr->instanced_cull_scratch.insert(state_ptr->instanced_cull_scratch.end(), matrix, matrix + 16);
				}
			visible_count = static_cast<uint32_t>(state_ptr->instanced_cull_scratch.size() / 16);
			const uint32_t culled_count = count - visible_count;
			const auto cull_end = std::chrono::high_resolution_clock::now();
			state_ptr->frame_cull_ms += std::chrono::duration<double, std::milli>(cull_end - cull_begin).count();
			state_ptr->culled_instances += culled_count;
			state_ptr->frame_culled_instances += culled_count;
			if (visible_count == 0)
			{
				++state_ptr->culled_items;
				++state_ptr->frame_culled_items;
				return true;
			}
			visible_transforms = state_ptr->instanced_cull_scratch.data();
		}
		if (state_ptr->pending_instances.size() + visible_count > state_ptr->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储不足（需 {} 个，余 {} 个）", visible_count,
				state_ptr->instance_capacity - static_cast<uint32_t>(state_ptr->pending_instances.size()));
			visible_count = count;
			return false;
		}
		instance_offset = static_cast<uint64_t>(instance_matrices_append(state_ptr->pending_instances,
			visible_transforms, visible_count, material.pbr_active, material.pbr_roughness, material.pbr_metallic,
			material.emissive, material.toon_active, material.toon_steps, material.toon_rim, material.toon_specular,
			material.toon_tint[0], material.toon_tint[1], material.toon_tint[2],
			material.pbr_features_active, material.pbr_features, material.tone_operator));
		return true;
	}

	//复制推送块并按先描边后主体的顺序登记网格项
	void Render_Device::mesh_item_push(Frame_Render_Item item, const std::array<float, 32>* push_constants)
	{
		if (push_constants != nullptr)
		{
			state_ptr->pending_push_constants.push_back(*push_constants);
			item.push_constants = state_ptr->pending_push_constants.back().data();
			item.push_constant_size = static_cast<uint32_t>(sizeof(float) * state_ptr->pending_push_constants.back().size());
		}
		outline_item_push(item);
		state_ptr->pending_items.push_back(item);
	}

	//复制推送块并登记粒子项
	void Render_Device::particle_item_push(Frame_Render_Item item, const std::array<float, 32>& push_constants)
	{
		state_ptr->pending_push_constants.push_back(push_constants);
		item.push_constants = state_ptr->pending_push_constants.back().data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push_constants.size());
		state_ptr->pending_items.push_back(item);
	}

	std::size_t Render_Device::frame_item_count(void) const
	{
		return state_ptr->pending_items.size();
	}

	std::size_t Render_Device::frame_push_count(void) const
	{
		return state_ptr->pending_push_constants.size();
	}

	//对刚登记的 CPU 粒子实例项补齐外观、软深度与专属采样集条件
	bool Render_Device::particle_item_finish(std::size_t items_before, std::size_t push_before,
		uint64_t default_view, uint64_t default_sampler, uint64_t depth_view,
		uint64_t depth_sampler, float soft_range)
	{
		if (state_ptr->pending_items.size() <= items_before || state_ptr->pending_push_constants.size() <= push_before)
			return false;
		Frame_Render_Item& item = state_ptr->pending_items.back();
		std::array<float, 32>& push = state_ptr->pending_push_constants.back();
		item.cast_shadow = false;
		owner->compute().particle_style_push_fill(push);
		item.draw_depth = reinterpret_cast<VkImageView>(depth_view);
		item.draw_depth_sampler = reinterpret_cast<VkSampler>(depth_sampler);
		push[28] = soft_range;
		if (item.draw_albedo == VK_NULL_HANDLE || item.draw_sampler == VK_NULL_HANDLE)
		{
			item.draw_albedo = reinterpret_cast<VkImageView>(default_view);
			item.draw_sampler = reinterpret_cast<VkSampler>(default_sampler);
		}
		return true;
	}

	//逐帧槽绑定粒子描述符所需的三个原生缓冲
	bool Render_Device::particle_scene_buffers_bind(uint64_t state_buffer, uint64_t index_buffer)
	{
		for (uint32_t slot = 0; slot < state_ptr->scene_ubos.size(); ++slot)
			if (!owner->resource().particle_buffers_bind(slot, reinterpret_cast<uint64_t>(state_ptr->scene_ubos[slot]),
				state_buffer, index_buffer))
				return false;
		return true;
	}

	//按程序化球体加载网格（第六十六轮新增）

	bool Render_Resource::mesh_draw(Mesh_Handle handle, const float* transform, const float* color)
	{
		return mesh_draw_range(handle, transform, color, 0u, 0u);
	}

	bool Render_Resource::mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
		uint32_t first_index, uint32_t range_count)
	{
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 网格绘制失败：尚未初始化");
			return false;
		}
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!(*state_ptr).mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 网格绘制失败：网格句柄 {} 无效", handle);
			return false;
		}
		if (first_index != 0u || range_count != 0u)
		{
			if (first_index >= index_count || range_count > index_count - first_index)
			{
				logger.error("渲染器:: 索引区间越界（首 {} ＋ 长 {}，网格索引数 {}）",
					first_index, range_count, index_count);
				return false;
			}
			index_offset += static_cast<VkDeviceSize>(first_index) * sizeof(uint32_t);
			index_count = range_count;
		}
		//设备域承接单项剔除、容量检查与材质实例追加
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		const bool bounds_valid = owner->device().cull_enabled()
			&& (*state_ptr).mesh.mesh_bounds_get(handle, bounds_min, bounds_max);
		bool registered = false;
		uint64_t instance_offset = 0;
		if (!owner->device().mesh_single_prepare(transform, bounds_valid, bounds_min, bounds_max,
			instance_material_values_make(*state_ptr), registered, instance_offset))
			return false;
		if (!registered)
			return true;
		Frame_Render_Item item{};
		item.pipeline = (*state_ptr).pipeline.pipeline_get();
		item.layout = (*state_ptr).pipeline.layout_get();
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
		item.draw_sampler = (*state_ptr).bound_sampler;
		item.draw_albedo = (*state_ptr).bound_view;
		item.draw_normal = (*state_ptr).material_views[0];
		item.draw_roughness = (*state_ptr).material_views[1];
		item.draw_emissive = (*state_ptr).material_views[2];
		item.draw_ao = (*state_ptr).material_views[3];
		item.draw_metallic = (*state_ptr).material_views[4];
		const bool text_drawing = owner->ui().text_drawing();
		item.cast_shadow = !text_drawing;
		item.transparent = (*state_ptr).pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		//alpha 裁剪材质不参与编号预通道（第 814 轮；编号通道无法复现 discard）
		item.alpha_clip = !text_drawing && (*state_ptr).material_alpha_cutoff > 0.0f;
		//文字保持描边参数、恒等 UV 与零清漆；非文字保持显式色、吸附色调、白色的优先顺序
		const float* push_color = color;
		if (push_color == nullptr && !text_drawing)
			push_color = (*state_ptr).material_tint_active ? (*state_ptr).material_tint : WHITE_COLOR_RGBA;
		std::array<float, 32> push{};
		const std::array<float, 32>* push_ptr = nullptr;
		if (push_color != nullptr)
		{
			push.fill(0.0f);
			push[0] = 1.0f;
			push[5] = 1.0f;
			push[10] = 1.0f;
			push[15] = 1.0f;
			for (std::size_t index = 0; index < 4; ++index)
				push[16 + index] = push_color[index];
			if (text_drawing)
				owner->ui().text_outline_get(push.data() + 20);
			else
			{
				push[20] = (*state_ptr).material_normal_strength;
				push[21] = (*state_ptr).material_normal_strength > 0.0f ? 1.0f : 0.0f;
				push[22] = (*state_ptr).material_alpha_cutoff;
				push[23] = (*state_ptr).material_uv_rotation;
			}
			if (text_drawing)
			{
				push[24] = 1.0f;
				push[25] = 1.0f;
			}
			else
			{
				push[24] = (*state_ptr).material_uv_scale[0];
				push[25] = (*state_ptr).material_uv_scale[1];
				push[26] = (*state_ptr).material_uv_offset[0];
				push[27] = (*state_ptr).material_uv_offset[1];
				push[28] = (*state_ptr).material_clear_coat;
				push[29] = (*state_ptr).material_clear_coat_roughness;
			}
			push_ptr = &push;
		}
		owner->device().mesh_item_push(item, push_ptr);
		return true;
	}

	bool Render_Resource::mesh_draw_sections(Mesh_Handle handle, const float* transform,
		const Material_Handle* materials, uint32_t count, const Material_Instance_Data* instances)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 多材质绘制失败：尚未初始化");
			return false;
		}
		//材质数组非空且段数为正
		if (materials == nullptr || count == 0)
		{
			logger.warn("渲染器:: 多材质绘制参数非法（材质数组为空或段数 0），已忽略");
			return false;
		}
		/*
		套用材质实例覆盖（第四百六十九轮；阶段 C）
		在 material_bind_set 之后写入吸附状态：tint **乘到**当前绑定色（默认 1 即等价模板）；
		自发光直接覆盖模板值。模板句柄无效时整条实例忽略（装配阶段已校验，此处只做防御）。
		*/
		auto apply_instance = [this](const Material_Instance_Data& data)
		{
			if (data.模板 == 0 || data.模板 > (*state_ptr).materials.size() || !(*state_ptr).materials[data.模板 - 1].valid)
				return;
			//覆盖底色（乘性叠加到模板色调）
			if (data.use_tint)
			{
				(*state_ptr).material_tint_active = true;
				for (std::size_t index = 0; index < 4; ++index)
					(*state_ptr).material_tint[index] *= data.tint[index];
			}
			//覆盖自发光（直接覆盖模板值）
			if (data.use_emissive)
				(*state_ptr).material_emissive = data.emissive;
			//逐实例卡通（第四百七十一轮；三项直接覆盖模板卡通参数）
			if (data.use_toon)
			{
				(*state_ptr).material_toon_active = true;
				(*state_ptr).material_toon_steps = data.toon_steps;
				(*state_ptr).material_toon_rim = data.toon_rim;
				(*state_ptr).material_toon_specular = data.toon_specular;
			}
			//逐实例 PBR（第五百六十四轮；覆盖即启用）
			if (data.use_pbr)
			{
				(*state_ptr).material_pbr_active = true;
				(*state_ptr).material_pbr_roughness = data.pbr_roughness;
				(*state_ptr).material_pbr_metallic = data.pbr_metallic;
			}
			//逐实例清漆（第五百九十四轮；覆盖即启用）
			if (data.use_clearcoat)
			{
				(*state_ptr).material_clear_coat = data.clear_coat;
				(*state_ptr).material_clear_coat_roughness = data.clear_coat_roughness;
			}
		};
		//取网格索引总数（均分回落用）
		uint32_t vertex_count = 0;
		uint32_t total_indices = 0;
		if (!(*state_ptr).mesh.mesh_counts_get(handle, vertex_count, total_indices))
		{
			logger.error("渲染器:: 多材质绘制失败：网格句柄 {} 无效", handle);
			return false;
		}
		/*
		子网格级分段（第四百一十四轮）
		网格自带分段描述时按段绘制：每段按其 material 槽取材质（越界回落首个），区间即段区间；
		无分段时回落到「按材质数均分」的旧口径。
		*/
		std::vector<Mesh_Section> sections;
		if ((*state_ptr).mesh.mesh_sections_get(handle, sections) && !sections.empty())
		{
			bool success = true;
			for (const Mesh_Section& section : sections)
			{
				//材质槽越界回落首个材质
				uint32_t slot = section.material < count ? section.material : 0u;
				material_bind_set(materials[slot]);
				//套用该段实例覆盖（第四百六十九轮）
				if (instances != nullptr)
					apply_instance(instances[slot]);
				if (!mesh_draw_range(handle, transform, nullptr, section.first_index, section.index_count))
					success = false;
			}
			//恢复默认绑定
			material_bind_set(0);
			return success;
		}
		//每段索引数按 3 对齐（避免切断三角形），余数归最后一段
		uint32_t segment = ((total_indices / count) / 3u) * 3u;
		//段数多于三角形数：无法均分
		if (segment == 0)
		{
			logger.warn("渲染器:: 多材质绘制段数 {} 多于三角形数（索引总数 {}），已忽略", count, total_indices);
			return false;
		}
		//逐段绑定材质并按区间登记
		uint32_t first = 0;
		bool success = true;
		for (uint32_t index = 0; index < count; ++index)
		{
			//最后一段吞掉余数
			uint32_t length = (index + 1u == count) ? (total_indices - first) : segment;
			//区间为空则跳过
			if (length == 0)
				continue;
			//绑定该段的材质（0 或无效句柄由绑定入口回落默认）
			material_bind_set(materials[index]);
			//套用该段实例覆盖（第四百六十九轮）
			if (instances != nullptr)
				apply_instance(instances[index]);
			if (!mesh_draw_range(handle, transform, nullptr, first, length))
				success = false;
			first += length;
		}
		//恢复默认绑定
		material_bind_set(0);
		return success;
	}

	bool Render_Resource::mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count)
	{
		if (owner == nullptr || !owner->device().valid())
			return false;
		if (count == 0)
		{
			logger.error("渲染器:: 实例化绘制的实例数必须为正");
			return false;
		}
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!(*state_ptr).mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 实例化绘制的网格句柄无效");
			return false;
		}
		//设备域完成 GPU 优先 / CPU 回落剔除，并只追加可见实例
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		const bool bounds_valid = owner->device().cull_enabled()
			&& (*state_ptr).mesh.mesh_bounds_get(handle, bounds_min, bounds_max);
		uint64_t instance_offset = 0;
		if (!owner->device().mesh_instances_prepare(transforms, count, bounds_valid, bounds_min, bounds_max,
			instance_material_values_make(*state_ptr), count, instance_offset))
			return false;
		if (count == 0)
			return true;
		Frame_Render_Item item{};
		item.pipeline = (*state_ptr).pipeline.pipeline_get();
		item.layout = (*state_ptr).pipeline.layout_get();
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
		item.draw_sampler = (*state_ptr).bound_sampler;
		item.draw_albedo = (*state_ptr).bound_view;
		item.draw_normal = (*state_ptr).material_views[0];
		item.draw_roughness = (*state_ptr).material_views[1];
		item.draw_emissive = (*state_ptr).material_views[2];
		item.draw_ao = (*state_ptr).material_views[3];
		item.draw_metallic = (*state_ptr).material_views[4];
		item.cast_shadow = !owner->ui().text_drawing();
		item.transparent = (*state_ptr).pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		//alpha 裁剪材质不参与编号预通道（第 814 轮；编号通道无法复现 discard）
		item.alpha_clip = !owner->ui().text_drawing() && (*state_ptr).material_alpha_cutoff > 0.0f;
		std::array<float, 32> push{};
		push.fill(0.0f);
		push[0] = 1.0f;
		push[5] = 1.0f;
		push[10] = 1.0f;
		push[15] = 1.0f;
		for (std::size_t index = 0; index < 4; ++index)
			push[16 + index] = (*state_ptr).material_tint_active ? (*state_ptr).material_tint[index] : 1.0f;
		push[20] = (*state_ptr).material_normal_strength;
		push[21] = (*state_ptr).material_normal_strength > 0.0f ? 1.0f : 0.0f;
		push[22] = (*state_ptr).material_alpha_cutoff;
		push[23] = (*state_ptr).material_uv_rotation;
		push[24] = (*state_ptr).material_uv_scale[0];
		push[25] = (*state_ptr).material_uv_scale[1];
		push[26] = (*state_ptr).material_uv_offset[0];
		push[27] = (*state_ptr).material_uv_offset[1];
		push[28] = (*state_ptr).material_clear_coat;
		push[29] = (*state_ptr).material_clear_coat_roughness;
		owner->device().mesh_item_push(item, &push);
		return true;
	}
}
