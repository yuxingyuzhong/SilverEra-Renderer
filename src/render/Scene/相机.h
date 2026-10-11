#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	/*
	相机
	生成视图矩阵与透视投影矩阵；投影按 Vulkan 约定（深度 Z ∈ [0,1]）构造并翻转 Y（K1）。
	内部用 glm 计算，但本头不出现 glm 类型。
	*/
	class Camera
	{
	public:
		//构造（默认机位在 +Z 轴 3 单位处，注视原点）
		Camera(void) = default;

		//设置位置
		void position_set(float x, float y, float z);
		//设置注视点
		void target_set(float x, float y, float z);
		//设置上方向
		void up_set(float x, float y, float z);
		//设置视场角（弧度）
		void fov_set(float radians);
		//设置宽高比
		void aspect_set(float aspect);
		//设置近远平面
		void near_far_set(float near_plane, float far_plane);

		//绕「上方向」围绕注视点轨道旋转（供演示；弧度）
		void orbit(float radians_delta);

		//写出「视图 × 投影」矩阵（列主序 16 浮点，可直接写入 UBO）
		void view_proj_get(float out[16]) const;
	private:
		//位置
		float position[3] = { 0.0f, 0.0f, 3.0f };
		//注视点
		float target[3] = { 0.0f, 0.0f, 0.0f };
		//上方向
		float up[3] = { 0.0f, 1.0f, 0.0f };
		//视场角（弧度，默认 60 度）
		float fov = 1.0472f;
		//宽高比
		float aspect = 16.0f / 9.0f;
		//近平面
		float near_plane = 0.1f;
		//远平面
		float far_plane = 100.0f;
	};
}
