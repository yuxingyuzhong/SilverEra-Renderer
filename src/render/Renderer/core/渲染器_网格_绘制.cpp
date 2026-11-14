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
	//把一批模型矩阵追加入待绘制实例存储（列主序 16 浮点 / 个，材质 4 ＋ 卡通 4 ＋ 暗部色调 4 浮点 / 个），返回首个实例的字节偏移
	//（文件内辅助函数；容量是否足够由调用方先行校验，此处不重复检查）
	static VkDeviceSize instance_matrices_append(std::vector<std::array<float, INSTANCE_FLOAT_COUNT>>& store,
		const float* transforms, uint32_t count, bool material_active, float roughness, float metallic,
		float emissive, bool toon_active, float toon_steps, float toon_rim, float toon_specular,
		float toon_tint_r, float toon_tint_g, float toon_tint_b)
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

	//按程序化球体加载网格（第六十六轮新增）

	bool Render_Resource::mesh_draw(Mesh_Handle handle, const float* transform, const float* color)
	{
		return mesh_draw_range(handle, transform, color, 0u, 0u);
	}

	bool Render_Resource::mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
		uint32_t first_index, uint32_t range_count)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
		{
			logger.error("渲染器:: 网格绘制失败：尚未初始化");
			return false;
		}

		//取网格共用缓冲、索引区偏移与索引数
		VkBuffer mesh_buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		uint32_t index_count = 0;
		if (!state().mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
		{
			logger.error("渲染器:: 网格绘制失败：网格句柄 {} 无效", handle);
			return false;
		}
		/*
		索引区间（第四百一十轮）
		(0, 0) 表示整段（mesh_draw 的转发口径）；否则校验区间并把索引偏移推进到区间起点。
		*/
		if (first_index != 0u || range_count != 0u)
		{
			//区间越界（首索引超出总数或区间长度溢出）
			if (first_index >= index_count || range_count > index_count - first_index)
			{
				logger.error("渲染器:: 索引区间越界（首 {} ＋ 长 {}，网格索引数 {}）",
					first_index, range_count, index_count);
				return false;
			}
			//索引偏移推进到区间起点（uint32 索引，步长 4 字节）
			index_offset += static_cast<VkDeviceSize>(first_index) * sizeof(uint32_t);
			index_count = range_count;
		}

		/*
		实例级视锥剔除（第二十二轮新增）
		完全在视锥外的绘制项**不登记**（合法跳过，不是错误）；返回真表示已剔除。
		保守口径：包围盒与视锥相交一律保留，故画面与旧版一致、仅减少不可见项。
		*/
		float bounds_min[3] = {};
		float bounds_max[3] = {};
		if (owner->impl->cull_enabled && state().mesh.mesh_bounds_get(handle, bounds_min, bounds_max))
		{
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			owner->impl->camera.view_proj_get(view_proj);
			//模型矩阵（空指针视作单位矩阵）
			const float* model = (transform != nullptr) ? transform : IDENTITY_MATRIX4;
			//若包围盒完全落在视锥外则跳过登记
			if (frustum_cull_aabb(view_proj, model, bounds_min, bounds_max))
			{
				//计入剔除观测（累计量与最近一帧量）
				++owner->impl->culled_items;
				++owner->impl->frame_culled_items;
				return true;
			}
		}

		/*
		模型矩阵改由「实例缓冲」承载（本层统一走实例路径）
		transform 为空则用单位矩阵；实例数为 1 即等价于逐次绘制（模型矩阵不再经推送常量传递）。
		*/
		if (owner->impl->pending_instances.size() >= owner->impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储已满（容量 {}），绘制被拒", owner->impl->instance_capacity);
			return false;
		}
		//追加本项模型矩阵与材质并取其在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(owner->impl->pending_instances, transform, 1,
			state().material_pbr_active, state().material_pbr_roughness, state().material_pbr_metallic,
			state().material_emissive, state().material_toon_active, state().material_toon_steps,
			state().material_toon_rim, state().material_toon_specular, state().material_toon_tint[0],
			state().material_toon_tint[1], state().material_toon_tint[2]);

		/*
		组装待绘制项
		描述符集（set0 场景 UBO / set1 采样器）延迟到 frame_render 时按「本帧帧槽」填装，
		故此处先置空、数量为 0。
		*/
		Frame_Render_Item item{};
		item.pipeline = owner->impl->pipeline.pipeline_get();
		item.layout = owner->impl->pipeline.layout_get();
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
		item.draw_sampler = state().bound_sampler;
		item.draw_albedo = state().bound_view;
		item.draw_normal = state().material_views[0];
		item.draw_roughness = state().material_views[1];
		item.draw_emissive = state().material_views[2];
		item.draw_ao = state().material_views[3];
		item.draw_metallic = state().material_views[4];
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !owner->ui().state().in_text_draw;
		//透明标记（第一百三十轮：文字等非不透明混合项不参与合批，避免混叠顺序改变）
		item.transparent = owner->impl->pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		/*
		颜色 / 色调推送（文字着色 ＋ 第六十九轮逐物体色调）
		矩阵已由实例缓冲承载，推送块前 64 字节仅用于占位（单位矩阵），后 16 字节为颜色；
		存储于 Impl 的 deque（元素引用稳定），绘制项持其指针，直到 frame_render 消费。
		非文字路径**恒推送**：显式 color 优先，其次吸附式逐物体色调，最后回落白色——
		场景片元着色器把反照率 × 该颜色，白色时画面逐字节不变。
		*/
		const float* push_color = color;
		//非文字且未显式给出颜色时，改用吸附色调或白色
		if (push_color == nullptr && !owner->ui().state().in_text_draw)
			push_color = state().material_tint_active
				? state().material_tint
				: WHITE_COLOR_RGBA;
		if (push_color != nullptr)
		{
			owner->impl->pending_push_constants.emplace_back();
			std::array<float, 32>& push = owner->impl->pending_push_constants.back();
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
			if (owner->ui().state().in_text_draw)
			{
				push[20] = owner->ui().state().text_outline_uv_width;
				push[21] = owner->ui().state().text_outline_color[0];
				push[22] = owner->ui().state().text_outline_color[1];
				push[23] = owner->ui().state().text_outline_color[2];
			}
			else
			{
				push[20] = state().material_normal_strength;
				push[21] = state().material_normal_strength > 0.0f ? 1.0f : 0.0f;
				//Alpha 裁剪阈值与 UV 旋转（第一百八十三轮；默认 0 = 关闭 / 不旋转）
				push[22] = state().material_alpha_cutoff;
				push[23] = state().material_uv_rotation;
			}
			/*
			贴图 UV 变换（第一百七十一轮；offset 96 的 vec4）
			xy = 缩放、zw = 偏移；文字绘制不采样材质贴图，保持恒等变换（1,1,0,0）。
			*/
			if (owner->ui().state().in_text_draw)
			{
				push[24] = 1.0f;
				push[25] = 1.0f;
			}
			else
			{
				push[24] = state().material_uv_scale[0];
				push[25] = state().material_uv_scale[1];
				push[26] = state().material_uv_offset[0];
				push[27] = state().material_uv_offset[1];
			}
			/*
			清漆层（第二百三十三轮；offset 112 的 vec4）
			x = 清漆强度、y = 清漆粗糙度；文字绘制无材质语义，恒为零（关闭）。
			*/
			if (!owner->ui().state().in_text_draw)
			{
				push[28] = state().material_clear_coat;
				push[29] = state().material_clear_coat_roughness;
			}
			item.push_constants = push.data();
			item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		}

		//先登记描边项（外扩轮廓须先写入深度，主体几何随后按更近深度覆盖内部）
		owner->impl->outline_item_push(item);
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		owner->impl->pending_items.push_back(item);
		return true;
	}

	bool Render_Resource::mesh_draw_sections(Mesh_Handle handle, const float* transform,
		const Material_Handle* materials, uint32_t count, const Material_Instance_Data* instances)
	{
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
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
			if (data.模板 == 0 || data.模板 > state().materials.size() || !state().materials[data.模板 - 1].valid)
				return;
			//覆盖底色（乘性叠加到模板色调）
			if (data.use_tint)
			{
				state().material_tint_active = true;
				for (std::size_t index = 0; index < 4; ++index)
					state().material_tint[index] *= data.tint[index];
			}
			//覆盖自发光（直接覆盖模板值）
			if (data.use_emissive)
				state().material_emissive = data.emissive;
			//逐实例卡通（第四百七十一轮；三项直接覆盖模板卡通参数）
			if (data.use_toon)
			{
				state().material_toon_active = true;
				state().material_toon_steps = data.toon_steps;
				state().material_toon_rim = data.toon_rim;
				state().material_toon_specular = data.toon_specular;
			}
			//逐实例 PBR（第五百六十四轮；覆盖即启用）
			if (data.use_pbr)
			{
				state().material_pbr_active = true;
				state().material_pbr_roughness = data.pbr_roughness;
				state().material_pbr_metallic = data.pbr_metallic;
			}
			//逐实例清漆（第五百九十四轮；覆盖即启用）
			if (data.use_clearcoat)
			{
				state().material_clear_coat = data.clear_coat;
				state().material_clear_coat_roughness = data.clear_coat_roughness;
			}
		};
		//取网格索引总数（均分回落用）
		uint32_t vertex_count = 0;
		uint32_t total_indices = 0;
		if (!state().mesh.mesh_counts_get(handle, vertex_count, total_indices))
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
		if (state().mesh.mesh_sections_get(handle, sections) && !sections.empty())
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
		//若尚未初始化
		if (owner->impl == nullptr || !owner->impl->initialized)
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
		if (!state().mesh.mesh_record_get(handle, mesh_buffer, index_offset, index_count))
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
		//是否具备剔除前置（变换 ＋ 剔除开启 ＋ 网格包围盒）
		const bool has_cull_input = transforms != nullptr && owner->impl->cull_enabled
			&& state().mesh.mesh_bounds_get(handle, bounds_min, bounds_max);
		//GPU 实例压缩优先（第二百四十九轮；失败回落 CPU 路径）
		bool gpu_compacted = false;
		if (has_cull_input && owner->impl->gpu_cull_enabled)
		{
			std::vector<float> compacted(static_cast<std::size_t>(count) * 16, 0.0f);
			uint32_t visible_count = 0;
			//压缩成功则直接采用 GPU 结果（与 CPU 剔除顺序 / 内容一致）
			if (owner->gpu_instance_compact(transforms, count, bounds_min, bounds_max, compacted.data(), &visible_count))
			{
				const uint32_t culled_count = count - visible_count;
				owner->impl->culled_instances += culled_count;
				owner->impl->frame_culled_instances += culled_count;
				//整批不可见按一个绘制项计
				if (visible_count == 0)
				{
					++owner->impl->culled_items;
					++owner->impl->frame_culled_items;
					return true;
				}
				owner->impl->instanced_cull_scratch.assign(compacted.begin(),
					compacted.begin() + static_cast<std::ptrdiff_t>(visible_count) * 16);
				transforms = owner->impl->instanced_cull_scratch.data();
				count = visible_count;
				gpu_compacted = true;
			}
		}
		//CPU 剔除路径（默认；GPU 压缩失败时回落）
		if (!gpu_compacted && has_cull_input)
		{
			//剔除计时起点（第二百四十七轮）
			const auto cull_begin = std::chrono::high_resolution_clock::now();
			//当前视图 × 投影矩阵
			float view_proj[16] = {};
			owner->impl->camera.view_proj_get(view_proj);
			/*
			可见性掩码（第一百七十七轮）
			逐实例只写自己的掩码槽（1 = 可见、0 = 剔除），故可分块并行且与单线程结果完全一致；
			实例数达到阈值且工作线程可用时走并行，否则由调用线程整段执行。
			*/
			owner->impl->instanced_cull_mask.assign(count, 0);
			//本帧生效的并行阈值（默认 256；可由配置调整，第二百二十二轮）
			const std::size_t cull_parallel_min = owner->impl->cull_parallel_min_instances;
			if (owner->impl->thread_pool.active() && count >= cull_parallel_min)
			{
				//派发计数差值（第二百二十四轮：按用途归入剔除）
				const uint64_t dispatch_before = owner->impl->thread_pool.dispatch_count_get();
				//并行判定（块之间无共享写入）
				owner->impl->thread_pool.ranges_run(count, cull_parallel_min,
					[this, view_proj, transforms, bounds_min, bounds_max](std::size_t begin, std::size_t end)
					{
						for (std::size_t instance = begin; instance < end; ++instance)
							owner->impl->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
								transforms + instance * 16, bounds_min, bounds_max) ? 0 : 1;
					});
				//累加剔除用途的派发次数
				owner->impl->cull_parallel_dispatches += owner->impl->thread_pool.dispatch_count_get() - dispatch_before;
			}
			else
			{
				//单线程判定（与引入并行前逐字节一致）
				for (uint32_t instance = 0; instance < count; ++instance)
					owner->impl->instanced_cull_mask[instance] = frustum_cull_aabb(view_proj,
						transforms + static_cast<std::size_t>(instance) * 16, bounds_min, bounds_max) ? 0 : 1;
			}
			//筛选可见实例（按原相对顺序压缩；整块拷贝 16 浮点）
			owner->impl->instanced_cull_scratch.clear();
			owner->impl->instanced_cull_scratch.reserve(static_cast<std::size_t>(count) * 16);
			for (uint32_t instance = 0; instance < count; ++instance)
				//仅可见实例写入压缩缓冲
				if (owner->impl->instanced_cull_mask[instance] != 0)
				{
					const float* matrix = transforms + static_cast<std::size_t>(instance) * 16;
					owner->impl->instanced_cull_scratch.insert(owner->impl->instanced_cull_scratch.end(), matrix, matrix + 16);
				}
			//可见 / 剔除的实例数
			const uint32_t visible_count = static_cast<uint32_t>(owner->impl->instanced_cull_scratch.size() / 16);
			const uint32_t culled_count = count - visible_count;
			//累计本帧剔除耗时（第二百四十七轮）
			const auto cull_end = std::chrono::high_resolution_clock::now();
			owner->impl->frame_cull_ms += std::chrono::duration<double, std::milli>(cull_end - cull_begin).count();
			owner->impl->culled_instances += culled_count;
			owner->impl->frame_culled_instances += culled_count;
			//若整批不可见则不登记（按一个绘制项计）
			if (visible_count == 0)
			{
				++owner->impl->culled_items;
				++owner->impl->frame_culled_items;
				return true;
			}
			//以筛选后的可见子集继续登记
			transforms = owner->impl->instanced_cull_scratch.data();
			count = visible_count;
		}
		/*
		本帧实例矩阵存储容量校验
		不足则整体拒绝、不登记任何绘制项（避免留下「半个实例化绘制」）。
		*/
		if (owner->impl->pending_instances.size() + count > owner->impl->instance_capacity)
		{
			logger.error("渲染器:: 本帧实例矩阵存储不足（需 {} 个，余 {} 个）", count,
				owner->impl->instance_capacity - static_cast<uint32_t>(owner->impl->pending_instances.size()));
			return false;
		}
		//追加本批模型矩阵与材质并取首个实例在本帧实例缓冲内的字节偏移
		VkDeviceSize instance_offset = instance_matrices_append(owner->impl->pending_instances, transforms, count,
			state().material_pbr_active, state().material_pbr_roughness, state().material_pbr_metallic,
			state().material_emissive, state().material_toon_active, state().material_toon_steps,
			state().material_toon_rim, state().material_toon_specular, state().material_toon_tint[0],
			state().material_toon_tint[1], state().material_toon_tint[2]);

		/*
		组装待绘制项
		描述符集与实例缓冲句柄延迟到 frame_render 时按「本帧帧槽」填装，故此处先置空。
		*/
		Frame_Render_Item item{};
		item.pipeline = owner->impl->pipeline.pipeline_get();
		item.layout = owner->impl->pipeline.layout_get();
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
		item.draw_sampler = state().bound_sampler;
		item.draw_albedo = state().bound_view;
		item.draw_normal = state().material_views[0];
		item.draw_roughness = state().material_views[1];
		item.draw_emissive = state().material_views[2];
		item.draw_ao = state().material_views[3];
		item.draw_metallic = state().material_views[4];
		//阴影投影标记（第五十二轮：文字绘制不参与投影）
		item.cast_shadow = !owner->ui().state().in_text_draw;
		//透明标记（第一百三十轮：非不透明混合项不参与合批）
		item.transparent = owner->impl->pipeline.pipeline_blend_mode_get() != Blend_Mode::Opaque;
		/*
		色调推送（第六十九轮）
		场景片元着色器恒读推送常量 offset 64 处的 tint，故实例化绘制也必须推送该块：
		吸附色调或白色（未设置时），否则未设置的推送常量取值未定义。
		*/
		owner->impl->pending_push_constants.emplace_back();
		std::array<float, 32>& push = owner->impl->pending_push_constants.back();
		push.fill(0.0f);
		push[0] = 1.0f;
		push[5] = 1.0f;
		push[10] = 1.0f;
		push[15] = 1.0f;
		for (std::size_t index = 0; index < 4; ++index)
			push[16 + index] = state().material_tint_active ? state().material_tint[index] : 1.0f;
		/*
		材质贴图参数（第一百二十四 / 一百八十四轮；offset 80 的 vec4）
		x = 法线强度、y = 粗糙度贴图启用标志、z = Alpha 裁剪阈值、w = UV 旋转角。
		实例化路径此前未写 x / y（法线映射与粗糙度贴图在实例化绘制下失效），本轮补齐。
		*/
		push[20] = state().material_normal_strength;
		push[21] = state().material_normal_strength > 0.0f ? 1.0f : 0.0f;
		push[22] = state().material_alpha_cutoff;
		push[23] = state().material_uv_rotation;
		//贴图 UV 变换（第一百七十一轮；实例化绘制同样使用吸附的材质 UV）
		push[24] = state().material_uv_scale[0];
		push[25] = state().material_uv_scale[1];
		push[26] = state().material_uv_offset[0];
		push[27] = state().material_uv_offset[1];
		//清漆层（第二百三十三轮；offset 112 的 vec4）
		push[28] = state().material_clear_coat;
		push[29] = state().material_clear_coat_roughness;
		item.push_constants = push.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		//先登记描边项（外扩轮廓须先写入深度，主体几何随后按更近深度覆盖内部）
		owner->impl->outline_item_push(item);
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		owner->impl->pending_items.push_back(item);
		return true;
	}
}
