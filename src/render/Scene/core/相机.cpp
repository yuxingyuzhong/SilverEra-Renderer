#include "../局部命名空间使用.h"
//glm 数学库（含矩阵变换与矩阵取址；深零到一与弧度已由 CMake 私有定义给出）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	/*
	构造
	默认机位 / 注视点 / 视场角 / 近远平面一律取自 Scene_Data 的默认值（单一真源，避免两处硬编码漂移，
	见 R-SCENE-07）；上方向与宽高比为本层自有默认（Scene_Data 未涵盖）。
	*/
	Camera::Camera(void)
	{
		//场景描述默认值（相机四项的唯一来源）
		const Scene_Data defaults{};
		//默认位置 / 注视点
		position_set(defaults.position[0], defaults.position[1], defaults.position[2]);
		target_set(defaults.target[0], defaults.target[1], defaults.target[2]);
		//默认视场角与近远平面
		fov_set(defaults.fov);
		near_far_set(defaults.near_plane, defaults.far_plane);
	}

	//设置位置
	void Camera::position_set(float x, float y, float z)
	{
		position[0] = x;
		position[1] = y;
		position[2] = z;
	}

	//设置注视点
	void Camera::target_set(float x, float y, float z)
	{
		target[0] = x;
		target[1] = y;
		target[2] = z;
	}

	//设置上方向
	void Camera::up_set(float x, float y, float z)
	{
		up[0] = x;
		up[1] = y;
		up[2] = z;
	}

	//设置视场角（弧度）
	void Camera::fov_set(float radians)
	{
		fov = radians;
	}

	//设置宽高比
	void Camera::aspect_set(float aspect)
	{
		this->aspect = aspect;
	}

	//设置宽高比覆盖（第一百八十六轮）
	void Camera::aspect_override_set(float aspect)
	{
		//非有限或非正值按「跟随窗口」处理
		aspect_override = (std::isfinite(aspect) && aspect > 0.0f) ? aspect : 0.0f;
	}

	//当前生效宽高比（覆盖优先；第一百八十六轮）
	float Camera::aspect_effective_get(void) const
	{
		return aspect_override > 0.0f ? aspect_override : aspect;
	}

	//设置近远平面
	void Camera::near_far_set(float near_plane, float far_plane)
	{
		this->near_plane = near_plane;
		this->far_plane = far_plane;
	}

	//写出近远平面（第一百二十三轮；供景深把深度缓冲换算为世界距离）
	void Camera::clip_get(float* near_out, float* far_out) const
	{
		if (near_out != nullptr)
			*near_out = near_plane;
		if (far_out != nullptr)
			*far_out = far_plane;
	}

	//绕「上方向」围绕注视点轨道旋转
	void Camera::orbit(float radians_delta)
	{
		//相对注视点的位置偏移
		glm::vec3 offset(position[0] - target[0], position[1] - target[1], position[2] - target[2]);
		//上方向（旋转轴；glm::rotate 按轴长度缩放角度，故须归一化）
		glm::vec3 up_axis(up[0], up[1], up[2]);
		//轴长度
		float up_length = glm::length(up_axis);
		//若轴长度为零则退化为默认上方向（避免除零）
		if (up_length <= 0.0f)
			up_axis = glm::vec3(0.0f, 1.0f, 0.0f);
		else
			up_axis /= up_length;
		//绕上方向旋转偏移向量
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), radians_delta, up_axis);
		glm::vec3 rotated = glm::vec3(rotation * glm::vec4(offset, 0.0f));
		//把旋转后的偏移加回注视点得到新位置
		position[0] = target[0] + rotated.x;
		position[1] = target[1] + rotated.y;
		position[2] = target[2] + rotated.z;
	}

	//设置投影模式（正交 / 透视）
	void Camera::ortho_set(bool enabled, float ortho_height)
	{
		//若垂直高度非正则忽略本次设置（保持原模式）
		if (!(ortho_height > 0.0f))
			return;
		ortho = enabled;
		this->ortho_height = ortho_height;
	}

	//写出视场角（第一百六十四轮）
	void Camera::fov_get(float* radians_out) const
	{
		//出参非空则写出
		if (radians_out != nullptr)
			*radians_out = fov;
	}

	//写出指定视深处可见的垂直世界高度（第一百六十五轮）
	void Camera::world_height_at_get(float view_depth, float* height_out) const
	{
		//出参为空则无需计算
		if (height_out == nullptr)
			return;
		//正交：与视深无关
		if (ortho)
		{
			*height_out = ortho_height;
			return;
		}
		//透视：视深非正按 0 处理
		const float depth = view_depth > 0.0f ? view_depth : 0.0f;
		*height_out = 2.0f * std::tan(fov * 0.5f) * depth;
	}

	//写出正交投影状态（第一百六十四轮）
	void Camera::ortho_get(bool* enabled_out, float* height_out) const
	{
		//出参非空则写出
		if (enabled_out != nullptr)
			*enabled_out = ortho;
		if (height_out != nullptr)
			*height_out = ortho_height;
	}

	//写出「视图 × 投影」矩阵（列主序 16 浮点）
	void Camera::view_proj_get(float out[16]) const
	{
		//视图矩阵
		glm::mat4 view = glm::lookAt(
			glm::vec3(position[0], position[1], position[2]),
			glm::vec3(target[0], target[1], target[2]),
			glm::vec3(up[0], up[1], up[2]));
		/*
		投影矩阵（深度范围 [0,1]，K1）
		正交模式：垂直可见高度 ortho_height、宽按 aspect 推出，与相机距离无关；
		透视模式：沿用 fov。
		*/
		glm::mat4 proj;
		//生效宽高比（覆盖优先；第一百八十六轮）
		const float effective_aspect = aspect_override > 0.0f ? aspect_override : aspect;
		if (ortho)
		{
			//垂直 / 水平半尺寸
			float half_height = ortho_height * 0.5f;
			float half_width = half_height * effective_aspect;
			proj = glm::ortho(-half_width, half_width, -half_height, half_height, near_plane, far_plane);
		}
		else
			proj = glm::perspective(fov, effective_aspect, near_plane, far_plane);
		//Y 翻转：Vulkan 的 NDC Y 轴向下（K1）
		proj[1][1] *= -1.0f;
		//组合「视图 × 投影」（列主序）
		glm::mat4 view_proj = proj * view;
		//拷贝列主序数据到输出
		const float* data = glm::value_ptr(view_proj);
		for (uint32_t index = 0; index < 16; ++index)
			out[index] = data[index];
	}

	//写出相机世界位置（3 浮点）
	void Camera::position_get(float out[3]) const
	{
		//逐个分量拷贝（不改动内部状态）
		for (uint32_t index = 0; index < 3; ++index)
			out[index] = position[index];
	}

	//世界点 → 屏幕坐标（第一百四十九轮）
	bool Camera::project_to_screen(const float world[3], float* u_out, float* v_out, float* depth_out) const
	{
		//世界点为空则拒绝
		if (world == nullptr)
			return false;
		//视图投影矩阵（列主序 → glm）
		float matrix[16] = {};
		view_proj_get(matrix);
		glm::mat4 view_proj = glm::make_mat4(matrix);
		//裁剪空间坐标
		glm::vec4 clip = view_proj * glm::vec4(world[0], world[1], world[2], 1.0f);
		//位于相机背后（w ≤ 0 无法透视除法）
		if (clip.w <= 0.000001f)
			return false;
		glm::vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
		//NDC → 左上原点的屏幕坐标（投影已做 Y 翻转）
		float u = ndc.x * 0.5f + 0.5f;
		float v = ndc.y * 0.5f + 0.5f;
		if (u_out != nullptr)
			*u_out = u;
		if (v_out != nullptr)
			*v_out = v;
		if (depth_out != nullptr)
			*depth_out = ndc.z;
		return true;
	}

	//屏幕坐标 → 世界点（第一百四十九轮）
	bool Camera::unproject_from_screen(float u, float v, float depth, float world_out[3]) const
	{
		//世界点出参为空则拒绝
		if (world_out == nullptr)
			return false;
		//视图投影矩阵及其逆
		float matrix[16] = {};
		view_proj_get(matrix);
		glm::mat4 view_proj = glm::make_mat4(matrix);
		glm::mat4 inverse = glm::inverse(view_proj);
		//屏幕坐标 → NDC（与 project_to_screen 的映射互逆）
		glm::vec4 ndc(u * 2.0f - 1.0f, v * 2.0f - 1.0f, depth, 1.0f);
		glm::vec4 world = inverse * ndc;
		//齐次 w 近零视为不可逆
		if (world.w < 0.000001f && world.w > -0.000001f)
			return false;
		world /= world.w;
		world_out[0] = world.x;
		world_out[1] = world.y;
		world_out[2] = world.z;
		return true;
	}

	//世界空间 AABB 是否与视锥相交（第一百六十轮）
	bool Camera::aabb_visible(const float min[3], const float max[3]) const
	{
		//入参为空或上下界颠倒则按不可见处理
		if (min == nullptr || max == nullptr)
			return false;
		//视图投影矩阵（列主序 → glm）
		float matrix[16] = {};
		view_proj_get(matrix);
		glm::mat4 view_proj = glm::make_mat4(matrix);
		/*
		逐面保守判定
		Vulkan 裁剪空间内：-w ≤ x ≤ w、-w ≤ y ≤ w、0 ≤ z ≤ w；
		某一面的 8 角点全部在外侧即整盒不可见；六面都不满足时判可见。
		*/
		bool any_inside = true;
		//六个裁剪面（1 = 左、2 = 右、3 = 下、4 = 上、5 = 近、6 = 远）
		for (int face = 1; face <= 6 && any_inside; ++face)
		{
			//该面是否对所有角点都在外侧
			bool all_outside = true;
			for (int index = 0; index < 8 && all_outside; ++index)
			{
				//角点（按 3 位二进制在 min / max 间取分量）
				glm::vec3 corner(
					(index & 1) != 0 ? max[0] : min[0],
					(index & 2) != 0 ? max[1] : min[1],
					(index & 4) != 0 ? max[2] : min[2]);
				glm::vec4 clip = view_proj * glm::vec4(corner, 1.0f);
				//按面求内测判定量（> 0 表示在该面内侧）
				float value = 0.0f;
				if (face == 1)
					value = clip.x + clip.w;
				else if (face == 2)
					value = clip.w - clip.x;
				else if (face == 3)
					value = clip.y + clip.w;
				else if (face == 4)
					value = clip.w - clip.y;
				else if (face == 5)
					value = clip.z;
				else
					value = clip.w - clip.z;
				//该角点落在内侧则本面不再是「全外」
				if (value > 0.0f)
					all_outside = false;
			}
			//任一面全外即整个盒体不可见
			if (all_outside)
				any_inside = false;
		}
		return any_inside;
	}
}