#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
//网格数据类型（Vertex / Mesh_Data / Mesh_Section / Mesh_File_Data）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	从 OBJ 文件加载网格（第四百二十二轮；解析器 2026-11-04 起归 EngineCore 的 Mesh_Loader）
	本入口保留为薄包装（转调 `Mesh_Loader::load_file`），以保持资产加载侧的调用面稳定。
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
