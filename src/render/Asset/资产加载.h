#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//网格数据类型（Vertex / Mesh_Data / Mesh_Section）
#include "Application/Renderer/src/render/Mesh/网格数据.h"

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
}
