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
	bool Renderer_Mesh::mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks)
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
		return self().mesh_load(handle_out, Mesh_Context::sphere_generate(radius, slices, stacks));
	}

	//按正二十面体球加载网格（第一百八十九轮）
	bool Renderer_Mesh::mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions)
	{
		//前置：已初始化、半径为正（非数亦拒）、细分次数在 0..4
		if (impl == nullptr || !impl->initialized || !(radius > 0.0f) || subdivisions > 4)
		{
			logger.warn("渲染器:: 正二十面体球参数非法（半径 {}、细分 {}）", radius, subdivisions);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return self().mesh_load(handle_out, Mesh_Context::icosphere_generate(radius, subdivisions));
	}

	//按 XZ 平面细分网格加载网格（第一百九十三轮）
	bool Renderer_Mesh::mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
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
		return self().mesh_load(handle_out, Mesh_Context::ground_grid_generate(width, depth, segments_x, segments_z));
	}

	//按程序化圆柱加载网格（第六十六轮新增）
	bool Renderer_Mesh::mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
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
		return self().mesh_load(handle_out, Mesh_Context::cylinder_generate(radius, height, segments));
	}

	//按程序化平面加载网格（第七十一轮新增）
	bool Renderer_Mesh::mesh_plane_load(Mesh_Handle& handle_out, float width, float height)
	{
		//前置：已初始化、宽高为正（非数亦拒）
		if (impl == nullptr || !impl->initialized || !(width > 0.0f) || !(height > 0.0f))
		{
			logger.warn("渲染器:: 平面参数非法（宽 {}、高 {}）", width, height);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return self().mesh_load(handle_out, Mesh_Context::plane_generate(width, height));
	}

	//按程序化三角形加载网格（第一百四十二轮新增）
	bool Renderer_Mesh::mesh_triangle_load(Mesh_Handle& handle_out)
	{
		//前置：已初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.warn("渲染器:: 三角形加载失败：尚未初始化");
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return self().mesh_load(handle_out, Mesh_Context::triangle_generate());
	}

	//按程序化立方体加载网格（第一百四十二轮新增）
	bool Renderer_Mesh::mesh_cube_load(Mesh_Handle& handle_out, float half_extent)
	{
		//前置：已初始化、半边长正（非数亦拒）
		if (impl == nullptr || !impl->initialized || !(half_extent > 0.0f))
		{
			logger.warn("渲染器:: 立方体参数非法（半边长 {}）", half_extent);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return self().mesh_load(handle_out, Mesh_Context::cube_generate(half_extent));
	}

	//按程序化圆盘加载网格（第八十三轮新增）
	bool Renderer_Mesh::mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments)
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
		return self().mesh_load(handle_out, Mesh_Context::disc_generate(radius, segments));
	}

	//按细分平面加载网格（第九十三轮新增）
	bool Renderer_Mesh::mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
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
		return self().mesh_load(handle_out, Mesh_Context::grid_generate(width, height, columns, rows));
	}

	//查询网格顶点 / 索引数量（第九十三轮新增）
	bool Renderer_Mesh::mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const
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
	bool Renderer_Mesh::mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
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
		return self().mesh_load(handle_out, Mesh_Context::capsule_generate(radius, height, segments, rings));
	}

	//按程序化圆锥加载网格（第六十八轮新增）
	bool Renderer_Mesh::mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments)
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
		return self().mesh_load(handle_out, Mesh_Context::cone_generate(radius, height, segments));
	}

	//按程序化四棱锥加载网格（第二百三十二轮新增）
	bool Renderer_Mesh::mesh_pyramid_load(Mesh_Handle& handle_out, float base_half, float height)
	{
		//前置：已初始化、底面半边长与高度为正（非数亦拒）
		if (impl == nullptr || !impl->initialized || !(base_half > 0.0f) || !(height > 0.0f))
		{
			logger.warn("渲染器:: 四棱锥参数非法（底面半边长 {}、高度 {}）", base_half, height);
			handle_out = 0;
			return false;
		}
		//生成并复用既有加载路径
		return self().mesh_load(handle_out, Mesh_Context::pyramid_generate(base_half, height));
	}

	//按程序化圆环加载网格（第六十七轮新增）
	bool Renderer_Mesh::mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
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
		return self().mesh_load(handle_out, Mesh_Context::torus_generate(major_radius, minor_radius,
			major_segments, minor_segments));
	}

	bool Renderer_Mesh::mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格加载失败：尚未初始化");
			return false;
		}
		//转交网格模块创建顶点 / 索引缓冲（无分段）
		return impl->mesh.mesh_create(handle_out, data);
	}

	//加载带子网格分段的网格（第四百一十六轮）
	bool Renderer_Mesh::mesh_load_sections(Mesh_Handle& handle_out, const Mesh_Data& data,
		const Mesh_Section* sections, uint32_t count)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 网格加载失败：尚未初始化");
			return false;
		}
		//段数组非空且段数为正（否则等价于整段，走 mesh_load 语义）
		if (sections == nullptr || count == 0)
		{
			logger.warn("渲染器:: 分段网格加载参数非法（段数组为空或段数 0），已忽略");
			handle_out = 0;
			return false;
		}
		//转交网格模块（分段校验在网格模块完成）
		std::vector<Mesh_Section> 段(sections, sections + count);
		return impl->mesh.mesh_create(handle_out, data, 段);
	}

	/*
	从 OBJ 文件加载网格（第四百二十二轮）
	解析在资产加载模块完成；本函数只做「初始化检查 ＋ 转交网格模块建缓冲」。
	已解析出的分段随网格记录保存，供 mesh_draw_sections 逐段绘制。
	*/
	bool Renderer_Mesh::mesh_load_obj(Mesh_Handle& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: OBJ 网格加载失败：尚未初始化");
			return false;
		}
		//解析文件（失败时日志已由资产加载模块写出）
		Mesh_File_Data file;
		if (!obj_mesh_load(path, file))
			return false;
		//按 usemtl 分段建立网格
		return impl->mesh.mesh_create(handle_out, file.data, file.sections);
	}

	//卸载网格
	void Renderer_Mesh::mesh_unload(Mesh_Handle handle)
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
	bool Renderer_Mesh::mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const
	{
		//未初始化或出参为空则拒绝
		if (impl == nullptr || !impl->initialized || min_out == nullptr || max_out == nullptr)
			return false;
		//转交网格模块（句柄无效时返回假，出参不变）
		return impl->mesh.mesh_bounds_get(handle, min_out, max_out);
	}

	//更新网格顶点区域（第一百五十九轮）
	bool Renderer_Mesh::mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
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
	bool Renderer_Mesh::mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
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

	//绘制整个网格（内部转交区间绘制；区间 (0, 0) 表示整段，第四百一十轮）
	bool Renderer_Mesh::mesh_draw(Mesh_Handle handle, const float* transform, const float* color)
	{
		return self().mesh_draw_range(handle, transform, color, 0u, 0u);
	}

	//按索引区间绘制网格（第四百一十轮；物体多材质的分段绘制基础）
	bool Renderer_Mesh::mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
		uint32_t first_index, uint32_t range_count)
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
			impl->material_emissive, impl->material_toon_active, impl->material_toon_steps,
			impl->material_toon_rim, impl->material_toon_specular, impl->material_toon_tint[0],
			impl->material_toon_tint[1], impl->material_toon_tint[2]);

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
			std::array<float, 32>& push = impl->pending_push_constants.back();
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
			/*
			清漆层（第二百三十三轮；offset 112 的 vec4）
			x = 清漆强度、y = 清漆粗糙度；文字绘制无材质语义，恒为零（关闭）。
			*/
			if (!impl->in_text_draw)
			{
				push[28] = impl->material_clear_coat;
				push[29] = impl->material_clear_coat_roughness;
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

	//多材质分段绘制（第四百一十轮；一个物体一次请求内使用多个材质）
	bool Renderer_Mesh::mesh_draw_sections(Mesh_Handle handle, const float* transform,
		const Material_Handle* materials, uint32_t count, const Material_Instance_Data* instances)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
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
			if (data.模板 == 0 || data.模板 > impl->materials.size() || !impl->materials[data.模板 - 1].valid)
				return;
			//覆盖底色（乘性叠加到模板色调）
			if (data.use_tint)
			{
				impl->material_tint_active = true;
				for (std::size_t index = 0; index < 4; ++index)
					impl->material_tint[index] *= data.tint[index];
			}
			//覆盖自发光（直接覆盖模板值）
			if (data.use_emissive)
				impl->material_emissive = data.emissive;
			//逐实例卡通（第四百七十一轮；三项直接覆盖模板卡通参数）
			if (data.use_toon)
			{
				impl->material_toon_active = true;
				impl->material_toon_steps = data.toon_steps;
				impl->material_toon_rim = data.toon_rim;
				impl->material_toon_specular = data.toon_specular;
			}
			//逐实例 PBR（第五百六十四轮；覆盖即启用）
			if (data.use_pbr)
			{
				impl->material_pbr_active = true;
				impl->material_pbr_roughness = data.pbr_roughness;
				impl->material_pbr_metallic = data.pbr_metallic;
			}
			//逐实例清漆（第五百九十四轮；覆盖即启用）
			if (data.use_clearcoat)
			{
				impl->material_clear_coat = data.clear_coat;
				impl->material_clear_coat_roughness = data.clear_coat_roughness;
			}
		};
		//取网格索引总数（均分回落用）
		uint32_t vertex_count = 0;
		uint32_t total_indices = 0;
		if (!impl->mesh.mesh_counts_get(handle, vertex_count, total_indices))
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
		if (impl->mesh.mesh_sections_get(handle, sections) && !sections.empty())
		{
			bool success = true;
			for (const Mesh_Section& section : sections)
			{
				//材质槽越界回落首个材质
				uint32_t slot = section.material < count ? section.material : 0u;
				self().material_bind_set(materials[slot]);
				//套用该段实例覆盖（第四百六十九轮）
				if (instances != nullptr)
					apply_instance(instances[slot]);
				if (!self().mesh_draw_range(handle, transform, nullptr, section.first_index, section.index_count))
					success = false;
			}
			//恢复默认绑定
			self().material_bind_set(0);
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
			self().material_bind_set(materials[index]);
			//套用该段实例覆盖（第四百六十九轮）
			if (instances != nullptr)
				apply_instance(instances[index]);
			if (!self().mesh_draw_range(handle, transform, nullptr, first, length))
				success = false;
			first += length;
		}
		//恢复默认绑定
		self().material_bind_set(0);
		return success;
	}

	//实例化绘制（同一网格 × count 个模型矩阵，一次绘制调用提交）
	bool Renderer_Mesh::mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count)
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
		//是否具备剔除前置（变换 ＋ 剔除开启 ＋ 网格包围盒）
		const bool has_cull_input = transforms != nullptr && impl->cull_enabled
			&& impl->mesh.mesh_bounds_get(handle, bounds_min, bounds_max);
		//GPU 实例压缩优先（第二百四十九轮；失败回落 CPU 路径）
		bool gpu_compacted = false;
		if (has_cull_input && impl->gpu_cull_enabled)
		{
			std::vector<float> compacted(static_cast<std::size_t>(count) * 16, 0.0f);
			uint32_t visible_count = 0;
			//压缩成功则直接采用 GPU 结果（与 CPU 剔除顺序 / 内容一致）
			if (self().gpu_instance_compact(transforms, count, bounds_min, bounds_max, compacted.data(), &visible_count))
			{
				const uint32_t culled_count = count - visible_count;
				impl->culled_instances += culled_count;
				impl->frame_culled_instances += culled_count;
				//整批不可见按一个绘制项计
				if (visible_count == 0)
				{
					++impl->culled_items;
					++impl->frame_culled_items;
					return true;
				}
				impl->instanced_cull_scratch.assign(compacted.begin(),
					compacted.begin() + static_cast<std::ptrdiff_t>(visible_count) * 16);
				transforms = impl->instanced_cull_scratch.data();
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
			//累计本帧剔除耗时（第二百四十七轮）
			const auto cull_end = std::chrono::high_resolution_clock::now();
			impl->frame_cull_ms += std::chrono::duration<double, std::milli>(cull_end - cull_begin).count();
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
			impl->material_emissive, impl->material_toon_active, impl->material_toon_steps,
			impl->material_toon_rim, impl->material_toon_specular, impl->material_toon_tint[0],
			impl->material_toon_tint[1], impl->material_toon_tint[2]);

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
		std::array<float, 32>& push = impl->pending_push_constants.back();
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
		//清漆层（第二百三十三轮；offset 112 的 vec4）
		push[28] = impl->material_clear_coat;
		push[29] = impl->material_clear_coat_roughness;
		item.push_constants = push.data();
		item.push_constant_size = static_cast<uint32_t>(sizeof(float) * push.size());
		//先登记描边项（外扩轮廓须先写入深度，主体几何随后按更近深度覆盖内部）
		impl->outline_item_push(item);
		//登记待绘制项（同帧可累积多个；由 frame_render 消费后统一清空）
		impl->pending_items.push_back(item);
		return true;
	}

}
