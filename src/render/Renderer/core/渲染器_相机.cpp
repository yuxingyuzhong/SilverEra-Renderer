#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
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
	//构造（只创建域状态，不接触 GPU 资源）
	Render_Scene::Render_Scene(Renderer* owner, const Render_Core_Context& core_context)
		: owner(owner), core(&core_context),
		state_ptr(new(std::nothrow) Scene_State())
	{
	}

	//析构（场景域只持有主机侧状态）
	Render_Scene::~Render_Scene(void)
	{
	}

	//关闭（按原门面语义逐项复位场景域状态；幂等）
	void Render_Scene::shutdown(void)
	{
		//状态未分配则无内容可复位
		if (state_ptr == nullptr)
			return;
		//光照恢复默认（满照：朝 -Z、白色、强度 1、环境光 0）
		(*state_ptr).light_direction[0] = 0.0f;
		(*state_ptr).light_direction[1] = 0.0f;
		(*state_ptr).light_direction[2] = -1.0f;
		(*state_ptr).light_color[0] = 1.0f;
		(*state_ptr).light_color[1] = 1.0f;
		(*state_ptr).light_color[2] = 1.0f;
		(*state_ptr).light_intensity = 1.0f;
		(*state_ptr).light_ambient = 0.0f;
		//点光源与高光恢复默认
		(*state_ptr).point_light_count = 0;
		(*state_ptr).specular_intensity = 0.0f;
		(*state_ptr).specular_shininess = 16.0f;
		(*state_ptr).attenuation_mode = 0;
		//距离雾、雾模式与高度衰减恢复默认
		(*state_ptr).fog_color[0] = 0.05f;
		(*state_ptr).fog_color[1] = 0.05f;
		(*state_ptr).fog_color[2] = 0.10f;
		(*state_ptr).fog_density = 0.0f;
		(*state_ptr).fog_start = 1.0f;
		(*state_ptr).fog_end = 10.0f;
		(*state_ptr).fog_mode = 0;
		(*state_ptr).fog_height_falloff = 0.0f;
		//环境光与半球环境光恢复默认
		(*state_ptr).ambient_color[0] = 1.0f;
		(*state_ptr).ambient_color[1] = 1.0f;
		(*state_ptr).ambient_color[2] = 1.0f;
		(*state_ptr).ambient_ground[0] = 1.0f;
		(*state_ptr).ambient_ground[1] = 1.0f;
		(*state_ptr).ambient_ground[2] = 1.0f;
		(*state_ptr).ambient_ground[3] = 0.0f;
		//补光恢复默认（朝 -Z、白、强度 0 = 关闭）
		(*state_ptr).fill_direction[0] = 0.0f;
		(*state_ptr).fill_direction[1] = 0.0f;
		(*state_ptr).fill_direction[2] = -1.0f;
		(*state_ptr).fill_color[0] = 1.0f;
		(*state_ptr).fill_color[1] = 1.0f;
		(*state_ptr).fill_color[2] = 1.0f;
		(*state_ptr).fill_intensity = 0.0f;
		(*state_ptr).directional_specular = 1.0f;
		(*state_ptr).fill_specular = 1.0f;
		//二次元风格化与描边用户参数恢复默认
		(*state_ptr).toon_steps = 0;
		(*state_ptr).toon_rim = 0.0f;
		(*state_ptr).toon_specular = 0.0f;
		(*state_ptr).outline_width = 0.0f;
		for (std::size_t index = 0; index < 3; ++index)
		{
			(*state_ptr).toon_shadow_tint[index] = 1.0f;
			(*state_ptr).outline_color[index] = 0.0f;
		}
	}

	void Render_Scene::frame_stats_fill(Frame_Stats& stats) const
	{
		stats.light_truncated = (*state_ptr).light_truncated ? 1u : 0u;
		stats.lights_active = (*state_ptr).point_light_count;
		stats.lights_dropped = (*state_ptr).light_dropped;
		stats.light_select_mode = (*state_ptr).light_select_mode;
		stats.shadow_atlas_bytes = stats.shadow_atlas
			? static_cast<uint64_t>((*state_ptr).shadow_size) * 8u * (*state_ptr).shadow_size * 4u : 0u;
		stats.shadow_pcss = (*state_ptr).shadow_pcss_enabled ? 1u : 0u;
	}

	//相机：设置位置

	void Render_Scene::camera_position_set(float x, float y, float z)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.position_set(x, y, z);
	}

	void Render_Scene::camera_target_set(float x, float y, float z)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.target_set(x, y, z);
	}

	void Render_Scene::camera_fov_set(float radians)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.fov_set(radians);
	}

	void Render_Scene::camera_near_far_set(float near_plane, float far_plane)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.near_far_set(near_plane, far_plane);
	}

	void Render_Scene::camera_ortho_set(bool enabled, float ortho_height)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.ortho_set(enabled, ortho_height);
	}

	void Render_Scene::camera_up_set(float x, float y, float z)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
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
		(*state_ptr).camera.up_set(x, y, z);
	}

	void Render_Scene::camera_aspect_set(float aspect)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		(*state_ptr).camera.aspect_override_set(aspect);
	}

	void Render_Scene::camera_aspect_refresh(float aspect)
	{
		//初始化服务与交换链重建均可在设备 initialized 置位前调用
		if (state_ptr == nullptr || core == nullptr)
			return;
		(*state_ptr).camera.aspect_set(aspect);
	}

	void Render_Scene::camera_position_get(float out[3]) const
	{
		//未初始化或出参为空时写零
		if (out == nullptr)
			return;
		if (owner == nullptr || !owner->device().valid())
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			return;
		}
		(*state_ptr).camera.position_get(out);
	}

	float Render_Scene::camera_aspect_get(void) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return 0.0f;
		return (*state_ptr).camera.aspect_effective_get();
	}

	bool Render_Scene::camera_fov_get(float* radians_out) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		(*state_ptr).camera.fov_get(radians_out);
		return true;
	}

	bool Render_Scene::camera_ortho_get(bool* enabled_out, float* height_out) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		(*state_ptr).camera.ortho_get(enabled_out, height_out);
		return true;
	}

	bool Render_Scene::camera_world_height_at(float view_depth, float* height_out) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		(*state_ptr).camera.world_height_at_get(view_depth, height_out);
		return true;
	}

	bool Render_Scene::camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		return (*state_ptr).camera.project_to_screen(world, u_out, v_out, depth_out);
	}

	bool Render_Scene::camera_unproject(float u, float v, float depth, float world_out[3]) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		return (*state_ptr).camera.unproject_from_screen(u, v, depth, world_out);
	}

	bool Render_Scene::camera_aabb_visible(const float min[3], const float max[3]) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//转交相机模块（六面保守判定）
		return (*state_ptr).camera.aabb_visible(min, max);
	}

	bool Render_Scene::camera_view_proj(float out[16]) const
	{
		//未初始化或出参为空则拒绝
		if (owner == nullptr || !owner->device().valid() || out == nullptr)
			return false;
		//写出列主序矩阵
		(*state_ptr).camera.view_proj_get(out);
		return true;
	}

	//导出相机近远平面
	bool Render_Scene::camera_near_far_get(float* near_out, float* far_out) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		(*state_ptr).camera.clip_get(near_out, far_out);
		return true;
	}
}
