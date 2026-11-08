#include "../局部命名空间使用.h"

namespace engine
{
	/*
	从 MTL 文件读取材质库（薄包装）
	解析器本体（newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump）已下移到
	EngineFoundation 的 `Mtl_Loader::load_file`；本函数只做转调，保证资产加载侧调用面不变。
	*/
	bool mtl_materials_load(const std::string& path, std::vector<Mtl_Material>& out)
	{
		return Mtl_Loader::load_file(path, out);
	}
}