#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//片段 / 姿态 / 采样
#include "Engine/EngineRenderer/src/render/Animation/动画片段.h"
#include "Engine/EngineRenderer/src/render/Animation/动画姿态.h"

/*
渲染动画 · 片段应用（方向十 · 收敛轮 861）
把动画片段在指定时间采样并写入骨架姿态：
1) 姿态先复位到绑定姿态；2) 逐骨骼按「平移 / 旋转 / 缩放」通道查找轨道，缺失通道保留绑定值；
3) 按父下标升序求值世界矩阵。片段或骨架非法、时间非有限时返回假且不保证姿态内容。
本头 Vulkan-free，供门面与上层桥接共用。
*/
namespace engine
{
	//按片段在 time 处构建姿态（loop 控制回绕 / 夹紧）
	bool animation_clip_pose_build(const Animation_Clip& clip, const Animation_Skeleton& skeleton,
		float time, bool loop, Animation_Pose& pose);
}
