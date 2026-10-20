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
		//清屏色（RGBA，分量取值 0..1）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
	};
}
