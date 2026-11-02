#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//相机 / 光照 / 雾三组（关卡直接复用，不另立平行字段）
#include "Application/Renderer/src/render/Scene/场景数据.h"

//游戏引擎命名空间
namespace engine
{
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
		//相机组
		Camera_Data camera;
		//光照组
		Light_Data light;
		//雾组
		Fog_Data fog;
		//资产声明行（`mesh` / `mtl` / `place`，已去首尾空白的原文）
		std::vector<std::string> asset_lines;
	};

	/*
	读取关卡文本（第四百二十九轮）
	行式格式：`<段> <键> <值…>`；`#` 起首为注释，空行忽略。
	  level  <名称>
	  camera position|target|up|clear|fov|near|far|ortho|ortho_height|aspect <值…>
	  light  direction|color|intensity|ambient|specular|attenuation_mode|ambient_color|
	         ambient_ground|ambient_hemisphere|fill_direction|fill_color|fill_intensity|
	         specular_scale|shadow|pbr|toon|outline <值…>；point / spot 逐盏一行
	  fog    color|density|range|mode|height_falloff <值…>
	  mesh / mtl / place <清单原文>（语法同场景清单，装载阶段解析）
	浮点按 `%.9g` 写出（二进制 32 位浮点可唯一还原），故读写往返逐位一致。
	失败（路径不可读 / 超限 / 未知段或键 / 字段数不符 / 非有限值 / 光源超过 8 盏）时返回假并清空输出。
	本函数只做文本层校验，不校验资产文件是否存在、不建 GPU 资源。
	*/
	bool level_text_read(const std::string& path, Level_Data& out);

	/*
	写出关卡文本（第四百二十九轮）
	按固定顺序输出全部受支持字段（未使用的点光 / 聚光槽位不写出）；
	资产段落按 `asset_lines` 原样逐行写出。路径按 UTF-8 解释；失败（路径为空 / 打不开）返回假。
	*/
	bool level_text_write(const Level_Data& level, const std::string& path);
}
