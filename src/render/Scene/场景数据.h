#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

/*
场景公共语义类型（Vulkan-free）
本头与「网格数据.h」「管线数据.h」「纹理数据.h」「材质数据.h」同构：只放渲染层公共面需要暴露的引擎语义类型，
不含任何 Vulkan / VMA / SDL3 / glm 类型；由门面公共头（渲染器.h）引用。
*/
namespace engine
{
	/*
	相机组（第四十七轮拆分）
	聚合「相机位置 / 注视点 / 投影 / 清屏色」；默认值与既有默认完全一致。
	*/
	struct Camera_Data
	{
		//相机位置（世界坐标）
		float position[3] = { 0.0f, 0.0f, 3.0f };
		//相机注视点（世界坐标）
		float target[3] = { 0.0f, 0.0f, 0.0f };
		//垂直视场角（弧度；默认 60°）
		float fov = 1.0472f;
		//近裁剪平面距离
		float near_plane = 0.1f;
		//远裁剪平面距离
		float far_plane = 100.0f;
		//正交投影开关（默认关闭 = 透视）
		bool ortho = false;
		//正交投影的垂直可见高度（世界单位；仅 ortho 为真时生效）
		float ortho_height = 2.0f;
		//清屏色（RGBA，分量取值 0..1）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
	};

