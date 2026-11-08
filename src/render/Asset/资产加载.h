#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//网格数据类型（Vertex / Mesh_Data / Mesh_Section / Mesh_File_Data）
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"
//MTL 材质库类型与解析器（Mtl_Material / Mtl_Loader）
#include "Engine/EngineFoundation/src/loader/assets/Mtl_Loader/材质加载器.h"

//游戏引擎命名空间
namespace engine
{
	/*
	从 OBJ 文件加载网格（第四百二十二轮；解析器 2026-11-04 起归 EngineFoundation 的 Mesh_Loader）
	本入口保留为薄包装（转调 `Mesh_Loader::load_file`），以保持资产加载侧的调用面稳定。
	*/
	bool obj_mesh_load(const std::string& path, Mesh_File_Data& out);

	/*
	从 MTL 文件读取材质库（薄包装）
	解析器本体（newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump）2026-11-07 起
	下移到 EngineFoundation 的 `Mtl_Loader::load_file`；本函数只做转调，
	「字段换算 ＋ 贴图解码 ＋ 材质创建」仍由门面 `material_load_mtl` 负责。
	*/
	bool mtl_materials_load(const std::string& path, std::vector<Mtl_Material>& out);
}
