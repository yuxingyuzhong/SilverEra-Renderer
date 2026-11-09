#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：材质
	*/
	class Renderer_Material : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		登记材质（图形管线 ＋ 纹理 ＋ 混合档）
		data.pipeline 为 0 且 data.blend 为 Opaque 时沿用默认场景管线；
		data.pipeline 为 0 且 data.blend 非 Opaque 时，以默认场景着色器对（场景.vert / 场景.frag）登记一条具名管线；
		data.pipeline 非 0 时沿用该具名管线（其 blend 已在 pipeline_load 登记时确定）。
		成功写出句柄（≥ 1）；未初始化 / 具名管线登记失败时返回假、句柄保持 0、不留半初始化记录。
		*/
		bool material_load(Material_Handle& handle_out, const Material_Data& data);
		/*
		从 MTL 材质库创建材质（第四百二十三轮；资产加载方向第二阶段）
		解析 newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump；
		换算口径：tint = Kd（第四分量为 d）、粗糙度 = √(2 / (Ns + 2))、自发光强度 = Ke 亮度、
		d < 1 时启用 alpha 裁剪（阈值 0.5）；map_Kd / map_Bump 相对 MTL 所在目录解码为纹理。
		句柄按文件内声明顺序写入 handles_out（与 OBJ 的 usemtl 顺序对齐，同名材质按名字自行匹配）。
		`名称输出`（第四百六十七轮；方向七阶段 B-B）非空时按同样顺序写出材质名，
		供关卡材质实例按「模板材质名」解析句柄。
		任一材质创建失败即整次回退（已建材质与纹理全部释放）并返回假。
		*/
		bool material_load_mtl(std::vector<Material_Handle>& handles_out, const std::string& path,
			std::vector<std::string>* names_out = nullptr);
		/*
		校验材质实例数据（第四百四十八轮；方向七阶段 A）
		规则：模板句柄须为已登记材质；`use_tint` 为真时 tint 四分量须有限且落在 [0,1]；
		`use_emissive` 为真时 emissive 须有限且落在 [0,1]。不建资源、不改变任何状态。
		*/
		bool material_instance_validate(const Material_Instance_Data& data) const;
		/*
		卸载材质（句柄无效则忽略）
		若材质自行登记了具名管线，则随卸载一并释放；卸载后其句柄失效。
		*/
		void material_unload(Material_Handle handle);
		/*
		设置后续绘制所用材质
		等价于 pipeline_bind_set(材质管线) ＋ 按材质纹理绑定（失败 / 为零回落内置默认白纹理）；
		同时按材质的 pbr_override 设置 / 清除逐物体 PBR 覆盖（第六十八轮）。
		handle 为 0 或无效句柄时回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖。
		*/
		void material_bind_set(Material_Handle handle);
		/*
		设置逐物体 PBR 材质（第六十八轮）
		设置后，后续 mesh_draw / mesh_draw_instanced 的实例属性携带该材质：
		粗糙度缩放与金属度按物体生效（全局 pbr_params 仍须启用，PBR 未启用时本覆盖不参与）。
		roughness 合法域 (0.05, 4]、metallic 合法域 [0, 1]；越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_pbr_set(float roughness, float metallic);
		/*
		清除逐物体 PBR 材质（第六十八轮）
		后续绘制回落到场景 UBO 的全局 pbr_params；未设置时为空操作。
		*/
		void material_pbr_clear(void);
		//当前是否启用逐物体 PBR 材质覆盖（第六十八轮）
		bool material_pbr_active(void) const;
		//当前逐物体材质粗糙度缩放（未启用时为 1；第六十八轮）
		float material_pbr_roughness(void) const;
		//当前逐物体材质金属度（未启用时为 0；第六十八轮）
		float material_pbr_metallic(void) const;
		/*
		设置逐物体清漆层（第二百三十九轮）
		设置后，后续 mesh_draw / mesh_draw_instanced 的推送块在 offset 112 携带该清漆参数，
		对全部直接光叠加一层独立粗糙度的 GGX 反射（材质中的同名字段优先，绑定材质时已覆盖本状态）。
		strength 合法域 (0, 1]、roughness 合法域 [0.03, 1]；越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_clear_coat_set(float strength, float roughness);
		/*
		清除逐物体清漆层（第二百三十九轮）
		后续绘制回落「清漆关闭」（强度 0、粗糙度 0.15）；未设置时为空操作。
		*/
		void material_clear_coat_clear(void);
		//当前是否启用逐物体清漆层（第二百三十九轮）
		bool material_clear_coat_active(void) const;
		//当前逐物体清漆强度（未启用时为 0；第二百三十九轮）
		float material_clear_coat_strength(void) const;
		//当前逐物体清漆粗糙度（未启用时为 0.15；第二百三十九轮）
		float material_clear_coat_roughness(void) const;
		/*
		设置逐物体反照率色调（第六十九轮）
		设置后，后续**非文字**网格绘制把反照率乘上该色调（RGBA，各分量 0..1）；
		mesh_draw 显式给出的 color 形参优先于本吸附状态。
		任一分量越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_tint_set(float red, float green, float blue, float alpha = 1.0f);
		/*
		清除逐物体反照率色调（第六十九轮）
		后续绘制回落白色（画面与未设置时一致）；未设置时为空操作。
		*/
		void material_tint_clear(void);
		//当前是否启用逐物体反照率色调（第六十九轮）
		bool material_tint_active(void) const;
		//读取当前逐物体色调（四个分量写入 out；未启用时为全 1；第六十九轮）
		void material_tint_get(float out[4]) const;
		/*
		设置逐物体自发光强度（第七十轮）
		设置后，后续绘制的实例属性携带自发光，片元着色器在受光结果上叠加 `反照率 × emissive`；
		合法域 [0, 8]，越界 / 非数整次拒绝并返回假。默认 0（无自发光，画面逐字节不变）。
		*/
		bool material_emissive_set(float emissive);
		//清除逐物体自发光（回落 0；第七十轮）
		void material_emissive_clear(void);
		//当前逐物体自发光强度（未初始化返回 0；第七十轮）
		float material_emissive(void) const;

	};
}
