//获取动画门面
#include "../动画门面.h"
//调色板容量与矩阵步长
#include "src/render/Animation/动画数据.h"
//获取渲染器根门面（调色板接口转交资源域）
#include "src/render/Renderer/渲染器.h"

namespace engine
{
	//构造
	Render_Animation::Render_Animation(Renderer* owner)
		: owner(owner)
	{
	}

	//加载片段
	bool Render_Animation::clip_load(const std::string& path,
		std::shared_ptr<const Animation_Clip>& clip_out) const
	{
		clip_out.reset();
		auto clip = std::make_shared<Animation_Clip>();
		if (!animation_clip_load_file(path, *clip))
			return false;
		clip_out = clip;
		return true;
	}

	//分配演员区间
	bool Render_Animation::actor_create(uint32_t bone_count, uint32_t& offset_out)
	{
		offset_out = 0;
		if (owner == nullptr)
			return false;
		uint32_t offset = 0;
		if (!owner->resource().animation_palette_allocate(bone_count, offset))
			return false;
		actors[offset] = bone_count;
		offset_out = offset;
		return true;
	}

	//回收演员区间
	bool Render_Animation::actor_destroy(uint32_t offset)
	{
		if (owner == nullptr)
			return false;
		auto it = actors.find(offset);
		if (it == actors.end())
			return false;
		if (!owner->resource().animation_palette_release(offset))
			return false;
		actors.erase(it);
		return true;
	}

	//按姿态写演员调色板
	bool Render_Animation::actor_palette_write(uint32_t offset, const Animation_Skeleton& skeleton,
		const Animation_Pose& pose)
	{
		if (owner == nullptr)
			return false;
		auto it = actors.find(offset);
		if (it == actors.end())
			return false;
		const uint32_t bone_count = skeleton.bone_count();
		//骨骼数须与登记一致
		if (bone_count == 0 || bone_count != it->second)
			return false;
		//求 world × inverse_bind
		std::vector<float> palette(
			static_cast<std::size_t>(bone_count) * ANIMATION_PALETTE_MATRIX_FLOATS, 0.0f);
		if (!animation_skin_palette_build(skeleton, pose, palette.data(), bone_count))
			return false;
		//上传到该演员区间
		return owner->resource().animation_palette_upload(palette.data(), bone_count, offset);
	}

	//按片段时间写演员调色板
	bool Render_Animation::actor_clip_write(uint32_t offset, const Animation_Clip& clip,
		const Animation_Skeleton& skeleton, float time, bool loop)
	{
		if (owner == nullptr)
			return false;
		if (actors.find(offset) == actors.end())
			return false;
		Animation_Pose pose;
		if (!animation_clip_pose_build(clip, skeleton, time, loop, pose))
			return false;
		return actor_palette_write(offset, skeleton, pose);
	}
}
