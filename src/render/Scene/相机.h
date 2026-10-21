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
		/*
		构造
		默认机位 / 注视点 / 视场角 / 近远平面一律取自 Scene_Data 的默认值（单一真源，避免两处硬编码漂移）；
		上方向与宽高比为本层自有默认（Scene_Data 未涵盖）。
		*/
		Camera(void);

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
		/*
		设置投影模式
		enabled 为真时改用正交投影（垂直可见高度 = ortho_height 世界单位，宽按 aspect 推出）；
		为假时回到透视投影（使用 fov）。ortho_height 非正时忽略本次设置。
		*/
		void ortho_set(bool enabled, float ortho_height);

		//绕「上方向」围绕注视点轨道旋转（供演示；弧度）
		void orbit(float radians_delta);

		//写出「视图 × 投影」矩阵（列主序 16 浮点，可直接写入 UBO）
		void view_proj_get(float out[16]) const;
	private:
		//位置（默认值取自 Scene_Data，见构造）
		float position[3];
		//注视点（默认值取自 Scene_Data，见构造）
		float target[3];
		//上方向（本层自有默认）
		float up[3] = { 0.0f, 1.0f, 0.0f };
		//视场角（弧度；默认值取自 Scene_Data，见构造）
		float fov;
		//宽高比（本层自有默认）
		float aspect = 16.0f / 9.0f;
		//近平面（默认值取自 Scene_Data，见构造）
		float near_plane;
		//远平面（默认值取自 Scene_Data，见构造）
		float far_plane;
		//是否正交投影（默认透视）
		bool ortho = false;
		//正交投影的垂直可见高度（世界单位）
		float ortho_height = 2.0f;
	};
}
