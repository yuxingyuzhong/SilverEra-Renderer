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
	场景描述（Vulkan-free）
	聚合「相机参数 ＋ 清屏色」，供上层以数据驱动方式提交场景状态（替代散装的相机 setter 调用）。
	默认值与本层既有默认一致（相机在 +Z 轴 3 单位处注视原点、视场角 60°、近远 0.1 / 100）。
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
		float point_light_position[4][3] = {};
		//点光源颜色（RGB，0..1）
		float point_light_color[4][3] = {
			{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
		};
		//点光源线性衰减半径（> 0）
		float point_light_radius[4] = { 5.0f, 5.0f, 5.0f, 5.0f };
		//点光源强度（≥ 0）
		float point_light_intensity[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//镜面高光强度（≥ 0；0 = 关闭）
		float specular_intensity = 0.0f;
		//镜面高光光泽度（≥ 1）
		float specular_shininess = 16.0f;
	};
}