	/*
	光照组（第四十七轮拆分）
	聚合方向光 / 环境光 / 点光源组 / 聚光灯组 / 高光 / 逐盏衰减 / 环境光颜色；
	默认值即「满照」：方向朝 -Z、白色、强度 1、环境光 0、0 盏点 / 聚光灯。
	*/
	struct Light_Data
	{
		//光传播方向（单位向量；长度须非零，渲染层内部归一化）
		float direction[3] = { 0.0f, 0.0f, -1.0f };
		//光颜色（RGB，分量 0..1）
		float color[3] = { 1.0f, 1.0f, 1.0f };
		//光强度（≥ 0）
		float intensity = 1.0f;
		//环境光强度（0..1，直接叠加到照度）
		float ambient = 0.0f;
		//点光源数量（0..4）
		uint32_t point_count = 0;
		//点光源位置（世界坐标）
		float point_position[8][3] = {};
		//点光源颜色（RGB，0..1）
		float point_color[8][3] = {
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		};
		//点光源线性衰减半径（> 0）
		float point_radius[8] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
		//点光源强度（≥ 0）
		float point_intensity[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//点光源逐盏衰减模式（0 跟随全局 / 1 线性 / 2 平方反比；>2 按 2）
		uint32_t point_attenuation[8] = {};
		//点光源逐盏高光缩放（第四十八轮；1 = 跟随全局，0 = 该灯不产生高光；0..100）
		float point_specular[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//点光源逐盏高光光泽度（第五十一轮；0 = 跟随全局，>0 为该灯自身光泽度 1..1000）
		float point_shininess[8] = {};
		//镜面高光强度（≥ 0；0 = 关闭）
		float specular_intensity = 0.0f;
		//镜面高光光泽度（≥ 1）
		float specular_shininess = 16.0f;
		//聚光灯数量（0..8；与点光源共用 8 盏上限（第六十二轮扩为 8））
		uint32_t spot_count = 0;
		//聚光灯位置（世界坐标）
		float spot_position[8][3] = {};
		//聚光灯传播方向（从光源指向被照区域；长度须非零）
		float spot_direction[8][3] = {
			{ 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f },
		};
		//聚光灯颜色（RGB，0..1）
		float spot_color[8][3] = {
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		};
		//聚光灯线性衰减半径（> 0）
		float spot_radius[8] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
		//聚光灯强度（≥ 0）
		float spot_intensity[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//内锥角（度；须满足 0 < 内角 < 外角）
		float spot_inner_deg[8] = { 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f };
		//外锥角（度；须 ≤ 90）
		float spot_outer_deg[8] = { 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f };
		//聚光灯逐盏衰减模式（语义同 point_attenuation）
		uint32_t spot_attenuation[8] = {};
		//聚光灯逐盏高光缩放（第四十八轮；语义同 point_specular）
		float spot_specular[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//聚光灯逐盏高光光泽度（第五十一轮；语义同 point_shininess）
		float spot_shininess[8] = {};
		//全局距离衰减模式（0 = 线性、1 = 平方反比）
		uint32_t attenuation_mode = 0;
		//环境光颜色（RGB，0..1；默认白 = 与标量环境光等价）
		float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
		/*
		半球环境光（第四十七轮）
		ambient_ground 为「朝下方向的环境光颜色」（朝上方向仍取 ambient_color）；
		ambient_hemisphere 为半球混合强度（0 = 关闭，只用 ambient_color；1 = 按法线 y 分量在两组颜色间线性插值）。
		默认 0 使既有画面逐字节不变。
		*/
		//环境光地面颜色（RGB，0..1）
		float ambient_ground[3] = { 1.0f, 1.0f, 1.0f };
		//半球混合强度（0..1；0 = 关闭）
		float ambient_hemisphere = 0.0f;
		/*
		补光（第四十七轮）
		第二盏方向光，用于主光背面补光；默认强度 0 = 关闭，画面与引入前逐字节一致。
		*/
		//补光传播方向（单位向量；长度须非零）
		float fill_direction[3] = { 0.0f, 0.0f, -1.0f };
		//补光颜色（RGB，0..1）
		float fill_color[3] = { 1.0f, 1.0f, 1.0f };
		//补光强度（≥ 0；0 = 关闭）
		float fill_intensity = 0.0f;
		//方向光逐光高光缩放（第五十轮；1 = 跟随全局，0 = 不产生高光；0..100）
		float directional_specular = 1.0f;
		//补光逐光高光缩放（第五十轮；语义同 directional_specular）
		float fill_specular = 1.0f;
		//方向光阴影开关（第五十二轮；0 = 关闭、1 = 开启）与深度偏置（0..0.05）
		int shadow_enabled = 0;
		float shadow_bias = 0.003f;
		//阴影软度（第五十三轮；0 = 单点硬边、≥0.5 = 3×3 PCF；0..1）与强度（0 = 无效、1 = 全遮蔽）
		float shadow_softness = 0.0f;
		float shadow_strength = 1.0f;
		//阴影图分辨率（第五十四轮；仅 512 / 1024 / 2048，默认 1024）
		int shadow_resolution = 1024;
		//PBR-lite 镜面开关与粗糙度缩放（第六十四轮；0 = 关闭走 Blinn-Phong，缩放 0.05..4）
		int pbr_enabled = 0;
		float pbr_roughness = 1.0f;
		//PBR 金属度（第六十五轮；0 = 介电、1 = 纯金属）
		float pbr_metallic = 0.0f;
	};

	/*
	雾组（第四十七轮拆分）
	默认密度 0（关闭），画面与引入雾前一致。
	*/
	struct Fog_Data
	{
		//雾色（RGB，0..1）
		float color[3] = { 0.05f, 0.05f, 0.10f };
		//雾强度（0..1；0 = 关闭）
		float density = 0.0f;
		//雾起始距离（世界单位；≥ 0）
		float start = 1.0f;
		//雾结束距离（世界单位；须大于起始）
		float end = 10.0f;
	};

	/*
	场景聚合（Vulkan-free；兼容入口）
	保留原先的扁平字段布局，供既有调用方继续使用；新代码建议直接使用
	Camera_Data / Light_Data / Fog_Data 三组，经 scene_apply(camera, light, fog) 提交
	（与扁平 Scene_Data 版本等价）。
	*/
	struct Scene_Data
	{
		//相机位置（世界坐标）
		float position[3] = { 0.0f, 0.0f, 3.0f };
		//相机注视点（世界坐标）
		float target[3] = { 0.0f, 0.0f, 0.0f };
		//垂直视场角（弧度；默认 60°）
		float fov = 1.0472f;
		//近裁剪平面距离
		float near_plane = 0.1f;
		//远裁剪平面距离
		float far_plane = 100.0f;
		/*
		正交投影开关（默认关闭 = 透视，与既有默认一致）
		开启后「垂直可见高度」由 ortho_height 决定，与相机距离无关（近大远小消失）。
		*/
		bool ortho = false;
		//正交投影的垂直可见高度（世界单位；仅 ortho 为真时生效；须为正）
		float ortho_height = 2.0f;
		//清屏色（RGBA，分量取值 0..1）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
		/*
		方向光与环境光（第二十九轮新增）
		默认值即「满照」：方向朝 -Z、白色、强度 1、环境光 0 —— 未提供法线的网格照度为 1，
		故默认场景画面与引入光照前逐通道一致。
		*/
		//光传播方向（单位向量；长度须非零，渲染层内部归一化）
		float light_direction[3] = { 0.0f, 0.0f, -1.0f };
		//光颜色（RGB，分量 0..1）
		float light_color[3] = { 1.0f, 1.0f, 1.0f };
		//光强度（≥ 0）
		float light_intensity = 1.0f;
		//环境光（0..1，直接叠加到照度）
		float light_ambient = 0.0f;
		/*
		点光源组与镜面高光（第三十二 / 三十三轮）
		默认 0 盏点光源、高光关闭 —— 与引入点光源前画面一致。
		*/
		//点光源数量（0..4；超出 4 的部分由 scene_apply 忽略并告警）
		uint32_t light_count = 0;
		//点光源位置（世界坐标；仅前 light_count 项有效）
		float point_light_position[8][3] = {};
		//点光源颜色（RGB，0..1）
		float point_light_color[8][3] = {
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		};
		//点光源线性衰减半径（> 0）
		float point_light_radius[8] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
		//点光源强度（≥ 0）
		float point_light_intensity[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		/*
		点光源逐盏衰减模式（第四十六轮）
		0 = 跟随全局（attenuation_mode）、1 = 线性、2 = 平方反比；其它取值按 2 处理。
		*/
		uint32_t point_light_attenuation[8] = {};
		//点光源逐盏高光缩放（第四十八轮；语义见 Light_Data::point_specular）
		float point_light_specular[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//点光源逐盏高光光泽度（第五十一轮；语义见 Light_Data::point_shininess）
		float point_light_shininess[8] = {};
		//镜面高光强度（≥ 0；0 = 关闭）
		float specular_intensity = 0.0f;
		//镜面高光光泽度（≥ 1）
		float specular_shininess = 16.0f;
		/*
		聚光灯组（第三十五轮）
		与点光源组共用 8 盏上限（第六十二轮扩为 8）：`scene_apply` 先登记点光源、再登记聚光灯，超出部分忽略；
		默认 0 盏 —— 与引入聚光灯前画面一致。
		*/
		//聚光灯数量（0..4）
		uint32_t spot_light_count = 0;
		//聚光灯位置（世界坐标；仅前 spot_light_count 项有效）
		float spot_light_position[8][3] = {};
		//聚光灯传播方向（从光源指向被照区域；长度须非零）
		float spot_light_direction[8][3] = {
			{ 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, -1.0f },
		};
		//聚光灯颜色（RGB，0..1）
		float spot_light_color[8][3] = {
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		};
		//聚光灯线性衰减半径（> 0）
		float spot_light_radius[8] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
		//聚光灯强度（≥ 0）
		float spot_light_intensity[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//内锥角（度；须满足 0 < 内角 < 外角）
		float spot_light_inner_deg[8] = { 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f };
		//外锥角（度；须 ≤ 90）
		float spot_light_outer_deg[8] = { 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f };
		//聚光灯逐盏衰减模式（第四十六轮；语义同 point_light_attenuation）
		uint32_t spot_light_attenuation[8] = {};
		//聚光灯逐盏高光缩放（第四十八轮；语义同 point_light_specular）
		float spot_light_specular[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
		//聚光灯逐盏高光光泽度（第五十一轮；语义同 point_light_shininess）
		float spot_light_shininess[8] = {};
		/*
		距离衰减模式（第三十七轮）
		0 = 线性（默认）、1 = 平方反比；其它取值由 scene_apply 忽略并告警（保持门面当前模式）。
		*/
		uint32_t attenuation_mode = 0;
		/*
		距离雾（第三十九轮）
		默认密度 0（关闭），画面与引入雾前一致；开启后按「相机 → 片元」距离在 [起始, 结束] 间混入雾色。
		*/
		//雾色（RGB，0..1）
		float fog_color[3] = { 0.05f, 0.05f, 0.10f };
		//雾强度（0..1；0 = 关闭）
		float fog_density = 0.0f;
		//雾起始距离（世界单位；≥ 0）
		float fog_start = 1.0f;
		//雾结束距离（世界单位；须大于起始）
		float fog_end = 10.0f;
		//环境光颜色（RGB，0..1；第四十一轮；默认白 = 与标量环境光等价）
		float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
		//环境光地面颜色与半球混合强度（第四十七轮；语义见 Light_Data::ambient_ground / ambient_hemisphere）
		float ambient_ground[3] = { 1.0f, 1.0f, 1.0f };
		float ambient_hemisphere = 0.0f;
		//补光（第四十七轮；语义见 Light_Data::fill_direction / fill_color / fill_intensity）
		float fill_direction[3] = { 0.0f, 0.0f, -1.0f };
		float fill_color[3] = { 1.0f, 1.0f, 1.0f };
		float fill_intensity = 0.0f;
		//方向光 / 补光的逐光高光缩放（第五十轮；语义见 Light_Data::directional_specular / fill_specular）
		float directional_specular = 1.0f;
		float fill_specular = 1.0f;
		//方向光阴影开关与深度偏置（第五十二轮；语义见 Light_Data::shadow_enabled / shadow_bias）
		int shadow_enabled = 0;
		float shadow_bias = 0.003f;
		//阴影软度与强度（第五十三轮；语义见 Light_Data::shadow_softness / shadow_strength）
		float shadow_softness = 0.0f;
		float shadow_strength = 1.0f;
		//阴影图分辨率（第五十四轮；语义见 Light_Data::shadow_resolution）
		int shadow_resolution = 1024;
		//PBR-lite 镜面开关与粗糙度缩放（第六十四轮；语义见 Light_Data::pbr_enabled / pbr_roughness）
		int pbr_enabled = 0;
		float pbr_roughness = 1.0f;
		//PBR 金属度（第六十五轮；语义见 Light_Data::pbr_metallic）
		float pbr_metallic = 0.0f;
	};
}
