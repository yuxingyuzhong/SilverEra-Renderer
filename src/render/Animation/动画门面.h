#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//片段 / 姿态 / 应用（门面只做编排，数据规则在上游头）
#include "Engine/EngineRenderer/src/render/Animation/动画片段.h"
#include "Engine/EngineRenderer/src/render/Animation/动画姿态.h"
#include "Engine/EngineRenderer/src/render/Animation/动画应用.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;

	/*
	渲染动画门面（方向十 · 收敛轮 861）
	上层入口：片段加载、演员调色板区间分配 / 回收、按姿态或按片段时间写调色板。
	本域不持有 GPU 资源：调色板缓冲与上传全部转交资源域（Render_Resource）。
	演员区间在门面内登记骨骼数，写入时校验条数，避免覆盖相邻演员。
	*/
	class Render_Animation
	{
	public:
		//构造（由 Renderer 持有；不创建任何资源）
		Render_Animation(Renderer* owner);
		//不可拷贝、不可移动（持有归属指针与登记表）
		Render_Animation(const Render_Animation&) = delete;
		Render_Animation& operator=(const Render_Animation&) = delete;

		//加载 .anim 片段（失败时清空输出）
		bool clip_load(const std::string& path, std::shared_ptr<const Animation_Clip>& clip_out) const;
		//分配演员调色板区间（bone_count ∈ (0, ANIMATION_ACTOR_BONES_MAX]）
		bool actor_create(uint32_t bone_count, uint32_t& offset_out);
		//回收演员区间（未登记偏移返回假）
		bool actor_destroy(uint32_t offset);
		//按姿态写演员调色板（骨骼数与登记一致；内部求 world × inverse_bind 后上传）
		bool actor_palette_write(uint32_t offset, const std::shared_ptr<const Animation_Skeleton>& skeleton,
			Animation_Pose& pose);
		//按片段时间写演员调色板（采样 → 姿态 → 调色板）
		bool actor_clip_write(uint32_t offset, const Animation_Clip& clip,
			const std::shared_ptr<const Animation_Skeleton>& skeleton, float time, bool loop);
		/*
		按双片段混合写演员调色板（方向十 · 收敛轮 864）
		weight ∈ [0,1]：0 取 clip_a、1 取 clip_b、中间为混合姿态；越界夹紧。
		门面只做编排与上传，混合规则在 `animation_clip_pose_blend`。
		*/
		bool actor_blend_write(uint32_t offset, const Animation_Clip& clip_a, const Animation_Clip& clip_b,
			const std::shared_ptr<const Animation_Skeleton>& skeleton, float time_a, float time_b, float weight,
			bool loop);
		/*
		按叠加层写演员调色板（方向十 · 收敛轮 872）
		base 片段决定主姿态，overlay 片段按 weight 叠加其相对绑定姿态的偏移（平移 / 缩放 / 旋转）；
		与过渡混合互斥：过渡进行中调用方应走 `actor_blend_write`。
		*/
		bool actor_layer_write(uint32_t offset, const Animation_Clip& base_clip, const Animation_Clip& overlay_clip,
			const std::shared_ptr<const Animation_Skeleton>& skeleton, float base_time, float overlay_time,
			float weight, bool loop);

	private:
		//渲染器根门面
		Renderer* owner = nullptr;
		//演员登记（偏移 → 骨骼数）
		std::unordered_map<uint32_t, uint32_t> actors;
	};
}
