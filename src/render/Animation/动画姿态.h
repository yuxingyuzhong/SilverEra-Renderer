#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

/*
渲染动画 · 骨架与姿态（方向十 · 阶段 B 第 8 轮）
本头 Vulkan-free：渲染侧独立维护骨骼层级与姿态求值，不依赖 EngineSystem。
约定与 `Skeleton_System` 一致：父骨骼下标必须小于自身下标；局部变换按「先缩放、再旋转、最后平移」；
矩阵一律 4×4 列主序，姿态世界矩阵位于演员根局部空间（根变换由调用方承担）。
绑定姿态由骨骼绑定 TRS 给出；逆绑定矩阵留给蒙皮矩阵（第 9 轮）。
*/
namespace engine
{
	//骨骼（层级与绑定信息）
	struct Animation_Bone
	{
		//骨骼名
		std::string name;
		//父骨骼下标（-1 表示根）
		int32_t parent = -1;
		//绑定局部平移
		float bind_translation[3] = { 0.0f, 0.0f, 0.0f };
		//绑定局部旋转（x、y、z、w）
		float bind_rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		//绑定局部缩放
		float bind_scale[3] = { 1.0f, 1.0f, 1.0f };
		//逆绑定矩阵（列主序；第 9 轮蒙皮使用）
		float inverse_bind[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };
	};

	//骨架（父下标须小于自身下标）
	struct Animation_Skeleton
	{
		//骨骼集合
		std::vector<Animation_Bone> bones;

		//合法性（非空且父下标合法）
		bool valid(void) const;
		//骨骼数量
		uint32_t bone_count(void) const;
		//骨骼读取（越界返回空）
		const Animation_Bone* bone_get(uint32_t index) const;
		//按名查骨骼（不存在返回 -1）
		int32_t bone_seek(const std::string& name) const;
	};

	/*
	姿态（逐骨骼局部 TRS 与世界矩阵）
	translation / rotation / scale 为输入（每骨骼 3 / 4 / 3 项），world 为求值输出（每骨骼 16 项）。
	build 按骨架初始化到绑定姿态并立即求值世界矩阵；local_set 写入后需调用 world_update 重新求值。
	*/
	struct Animation_Pose
	{
		//逐骨骼局部平移
		std::vector<float> translation;
		//逐骨骼局部旋转（四元数）
		std::vector<float> rotation;
		//逐骨骼局部缩放
		std::vector<float> scale;
		//逐骨骼世界矩阵（列主序）
		std::vector<float> world;

		//按骨架初始化到绑定姿态并求值
		bool build(const Animation_Skeleton& skeleton);
		//骨骼局部变换写入（越界或参数非法返回假）
		bool local_set(uint32_t bone, const float translation[3], const float rotation[4], const float scale[3]);
		//按父下标升序求值世界矩阵（姿态尺寸与骨架不符返回假）
		bool world_update(const Animation_Skeleton& skeleton);
		//骨骼世界矩阵读取（越界返回假）
		bool world_get(uint32_t bone, float out[16]) const;
	};

	//TRS 合成（列主序；先缩放、再旋转、最后平移）
	void animation_trs_compose(const float translation[3], const float rotation[4], const float scale[3],
		float out[16]);
	//矩阵乘（列主序；out = left × right）
	void animation_matrix_multiply(const float left[16], const float right[16], float out[16]);
}
