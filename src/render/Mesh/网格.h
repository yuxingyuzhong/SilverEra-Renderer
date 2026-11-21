#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 与 VMA 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露这两者的类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
//网格数据（Vertex / Mesh_Data / Mesh_Handle 共用型，Vulkan-free）
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	//内存上下文（前置声明；仅作引用入参，其定义由实现文件包含）
	class Memory_Context;

	/*
	网格记录
	顶点与索引共用一个设备本地缓冲：顶点区自 0 起，索引区自 index_offset 起（= 顶点区字节数）；
	如此一次分配、一次上传即可同时落位顶点与索引，减少 VMA 分配与 GPU 同步次数。
	*/
	struct Mesh_Record
	{
		//顶点 / 索引共用缓冲
		VkBuffer buffer = VK_NULL_HANDLE;
		//缓冲内存
		VmaAllocation allocation = VK_NULL_HANDLE;
		//索引区在缓冲内的字节偏移（= 顶点区字节数；对齐满足 uint32 索引要求）
		VkDeviceSize index_offset = 0;
		//索引数
		uint32_t index_count = 0;
		//顶点数（第九十三轮新增；只读查询用）
		uint32_t vertex_count = 0;
		//顶点步长（收敛轮 859：静态 32 / 蒙皮 48；绘制管线布局据此对齐）
		uint32_t vertex_stride = sizeof(Vertex);
		//蒙皮骨骼数上限（收敛轮 867：0 = 未声明，跳过关节越界校验）
		uint32_t skinned_bones = 0;
		/*
		局部包围盒（创建时按顶点位置一次算出；第二十二轮新增）
		供门面做实例级视锥剔除；空网格（不应出现）时取全零，等价于「保守保留」。
		*/
		float bounds_min[3] = { 0.0f, 0.0f, 0.0f };
		float bounds_max[3] = { 0.0f, 0.0f, 0.0f };
		//分段描述（第四百一十四轮；空表示整段）
		std::vector<Mesh_Section> sections;
		//记录是否有效
		bool valid = false;
	};

	/*
	网格上下文
	持有网格记录集合，负责顶点 / 索引缓冲的创建设备本地缓冲与上传、销毁。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	句柄自 1 起（0 表无效），下标 = 句柄 - 1。
	*/
	class Mesh_Context
	{
	public:
		//构造（不创建任何网格）
		Mesh_Context(void) = default;
		//析构（内部调用 destroy，保证网格不泄漏）
		~Mesh_Context(void);
		//不可拷贝、不可移动（持有 VMA 句柄）
		Mesh_Context(const Mesh_Context&) = delete;
		Mesh_Context& operator=(const Mesh_Context&) = delete;

		//构建（记录内存模块；不创建任何网格）
		bool build(Memory_Context& memory);
		//销毁（幂等；销毁全部网格缓冲）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		//创建网格（顶点 / 索引合入一个设备本地缓冲后上传；句柄自 1 起，0 表无效）
		//sections 为可选分段描述（第四百一十六轮；空表示整段），随记录保存供多材质绘制入口使用
		bool mesh_create(Mesh_Handle& handle_out, const Mesh_Data& data,
			const std::vector<Mesh_Section>& sections = {});
		/*
		创建蒙皮网格（方向十 · 收敛轮 859；收敛轮 867 增加骨骼数声明）
		顶点按 48 字节步长上传（Skinned_Vertex），索引与分段规则同 mesh_create；
		记录 vertex_stride = 48，供绘制入口选择蒙皮管线。
		bone_count > 0 时校验每个顶点的关节下标（须小于骨骼数），越界整次拒绝；
		bone_count = 0 表示未声明，跳过关节校验（兼容旧数据与无骨骼信息的资产）。
		*/
		bool mesh_create_skinned(Mesh_Handle& handle_out, const Skinned_Mesh_Data& data,
			const std::vector<Mesh_Section>& sections = {}, uint32_t bone_count = 0);
		//销毁网格
		void mesh_destroy(Mesh_Handle handle);
		/*
		分离网格资源（摘出记录但**不销毁**，归还责任移交调用方）
		供门面在运行时卸载路径把缓冲压入帧延迟销毁队列；
		成功返回真并写出被摘出的记录；句柄无效或记录已空时返回假（不写出任何资源）。
		*/
		bool mesh_detach(Mesh_Handle handle, Mesh_Record& record_out);
		//取网格缓冲、索引区偏移与索引数（句柄无效返回假）//当前有效网格数量（第一百一十三轮；只读诊断）
		uint32_t mesh_count(void) const;
		//句柄是否有效（实现见 core/网格缓冲.cpp）
		bool mesh_record_get(Mesh_Handle handle, VkBuffer& buffer_out, VkDeviceSize& index_offset_out, uint32_t& index_count_out) const;
		//取网格顶点步长（收敛轮 859；句柄无效返回假且出参归零）
		bool mesh_vertex_stride_get(Mesh_Handle handle, uint32_t& stride_out) const;
		//取蒙皮网格骨骼数声明（收敛轮 867；句柄无效返回假且出参归零；0 = 未声明）
		bool mesh_skinned_bone_count_get(Mesh_Handle handle, uint32_t& bone_count_out) const;
		//取网格顶点 / 索引数量（第九十三轮新增；句柄无效返回假且出参归零）
		bool mesh_counts_get(Mesh_Handle handle, uint32_t& vertex_count_out, uint32_t& index_count_out) const;
		//取网格分段描述（第四百一十四轮；句柄无效返回假并清空出参；无分段时返回真且出参为空）
		bool mesh_sections_get(Mesh_Handle handle, std::vector<Mesh_Section>& sections_out) const;
		//取网格局部包围盒（句柄无效返回假；第二十二轮新增）
		bool mesh_bounds_get(Mesh_Handle handle, float min_out[3], float max_out[3]) const;
		/*
		更新网格顶点区域（第一百五十九轮新增）
		first_vertex 起、count 个顶点以 vertices 覆盖（结构须与创建时一致）；索引区不动。
		局部包围盒按更新后的顶点做**保守扩张**（只扩不缩），保证已有剔除结果不会误剔。
		句柄无效 / 记录已空 / 数据为空 / 区间越界返回假。
		*/
		bool mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
			const Vertex* vertices);
		/*
		更新网格索引区域（第一百六十轮新增）
		first_index 起、count 个索引以 indices 覆盖（写入共用缓冲的索引区）；
		每个索引须小于该网格顶点数；句柄无效 / 数据为空 / 区间越界 / 索引越界返回假。
		*/
		bool mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
			const uint32_t* indices);

		//生成三角形（顶点 3 / 索引 3）
		static Mesh_Data triangle_generate(void);
		//生成轴对齐立方体（边长 2*half_extent；24 顶点 / 36 索引）
		static Mesh_Data cube_generate(float half_extent);
		/*
		生成 UV 球体（第六十六轮新增）
		radius 半径；slices 经度分段（>= 3）、stacks 纬度分段（>= 2）；
		顶点 (stacks+1) × (slices+1)（末列与首列重合以保留 UV 接缝），法线 = 归一化位置。
		*/
		static Mesh_Data sphere_generate(float radius, uint32_t slices, uint32_t stacks);
		/*
		生成正二十面体球（第一百八十九轮）
		radius 半径；subdivisions 细分次数（0..4，0 = 正二十面体：12 顶点 / 20 面）；
		每细分一次面数 ×4；顶点按球面归一化，法线 = 归一化位置，UV 按球坐标展开。
		*/
		static Mesh_Data icosphere_generate(float radius, uint32_t subdivisions);
		/*
		生成 XZ 平面细分网格（第一百九十三轮）
		width / depth 为世界单位（沿 X / Z、中心在原点）；segments_x / segments_z 分段数（1..512）；
		顶点 (segments_x+1) × (segments_z+1)、索引 segments_x × segments_z × 6；
		法线恒 +Y、UV 在 [0,1] 上线性分布；正面绕序为「从 +Y 看逆时针」（与其它程序化图元一致）。
		*/
		static Mesh_Data ground_grid_generate(float width, float depth, uint32_t segments_x, uint32_t segments_z);
		/*
		生成带端盖圆柱（第六十六轮新增）
		radius 半径、height 总高（沿 Y 轴、中心在原点）；segments 圆周分段（>= 3）；
		侧面法线径向，上下端盖法线朝 ±Y。
		*/
		static Mesh_Data cylinder_generate(float radius, float height, uint32_t segments);
		/*
		生成圆锥（第六十八轮新增）
		radius 底面半径、height 总高（沿 Y 轴、中心在原点，锥尖朝 +Y）；segments 圆周分段（>= 3）；
		侧面法线按锥面斜率倾斜，底面端盖法线朝 -Y。
		*/
		static Mesh_Data cone_generate(float radius, float height, uint32_t segments);
		/*
		生成四棱锥（第二百三十二轮新增）
		base_half 底面半边长（底面为 XZ 平面上的正方形）、height 总高（沿 Y 轴、中心在原点，锥尖朝 +Y）；
		四个侧面与底面均为独立顶点，法线为各自面法线（硬边），故无平滑明暗过渡。
		*/
		static Mesh_Data pyramid_generate(float base_half, float height);
		/*
		生成胶囊（第六十九轮新增）
		radius 半径、height 中段圆柱高度（可为 0 → 退化为球）；segments 圆周分段（>= 3）、rings 单侧半球纬圈（>= 2）；
		总高 = height + 2 × radius，沿 Y 轴、中心在原点；法线朝外，接缝 UV，外向绕序。
		*/
		static Mesh_Data capsule_generate(float radius, float height, uint32_t segments, uint32_t rings);
		/*
		生成平面（第七十一轮新增）
		width / height 为平面尺寸（沿 X / Y，中心在原点，法线朝 +Z）；UV 覆盖整面。
		*/
		static Mesh_Data plane_generate(float width, float height);
		/*
		生成圆盘（第八十三轮新增）
		radius 半径、segments 圆周分段（>= 3）；XY 平面上的实心圆，中心在原点、法线朝 +Z；
		中心顶点 ＋ 圆周顶点（末点与首点重合以保留 UV 接缝），三角扇索引。
		*/
		static Mesh_Data disc_generate(float radius, uint32_t segments);
		/*
		生成细分平面（第九十三轮新增）
		width / height 为尺寸（X / Y，中心在原点、法线 +Z）；columns / rows 为网格分段（>= 1）；
		顶点 (columns+1) × (rows+1)，索引 columns × rows × 6，UV 覆盖整面。
		*/
		static Mesh_Data grid_generate(float width, float height, uint32_t columns, uint32_t rows);
		/*
		生成圆环（第六十七轮新增）
		major_radius 环半径、minor_radius 管半径；major_segments / minor_segments 双分段（>= 3）；
		顶点 (major+1) × (minor+1)（接缝复制），法线朝外，UV 覆盖整面。
		*/
		static Mesh_Data torus_generate(float major_radius, float minor_radius,
			uint32_t major_segments, uint32_t minor_segments);
	private:
		//内存模块（缓冲创建与上传在此完成）
		Memory_Context* memory = nullptr;
		//网格记录集合（下标 = 句柄 - 1）
		std::vector<Mesh_Record> records;
	};
}
