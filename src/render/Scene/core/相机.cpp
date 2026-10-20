#include "src/render/Scene/局部命名空间使用.h"
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

	//设置近远平面
	void Camera::near_far_set(float near_plane, float far_plane)
	{
		this->near_plane = near_plane;
		this->far_plane = far_plane;
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

	//写出「视图 × 投影」矩阵（列主序 16 浮点）
	void Camera::view_proj_get(float out[16]) const
	{
		//视图矩阵
		glm::mat4 view = glm::lookAt(
			glm::vec3(position[0], position[1], position[2]),
			glm::vec3(target[0], target[1], target[2]),
			glm::vec3(up[0], up[1], up[2]));
		//透视投影（深度范围 [0,1]，K1）
		glm::mat4 proj = glm::perspective(fov, aspect, near_plane, far_plane);
		//Y 翻转：Vulkan 的 NDC Y 轴向下（K1）
		proj[1][1] *= -1.0f;
		//组合「视图 × 投影」（列主序）
		glm::mat4 view_proj = proj * view;
		//拷贝列主序数据到输出
		const float* data = glm::value_ptr(view_proj);
		for (uint32_t index = 0; index < 16; ++index)
			out[index] = data[index];
	}
}
