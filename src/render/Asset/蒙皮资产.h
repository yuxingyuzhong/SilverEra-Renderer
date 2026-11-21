#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//网格数据类型（Skinned_Mesh_Data / Mesh_Data）
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	蒙皮资产加载（方向十 · 收敛轮 868）
	OBJ 提供几何（position / uv / normal 与索引），侧车 JSON 提供逐顶点关节与权重：
		{"bone_count":2,"weights":[{"joint":[0,0,0,0],"weight":[65535,0,0,0]}, ...]}
	规则：
	  1) weights 数量须等于 OBJ 顶点数；bone_count ∈ [1, ANIMATION_ACTOR_BONES_MAX]；
	  2) 每个 joint 须小于 bone_count；每个 weight ∈ [0, 65535]；
	  3) 侧车缺失、JSON 非法或任一字段越界整体拒绝，并清空输出与骨骼数。
	本入口只产出纯数据；上传与绘制由 mesh_load_skinned / mesh_draw_skinned 负责。
	*/
	bool skinned_asset_load(const std::string& obj_path, const std::string& skin_path,
		Skinned_Mesh_Data& out, uint32_t& bone_count_out);
}
