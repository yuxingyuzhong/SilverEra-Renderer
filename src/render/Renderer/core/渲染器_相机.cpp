#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "渲染器内部工具.h"

namespace engine
{
	//相机：设置位置

	void Renderer_Scene::camera_position_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.position_set(x, y, z);
	}

	void Renderer_Scene::camera_target_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.target_set(x, y, z);
	}

	void Renderer_Scene::camera_fov_set(float radians)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.fov_set(radians);
	}

	void Renderer_Scene::camera_near_far_set(float near_plane, float far_plane)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.near_far_set(near_plane, far_plane);
	}

	void Renderer_Scene::camera_ortho_set(bool enabled, float ortho_height)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.ortho_set(enabled, ortho_height);
	}

	void Renderer_Scene::camera_up_set(float x, float y, float z)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//全零向量会使视图矩阵退化，整次忽略
		if (x == 0.0f && y == 0.0f && z == 0.0f)
		{
			logger.warn("渲染器:: 相机上方向全零，整次忽略");
			return;
		}
		//非有限分量按忽略处理
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
			return;
		impl->camera.up_set(x, y, z);
	}

	void Renderer_Scene::camera_aspect_set(float aspect)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->camera.aspect_override_set(aspect);
	}

	float Renderer_Scene::camera_aspect_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->camera.aspect_effective_get();
	}

	bool Renderer_Scene::camera_fov_get(float* radians_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.fov_get(radians_out);
		return true;
	}

	bool Renderer_Scene::camera_ortho_get(bool* enabled_out, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.ortho_get(enabled_out, height_out);
		return true;
	}

	bool Renderer_Scene::camera_world_height_at(float view_depth, float* height_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		impl->camera.world_height_at_get(view_depth, height_out);
		return true;
	}

	bool Renderer_Scene::camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.project_to_screen(world, u_out, v_out, depth_out);
	}

	bool Renderer_Scene::camera_unproject(float u, float v, float depth, float world_out[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->camera.unproject_from_screen(u, v, depth, world_out);
	}

	bool Renderer_Scene::camera_aabb_visible(const float min[3], const float max[3]) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//转交相机模块（六面保守判定）
		return impl->camera.aabb_visible(min, max);
	}

	bool Renderer_Scene::camera_view_proj(float out[16]) const
	{
		//未初始化或出参为空则拒绝
		if (impl == nullptr || !impl->initialized || out == nullptr)
			return false;
		//写出列主序矩阵
		impl->camera.view_proj_get(out);
		return true;
	}
}
