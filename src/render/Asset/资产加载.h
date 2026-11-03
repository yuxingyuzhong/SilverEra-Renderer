#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
//网格数据类型（Vertex / Mesh_Data / Mesh_Section）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	网格文件数据（第四百二十二轮；资产加载方向第一阶段的返回结构）
	data      —— 顶点与索引（可直接交给门面 mesh_load / mesh_load_sections）
	sections  —— 按 usemtl 分组的子网格段（material 为组序号 0,1,2…）
	names     —— 段序号 → 材质名（OBJ 里的 usemtl 名称；空串表示该文件未分材质）
	*/
	struct Mesh_File_Data
	{
		//顶点与索引
		Mesh_Data data;
		//子网格分段（至少一段；未分材质时只有一段 material = 0）
		std::vector<Mesh_Section> sections;
		//段序号对应的材质名
		std::vector<std::string> names;
	};

	/*
	从 OBJ 文件加载网格（第四百二十二轮）
	支持：v / vt / vn / f（含多边形扇形三角化、负索引）、usemtl 分组；
	忽略：mtllib、o、g、s、注释与其余行。
	缺 vt 时纹理坐标填 (0,0)；缺 vn 时按相邻三角形面积加权自动生成顶点法线。
	失败（路径不可读 / 超限 / 语法非法 / 无有效三角面）时返回假，且 out 被清空。
	*/
	bool obj_mesh_load(const std::string& path, Mesh_File_Data& out);

	/*
	MTL 材质项（第四百二十三轮；资产加载方向第二阶段）
	只承载 OBJ 材质库里的原始字段，不直接构造 Material_Data——
	由门面 `material_load_mtl` 负责「字段换算 ＋ 贴图解码 ＋ 材质创建」。
	*/
	struct Mtl_Material
	{
		//材质名（与 OBJ 的 usemtl 对应）
		std::string name;
		//漫反射颜色（Kd；缺省白）
		float diffuse[3] = { 1.0f, 1.0f, 1.0f };
		//镜面颜色（Ks；缺省黑）
		float specular[3] = { 0.0f, 0.0f, 0.0f };
		//自发光颜色（Ke；缺省黑）
		float emissive[3] = { 0.0f, 0.0f, 0.0f };
		//镜面指数（Ns；0..1000，缺省 0）
		float shininess = 0.0f;
		//不透明度（d；缺省 1；Tr 按 1 − Tr 换算）
		float opacity = 1.0f;
		//漫反射贴图（map_Kd；相对 MTL 所在目录，空串表示无）
		std::string diffuse_map;
		//法线 / 凹凸贴图（map_Bump / bump / norm；同上）
		std::string normal_map;
	};

	/*
	从 MTL 文件读取材质库（第四百二十三轮）
	支持 newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump（bump / norm 视同）；
	其余行（Ka / Ni / illum / 注释等）忽略。失败（路径不可读 / 超限 / 无任何 newmtl）返回假并清空输出。
	贴图路径原样保留（不解析相对目录）；解析目录由调用方处理。
	*/
	bool mtl_materials_load(const std::string& path, std::vector<Mtl_Material>& out);
}
