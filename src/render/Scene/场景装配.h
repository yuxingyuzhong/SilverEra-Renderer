#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//渲染器门面（Vulkan-free 公共面：资源创建 / 纯数据场景提交 / 绘制登记）
#include "Engine/EngineRenderer/src/render/Renderer/渲染器.h"
//系统层关卡数据（中立数据契约：相机 / 光照 / 雾 ＋ 资产段落 ＋ 材质实例声明）
#include "Engine/EngineSystem/src/scene/Level/关卡文本.h"
//系统层场景清单解析（中立数据契约：网格 / 材质库 / 摆放）
#include "Engine/EngineSystem/src/scene/Manifest/场景清单.h"

/*
场景装配（渲染层；2026-11-07 自 EngineSystem 归位）
依赖方向：渲染层单向消费系统层产出的中立描述（`Level_Data` / `Scene_Manifest` / `Level_Material`），
由本模块完成「资源创建 → 摆放表装配 → 绘制登记」——
渲染层只保留纯数据消费接口（scene_apply）与资源门面（mesh_load_obj / material_load_mtl /
material_instance_validate / mesh_draw_sections），资源生命周期与装配顺序归本层。
本模块的装配顺序与失败回退口径与旧 `Renderer::level_load` / `scene_load` / `scene_draw` 完全一致。
*/
namespace engine
{
	/*
	装载关卡（渲染层消费系统层描述）
	顺序：读关卡文本 → 校验相机 → 装配资产（失败整体回退）→ 应用相机 / 光照 / 雾。
	先装配后应用，保证「装载失败时既有场景状态不被改动」。
	apply_scene 为假时只装配资产（供展示层切换关卡时不动全局相机 / 光照 / 雾）。
	失败时 `level_out` 与 `scene_out` 均被清空。
	*/
	bool level_bridge_load(Renderer& renderer, Level_Data& level_out, Asset_Scene& scene_out,
		const std::string& path, bool apply_scene = true);

	//释放装配创建的全部资源（幂等）
	void level_bridge_unload(Renderer& renderer, Asset_Scene& scene);

	/*
	装载场景清单文件（渲染层消费系统层描述）
	解析清单文本 → 按清单路径解析资产目录 → 创建资源并装配摆放表。
	任一资源创建失败整体回退；失败时 `scene_out` 清空。
	*/
	bool scene_bundle_load(Renderer& renderer, Asset_Scene& scene_out, const std::string& path);

	//逐对象构造变换矩阵并逐段绑材质（返回成功登记的对象数）
	uint32_t level_bridge_draw(Renderer& renderer, const Asset_Scene& scene);
}
