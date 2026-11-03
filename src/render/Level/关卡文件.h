#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//相机 / 光照 / 雾三组（关卡直接复用，不另立平行字段）
#include "Application/Renderer/src/render/Scene/场景数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	材质实例声明（第四百六十六轮；方向七阶段 B）
	关卡文本的 `material` 行：实例名 ＋ 模板材质名 ＋ 可选 tint / emissive / toon / pbr 覆盖。
	只保存声明（Vulkan-free），不持有句柄；装配阶段由场景侧按实例名查表。
	*/
	struct Level_Material
	{
		//实例名（place 行材质列引用；同一关卡内唯一）
		std::string 实例名;
		//模板材质名（引用 `mtl` 行里的材质名）
		std::string 模板;
		//是否覆盖底色
		bool use_tint = false;
		//覆盖底色（RGBA；use_tint 为假时无意义）
		float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//是否覆盖自发光强度
		bool use_emissive = false;
		//覆盖自发光强度（use_emissive 为假时无意义）
		float emissive = 0.0f;
		/*
		逐实例卡通覆盖（第四百七十一轮；方向七延伸）
		仅 use_toon 为真时生效：色阶数 0..8（0 = 关闭）、边缘光 0..1、卡通高光阈值 0..1。
		*/
		bool use_toon = false;
		//色阶数（0..8）
		float toon_steps = 0.0f;
		//边缘光强度（0..1）
		float toon_rim = 0.0f;
		//卡通高光阈值（0..1）
		float toon_specular = 0.0f;
		/*
		逐实例 PBR 覆盖（第五百六十四轮）
		仅 use_pbr 为真时生效：覆盖即启用 PBR；粗糙度 (0.05, 4]、金属度 [0, 1]。
		*/
		bool use_pbr = false;
		//粗糙度（0.05 < 值 ≤ 4）
		float pbr_roughness = 1.0f;
		//金属度（0..1）
		float pbr_metallic = 0.0f;
		/*
		逐实例清漆覆盖（第五百九十四轮）
		仅 use_clearcoat 为真时生效：覆盖即启用清漆层（等价于模板材质执行清漆设置）；
		强度 (0, 1]、粗糙度 [0.03, 1]。
		*/
		bool use_clearcoat = false;
		//清漆强度（0 < 值 ≤ 1）
		float clear_coat = 0.0f;
		//清漆粗糙度（0.03..1）
		float clear_coat_roughness = 0.15f;
	};

	/*
	关卡数据（第四百二十九轮；方向六阶段 A）
	聚合「关卡的相机 / 光照 / 雾 / 资产摆放」；四部分都复用既有结构，
	资产段落以清单原文保存（`mesh` / `mtl` / `place` 行），装载时交给场景清单链路。
	本结构为纯数据（Vulkan-free），不持有任何 GPU 句柄。
	*/
	struct Level_Data
	{
		//关卡名（可为空；`level` 记录取行尾全部文本）
		std::string name;
		//格式版本（第四百三十六轮；当前只接受 1，缺省行按 1）
		int version = 1;
		//相机组
		Camera_Data camera;
		//光照组
		Light_Data light;
		//雾组
		Fog_Data fog;
		//资产声明行（`mesh` / `mtl` / `place`，已去首尾空白的原文）
		std::vector<std::string> asset_lines;
		//材质实例声明（第四百六十六轮；按文本出现顺序，旧文件为空）
		std::vector<Level_Material> materials;
	};

	/*
	读取关卡文本（第四百二十九轮）
	行式格式：`<段> <键> <值…>`；`#` 起首为注释，空行忽略。
	  level  <名称>
	  camera position|target|up|clear|fov|near|far|ortho|ortho_height|aspect <值…>
	  light  direction|color|intensity|ambient|specular|attenuation_mode|ambient_color|
	         ambient_ground|ambient_hemisphere|fill_direction|fill_color|fill_intensity|
	         specular_scale|shadow|spot_shadow|spot_shadow_index|spot_shadow_all|spot_shadow_bias_auto|pbr|toon|outline <值…>；point / spot 逐盏一行
	         spot_shadow 形如 <0|1> [<bias 0..0.05> <softness 0..1> <strength 0..1>]（第四百七十八轮）
	         spot_shadow_index 为 0..7 的已登记光源槽位（第四百八十一轮）
	         spot_shadow_all 为 <0|1>，非 0 时前 8 盏聚光灯各自投影（第五百零七轮起 4 盏，第五百一十三轮扩为 8 盏）
	         spot_shadow_bias_auto 为 <0|1>，非 0 时逐瓦片偏置按跨度归一化（第五百二十九轮）
	  fog    color|density|range|mode|height_falloff <值…>
	  mesh / mtl / place <清单原文>（语法同场景清单，装载阶段解析）
	  material <实例名> <模板材质名> [tint r g b a] [emissive v] [toon steps rim specular] [pbr roughness metallic] [clearcoat strength roughness]（第四百六十六 / 471 / 564 / 594 轮）
	浮点按 `%.9g` 写出（二进制 32 位浮点可唯一还原），故读写往返逐位一致。
	失败（路径不可读 / 超限 / 未知段或键 / 字段数不符 / 非有限值 / 光源超过 8 盏）时返回假并清空输出。
	本函数只做文本层校验，不校验资产文件是否存在、不建 GPU 资源。
	*/
	bool level_text_read(const std::string& path, Level_Data& out);

	/*
	写出关卡文本（第四百二十九轮）
	按固定顺序输出全部受支持字段（未使用的点光 / 聚光槽位不写出）；
	资产段落按 `asset_lines` 原样逐行写出。路径按 UTF-8 解释；失败（路径为空 / 打不开）返回假。
	第四百三十六轮起：先校验版本 ≥ 1 与全部浮点字段有限，校验失败不落盘。
	*/
	bool level_text_write(const Level_Data& level, const std::string& path);

	/*
	查找关卡内材质实例声明（第五百六十三轮）
	按实例名在 `Level_Data.materials` 中查找首个匹配项并复制到 out；
	名称为空或未找到返回假（out 保持入参原值）。纯查表，不建资源、不修改关卡。
	*/
	bool level_material_find(const Level_Data& level, const std::string& name, Level_Material& out);

	/*
	更新关卡内材质实例声明（第五百六十七轮）
	按实例名定位首个匹配并整体替换为 data（名称须与关卡内一致）；空名 / 未找到返回假且不改关卡。
	本函数不做数值校验，写出前由 `level_text_write` 统一校验。
	*/
	bool level_material_update(Level_Data& level, const std::string& name, const Level_Material& data);

	/*
	新增关卡内材质实例声明（第五百九十三轮）
	把 data 追加到 `Level_Data.materials` 末尾（保留其中的实例名）；实例名为空、已存在同名（按首个匹配）、
	或已达声明上限（256 条）时返回假且不改关卡。本函数不做数值校验，写出前由 `level_text_write` 统一校验。
	*/
	bool level_material_add(Level_Data& level, const Level_Material& data);

	/*
	删除关卡内材质实例声明（第五百九十三轮）
	按实例名删除首个匹配项并保持其余声明原有顺序；空名 / 未找到返回假且不改关卡。
	本函数只改声明表，不回溯清理 `place` 行对已删实例的引用。
	*/
	bool level_material_remove(Level_Data& level, const std::string& name);

	/*
	构造关卡对象的变换矩阵（第四百三十一轮；列主序 4×4）
	按「缩放 → 旋转 → 平移」合成（M = T·R·S）；旋转为欧拉角（**度**），顺序 X → Y → Z（R = Rz·Ry·Rx）。
	结果可直接传给 `mesh_draw` / `mesh_draw_sections` / `mesh_draw_range`；
	旋转全零时等价于「缩放 ＋ 平移」对角矩阵（与既有手工构造一致）。
	*/
	void level_transform_build(const float position[3], const float rotation_deg[3],
		const float scale[3], float out[16]);
}
