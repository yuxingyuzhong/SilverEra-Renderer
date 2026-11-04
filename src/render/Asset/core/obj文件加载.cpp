#include "src/render/Asset/局部命名空间使用.h"

namespace engine
{
	/*
	从 OBJ 文件加载网格（薄包装）
	解析器本体（v / vt / vn / f / usemtl、负索引、扇形三角化、缺法线补齐）已下移到
	EngineCore 的 `Mesh_Loader::load_file`；本函数只做转调，保证资产加载侧调用面不变。
	*/
	bool obj_mesh_load(const std::string& path, Mesh_File_Data& out)
	{
		return Mesh_Loader::load_file(path, out);
	}
}
