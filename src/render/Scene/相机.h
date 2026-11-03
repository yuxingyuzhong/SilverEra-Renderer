#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"

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
		/*
		设置宽高比覆盖（第一百八十六轮；0 = 跟随窗口）
		大于 0 时 view_proj_get 改用该固定宽高比（离屏渲染 / 非窗口比例输出的场景使用）；
		为 0 时回到「由交换链范围自动设置」的宽高比（既有行为不变）。
		*/
		void aspect_override_set(float aspect);
		//当前生效宽高比（覆盖优先；第一百八十六轮）
		float aspect_effective_get(void) const;

		//设置近远平面
		void near_far_set(float near_plane, float far_plane);
		//写出近远平面（第一百二十三轮新增；供景深把深度缓冲换算为世界距离）
		void clip_get(float* near_out, float* far_out) const;
		//写出视场角（弧度；第一百六十四轮）
		void fov_get(float* radians_out) const;
		/*
		写出指定视深处可见的垂直世界高度（第一百六十五轮）
		透视：``2 · tan(fov/2) · 视深``；正交：恒为 ortho_height（与视深无关）。
		视深非正时按 0 处理（不写出负值）。
		*/
		void world_height_at_get(float view_depth, float* height_out) const;
		//写出正交投影状态与垂直可见高度（第一百六十四轮）
		void ortho_get(bool* enabled_out, float* height_out) const;
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
		//写出相机世界位置（3 浮点；第三十一轮新增，供片元计算镜面高光的视线方向）
		void position_get(float out[3]) const;
		/*
		世界点 → 屏幕坐标（第一百四十九轮）
		u / v 以左上角为原点（0..1 表示视口内，允许越界），depth 为裁剪空间深度（0..1）；
		点位于相机背后（裁剪 w ≤ 0）时返回假，出参保持原值。
		*/
		bool project_to_screen(const float world[3], float* u_out, float* v_out, float* depth_out) const;
		/*
		屏幕坐标 → 世界点（第一百四十九轮）
		与 project_to_screen 互逆；视图投影矩阵不可逆时返回假，出参保持原值。
		*/
		bool unproject_from_screen(float u, float v, float depth, float world_out[3]) const;
		/*
		世界空间轴对齐包围盒是否与视锥相交（第一百六十轮）
		采用「裁剪空间六面」的保守判定：任一面的 8 角点全部在其外侧才判不可见，
		故「判可见」可能包含盒体跨在平面两侧的情形（不会误剔），「判不可见」为精确。
		min / max 为世界空间下界 / 上界（分量顺序 x / y / z）。
		*/
		bool aabb_visible(const float min[3], const float max[3]) const;
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
		//宽高比覆盖（第一百八十六轮；0 = 跟随窗口，> 0 时优先于 aspect）
		float aspect_override = 0.0f;
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
