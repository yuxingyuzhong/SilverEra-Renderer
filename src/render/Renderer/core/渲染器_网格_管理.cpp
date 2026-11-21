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

	bool Render_Resource::mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const
	{
		//出参先复位
		if (vertex_count_out != nullptr)
			*vertex_count_out = 0;
		if (index_count_out != nullptr)
			*index_count_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//转交网格模块
		uint32_t vertex_count = 0;
		uint32_t index_count = 0;
		if (!(*state_ptr).mesh.mesh_counts_get(handle, vertex_count, index_count))
			return false;
		if (vertex_count_out != nullptr)
			*vertex_count_out = vertex_count;
		if (index_count_out != nullptr)
			*index_count_out = index_count;
		return true;
	}

	bool Render_Resource::mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 网格加载失败：尚未初始化");
			return false;
		}
		//转交网格模块创建顶点 / 索引缓冲（无分段）
		return (*state_ptr).mesh.mesh_create(handle_out, data);
	}

	//创建蒙皮网格
	bool Render_Resource::mesh_load_skinned(Mesh_Handle& handle_out, const Skinned_Mesh_Data& data,
		uint32_t bone_count)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: 蒙皮网格加载失败：尚未初始化");
			return false;
		}
		//转交网格模块（48 字节步长；骨骼数用于关节下标校验）
		return (*state_ptr).mesh.mesh_create_skinned(handle_out, data, {}, bone_count);
	}

	bool Render_Resource::mesh_load_sections(Mesh_Handle& handle_out, const Mesh_Data& data,
		const Mesh_Section* sections, uint32_t count)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
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
		return (*state_ptr).mesh.mesh_create(handle_out, data, 段);
	}

	bool Render_Resource::mesh_load_obj(Mesh_Handle& handle_out, const std::string& path)
	{
		//默认无有效句柄
		handle_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
		{
			logger.error("渲染器:: OBJ 网格加载失败：尚未初始化");
			return false;
		}
		//解析文件（失败时日志已由资产加载模块写出）
		Mesh_File_Data file;
		if (!obj_mesh_load(path, file))
			return false;
		//按 usemtl 分段建立网格
		return (*state_ptr).mesh.mesh_create(handle_out, file.data, file.sections);
	}

	void Render_Resource::mesh_unload(Mesh_Handle handle)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//摘出网格缓冲（不立即销毁，避免销毁仍被在途命令缓冲引用的缓冲）
		Mesh_Record record{};
		if (!(*state_ptr).mesh.mesh_detach(handle, record))
			return;
		//组装延迟条目（缓冲经内存模块归还）
		Deferred_Entry entry{};
		Deferred_Memory_Resource resource{};
		resource.buffer = record.buffer;
		resource.buffer_allocation = record.allocation;
		entry.memory_resources.push_back(resource);
		//压入帧延迟队列（延迟 = 在途帧数 + 1，保证在途帧用尽后才释放）
		core->frame.deferred_push(std::move(entry), core->frame.in_flight_count_get() + 1);
	}

	bool Render_Resource::mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const
	{
		//未初始化或出参为空则拒绝
		if (owner == nullptr || !owner->device().valid() || min_out == nullptr || max_out == nullptr)
			return false;
		//转交网格模块（句柄无效时返回假，出参不变）
		return (*state_ptr).mesh.mesh_bounds_get(handle, min_out, max_out);
	}

	bool Render_Resource::mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
		const Vertex* vertices)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽（避免改写仍被绘制命令读取的顶点缓冲）
		if (!core->frame.frames_wait())
			return false;
		//转交网格模块（顶点区覆盖写入并保守扩张包围盒）
		return (*state_ptr).mesh.mesh_update_vertices(handle, first_vertex, count, vertices);
	}

	bool Render_Resource::mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
		const uint32_t* indices)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//先把在途帧等尽（避免改写仍被绘制命令读取的索引缓冲）
		if (!core->frame.frames_wait())
			return false;
		//转交网格模块（索引区覆盖写入并校验取值）
		return (*state_ptr).mesh.mesh_update_indices(handle, first_index, count, indices);
	}
}
