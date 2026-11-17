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
	//设置距离雾（第三十八轮新增）

	void Render_Scene::fog_set(const float color[3], float density, float start, float end)
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始非负、结束大于起始；
		任一非法即整次忽略并告警（保持既有雾参数）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f) && (end > start) && (end <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 雾参数非法（密度 {}、起始 {}、结束 {}），整次忽略", density, start, end);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).fog_color[index] = color[index];
		(*state_ptr).fog_density = density;
		(*state_ptr).fog_start = start;
		(*state_ptr).fog_end = end;
		//切回线性区间模式（第一百四十三轮）
		(*state_ptr).fog_mode = 0;
	}

	void Render_Scene::fog_exp2_set(const float color[3], float density, float start)
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || color == nullptr)
			return;
		/*
		参数校验（取反比较使 NaN 判非法）
		颜色各分量须落在 [0,1]，密度须在 [0,1]、起始须非负且有限；
		任一非法即整次忽略并告警（保持既有雾参数与模式）。
		*/
		bool fog_valid = (density >= 0.0f) && (density <= 1.0f)
			&& (start >= 0.0f) && (start <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fog_valid = false;
		//若雾参数非法
		if (!fog_valid)
		{
			logger.warn("渲染器:: 指数平方雾参数非法（密度 {}、起始 {}），整次忽略", density, start);
			return;
		}
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).fog_color[index] = color[index];
		(*state_ptr).fog_density = density;
		(*state_ptr).fog_start = start;
		//切到指数平方模式
		(*state_ptr).fog_mode = 1;
	}

	uint32_t Render_Scene::fog_mode_get(void) const
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return 0;
		return (*state_ptr).fog_mode;
	}

	void Render_Scene::fog_height_set(float falloff)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//系数须在 [0, 10]（取反比较使 NaN 判非法）
		if (!(falloff >= 0.0f && falloff <= 10.0f))
		{
			logger.warn("渲染器:: 雾高度衰减系数非法（{}），整次忽略", falloff);
			return;
		}
		(*state_ptr).fog_height_falloff = falloff;
	}

	void Render_Scene::fog_clear(void)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//密度归零即关闭（颜色与区间保持既有值，便于再次开启）
		(*state_ptr).fog_density = 0.0f;
	}

	void Render_Scene::ambient_color_set(const float color[3])
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 环境光颜色非法（{}），整次忽略", color[index]);
				return;
			}
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).ambient_color[index] = color[index];
	}

	void Render_Scene::ambient_ground_set(const float ground_color[3], float hemisphere)
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || ground_color == nullptr)
			return;
		//颜色各分量须落在 [0,1]（取反比较使 NaN 判非法）
		for (std::size_t index = 0; index < 3; ++index)
			//分量越界或非数即整次忽略
			if (!(ground_color[index] >= 0.0f && ground_color[index] <= 1.0f))
			{
				logger.warn("渲染器:: 半球环境光颜色非法（{}），整次忽略", ground_color[index]);
				return;
			}
		//强度按 0..1 钳制（非数按 0 = 关闭处理）
		float blend = 0.0f;
		//仅有限值参与混合
		if (hemisphere >= 0.0f && hemisphere <= 1.0f)
			blend = hemisphere;
		else if (hemisphere > 1.0f)
			blend = 1.0f;
		//写入地面颜色与混合强度
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).ambient_ground[index] = ground_color[index];
		(*state_ptr).ambient_ground[3] = blend;
	}

	void Render_Scene::environment_set(const float color[3], float intensity)
	{
		//若尚未初始化或颜色指针为空
		if (owner == nullptr || !owner->device().valid() || color == nullptr)
			return;
		//颜色各分量须落在 [0,1]、强度须非负有限（取反比较使 NaN 判非法）
		bool valid = (intensity >= 0.0f) && (intensity <= 1e6f);
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				valid = false;
		//若环境参数非法
		if (!valid)
		{
			logger.warn("渲染器:: 环境光照参数非法（强度 {}），整次忽略", intensity);
			return;
		}
		//写入环境颜色与强度
		for (std::size_t index = 0; index < 3; ++index)
			(*state_ptr).environment_color[index] = color[index];
		(*state_ptr).environment_intensity = intensity;
	}

	void Render_Scene::environment_get(float color[3], float* intensity) const
	{
		//未初始化时写 0
		if (state_ptr == nullptr)
		{
			if (color != nullptr)
				color[0] = color[1] = color[2] = 0.0f;
			if (intensity != nullptr)
				*intensity = 0.0f;
			return;
		}
		if (color != nullptr)
			for (std::size_t index = 0; index < 3; ++index)
				color[index] = (*state_ptr).environment_color[index];
		if (intensity != nullptr)
			*intensity = (*state_ptr).environment_intensity;
	}

	void Render_Device::light_fill_set(const float direction[3], const float color[3], float intensity, float specular)
	{
		//转发场景域补光语义
		owner->scene().light_fill_set(direction, color, intensity, specular);
	}

	void Render_Scene::light_fill_set(const float direction[3], const float color[3], float intensity, float specular)
	{
		//若尚未初始化或任一指针为空
		if (owner == nullptr || !owner->device().valid() || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验（口径同 light_set）
		方向长度须非零、颜色各分量须落在 [0,1]、强度须非负且有限、高光缩放须落在 0..100；任一非法即整次拒绝。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool fill_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (specular >= 0.0f) && (specular <= 100.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				fill_valid = false;
		//若补光参数非法
		if (!fill_valid)
		{
			logger.warn("渲染器:: 补光参数非法（强度 {}、高光缩放 {}），整次拒绝", intensity, specular);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			(*state_ptr).fill_direction[index] = direction[index] * inverse_length;
			(*state_ptr).fill_color[index] = color[index];
		}
		(*state_ptr).fill_intensity = intensity;
		(*state_ptr).fill_specular = specular;
	}

	void Render_Scene::directional_specular_set(float scale)
	{
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return;
		//缩放须落在 0..100（取反比较使 NaN 判非法）
		if (!(scale >= 0.0f && scale <= 100.0f))
		{
			logger.warn("渲染器:: 方向光高光缩放非法（{}），整次忽略", scale);
			return;
		}
		(*state_ptr).directional_specular = scale;
	}

	bool Render_Scene::exposure_set(float ev)
	{
		//若尚未初始化或值非有限
		if (owner == nullptr || !owner->device().valid() || !std::isfinite(ev))
			return false;
		(*state_ptr).exposure = ev < -4.0f ? -4.0f : (ev > 4.0f ? 4.0f : ev);
		return true;
	}

	float Render_Scene::exposure_get(void) const
	{
		return owner != nullptr && owner->device().valid() ? (*state_ptr).exposure : 0.0f;
	}
}
