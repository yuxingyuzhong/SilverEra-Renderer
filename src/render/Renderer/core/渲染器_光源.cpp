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

	void Renderer_Scene::light_set(const float direction[3], const float color[3], float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || direction == nullptr || color == nullptr)
			return;
		/*
		参数校验
		方向长度须非零（否则无法归一化）、颜色各分量须落在 [0,1]、强度须非负且有限、环境光须落在 [0,1]；
		用「取反比较」写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		float length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (length_squared > 1e-12f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		for (std::size_t index = 0; index < 3; ++index)
			//颜色分量越界或非数即判非法
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 光照参数非法（方向 ({}, {}, {})、强度 {}、环境光 {}），整次拒绝",
				direction[0], direction[1], direction[2], intensity, ambient);
			return;
		}
		//归一化方向（内部恒存单位向量）
		float inverse_length = 1.0f / std::sqrt(length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->light_direction[index] = direction[index] * inverse_length;
			impl->light_color[index] = color[index];
		}
		impl->light_intensity = intensity;
		impl->light_ambient = ambient;
		//切回「仅方向光」（清空点光源；第三十二轮）
		impl->point_light_count = 0;
	}

	void Renderer_Scene::light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return;
		/*
		参数校验（与 light_set 同口径，另加「半径须为正且有限」）
		用取反比较写法使 NaN 同样被判为非法，任一非法即整次拒绝并保持既有光照。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f);
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、环境光 {}），整次拒绝",
				position[0], position[1], position[2], radius, intensity, ambient);
			return;
		}
		//写入点光源参数（位置即「光位置」，由着色器按模式选择语义）
		for (std::size_t index = 0; index < 3; ++index)
		{
			impl->point_lights[0].position[index] = position[index];
			impl->point_lights[0].color[index] = color[index];
			//方向光颜色保持既有值
		}
		impl->point_lights[0].radius = radius;
		impl->point_lights[0].intensity = intensity;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		impl->point_lights[0].is_spot = false;
		impl->point_light_count = 1;
		impl->light_ambient = ambient;
		//关闭方向光（第三十二轮：单点光源入口等价于「仅一个点光源」）
		impl->light_intensity = 0.0f;
	}

	bool Renderer_Scene::light_add_point(const float position[3], const float color[3], float radius, float intensity,
		float ambient, uint32_t attenuation, float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || color == nullptr)
			return false;
		/*
		参数校验（与 light_point_set 同口径）＋ 容量校验
		任一非法或已满即返回假并告警，既有光源保持不变。
		*/
		bool light_valid = (radius > 0.0f) && (radius <= 1e6f) && (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若光照参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 追加点光源参数非法（位置 ({}, {}, {})、半径 {}、强度 {}），拒绝",
				position[0], position[1], position[2], radius, intensity);
			return false;
		}
		//登记上限：Cluster 开启为全量容量，关闭为旧前向路径 8 盏（第二百八十轮）
		const uint32_t light_limit = impl->compute_domain->state().cluster_enabled ? Impl::POINT_LIGHT_MAX : Impl::POINT_LIGHT_UBO_MAX;
		//若已达上限
		if (impl->point_light_count >= light_limit)
		{
			//关闭 Cluster 时的截断须在统计中可见（不静默）
			if (!impl->compute_domain->state().cluster_enabled)
				impl->light_truncated = true;
			logger.warn("渲染器:: 点光源数量已达上限（{}），拒绝追加", light_limit);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		//本入口登记的是普通点光源（第三十四轮：清除可能残留的聚光标记）
		light.is_spot = false;
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	bool Renderer_Scene::light_add_spot(const float position[3], const float direction[3], const float color[3],
		float radius, float intensity, float inner_deg, float outer_deg, float ambient, uint32_t attenuation,
		float specular, float shininess)
	{
		//若尚未初始化或任一指针为空
		if (impl == nullptr || !impl->initialized || position == nullptr || direction == nullptr || color == nullptr)
			return false;
		/*
		参数校验（点光源同口径 ＋ 方向非退化 ＋ 锥角关系）
		方向长度须非零；锥角须满足 0 < 内角 < 外角 ≤ 90；
		用取反比较写法使 NaN 同样被判为非法，任一非法即拒绝并保持既有光源。
		*/
		float direction_length_squared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
		bool light_valid = (direction_length_squared > 1e-12f) && (radius > 0.0f) && (radius <= 1e6f)
			&& (intensity >= 0.0f) && (intensity <= 1e6f)
			&& (ambient >= 0.0f) && (ambient <= 1.0f)
			&& (inner_deg > 0.0f) && (inner_deg < outer_deg) && (outer_deg <= 90.0f)
			//逐盏高光缩放须落在 0..100（第四十九轮）
			&& (specular >= 0.0f) && (specular <= 100.0f)
			//逐盏光泽度须为 0（跟随全局）或 1..1000（第五十一轮）
			&& (shininess == 0.0f || (shininess >= 1.0f && shininess <= 1000.0f));
		//位置须为有限值
		for (std::size_t index = 0; index < 3; ++index)
			if (!(position[index] >= -1e6f && position[index] <= 1e6f))
				light_valid = false;
		//颜色分量越界或非数即判非法
		for (std::size_t index = 0; index < 3; ++index)
			if (!(color[index] >= 0.0f && color[index] <= 1.0f))
				light_valid = false;
		//若参数非法
		if (!light_valid)
		{
			logger.warn("渲染器:: 聚光灯参数非法（位置 ({}, {}, {})、半径 {}、强度 {}、内角 {}、外角 {}），拒绝",
				position[0], position[1], position[2], radius, intensity, inner_deg, outer_deg);
			return false;
		}
		//登记上限：Cluster 开启为全量容量，关闭为旧前向路径 8 盏（第二百八十轮）
		const uint32_t light_limit = impl->compute_domain->state().cluster_enabled ? Impl::POINT_LIGHT_MAX : Impl::POINT_LIGHT_UBO_MAX;
		//若已达上限
		if (impl->point_light_count >= light_limit)
		{
			//关闭 Cluster 时的截断须在统计中可见（不静默）
			if (!impl->compute_domain->state().cluster_enabled)
				impl->light_truncated = true;
			logger.warn("渲染器:: 光源数量已达上限（{}），拒绝追加聚光灯", light_limit);
			return false;
		}
		//写入新槽位（按登记顺序累加）
		Impl::Point_Light& light = impl->point_lights[impl->point_light_count];
		float inverse_length = 1.0f / std::sqrt(direction_length_squared);
		for (std::size_t index = 0; index < 3; ++index)
		{
			light.position[index] = position[index];
			light.color[index] = color[index];
			//方向归一化（内部恒存单位向量）
			light.spot_direction[index] = direction[index] * inverse_length;
		}
		light.radius = radius;
		light.intensity = intensity;
		//逐光源衰减模式（大于 2 按 2 处理；0 = 跟随全局）
		light.attenuation = attenuation > 2 ? 2 : attenuation;
		//逐盏高光缩放（第四十九轮）
		light.specular_scale = specular;
		//逐盏光泽度（第五十一轮）
		light.shininess = shininess;
		light.is_spot = true;
		//锥角转余弦（度 → 弧度；内角更小故其余弦更大）
		constexpr float degrees_to_radians = 0.017453292519943295f;
		light.spot_inner_cos = std::cos(inner_deg * degrees_to_radians);
		light.spot_outer_cos = std::cos(outer_deg * degrees_to_radians);
		impl->point_light_count += 1;
		impl->light_ambient = ambient;
		return true;
	}

	void Renderer_Scene::lights_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//清空计数并恢复方向光默认强度（方向 / 颜色 / 环境光保持既有值）
		impl->point_light_count = 0;
		//清空截断标志（第二百八十轮）
		impl->light_truncated = false;
		//清空淘汰数（第六百四十五轮）
		impl->light_dropped = 0;
		impl->light_intensity = 1.0f;
	}

	void Renderer_Scene::light_select_set(uint32_t mode, uint32_t budget)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//模式仅接受 0 / 1（其它取值按 0，保持旧口径）
		impl->light_select_mode = mode == 1u ? 1u : 0u;
		//预算钳到容量上限（0 = 跟随路径上限）
		impl->light_budget = budget > Impl::POINT_LIGHT_MAX ? Impl::POINT_LIGHT_MAX : budget;
	}

	uint32_t Renderer_Scene::point_light_count(void) const
	{
		//未初始化视为 0
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->point_light_count;
	}

	bool Renderer_Scene::point_light_state_get(uint32_t index, Point_Light_State& state_out) const
	{
		//未初始化或越界返回假（出参保持不变）
		if (impl == nullptr || !impl->initialized || index >= impl->point_light_count
			|| index >= Impl::POINT_LIGHT_MAX)
			return false;
		//内部记录 → 公开快照
		const Impl::Point_Light& light = impl->point_lights[index];
		state_out = Point_Light_State{};
		for (std::size_t each = 0; each < 3; ++each)
		{
			state_out.position[each] = light.position[each];
			state_out.color[each] = light.color[each];
			state_out.spot_direction[each] = light.spot_direction[each];
		}
		state_out.radius = light.radius;
		state_out.intensity = light.intensity;
		state_out.attenuation = light.attenuation;
		state_out.specular_scale = light.specular_scale;
		state_out.shininess = light.shininess;
		state_out.is_spot = light.is_spot;
		state_out.spot_inner_cos = light.spot_inner_cos;
		state_out.spot_outer_cos = light.spot_outer_cos;
		return true;
	}

	uint32_t Renderer_Scene::light_record_count(void) const
	{
		//未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return 2u + impl->point_light_count;
	}

	uint32_t Renderer_Scene::light_records_get(Light_Record* out, uint32_t capacity) const
	{
		//未初始化 / 出参缺失
		if (impl == nullptr || !impl->initialized || out == nullptr || capacity == 0)
			return 0;
		uint32_t written = 0;
		//写入一条记录（容量不足则只计数不写）
		auto push = [&](const Light_Record& record)
		{
			//容量内则写出
			if (written < capacity)
				out[written] = record;
			++written;
		};
		//主方向光
		{
			Light_Record record;
			record.color_intensity[0] = impl->light_color[0];
			record.color_intensity[1] = impl->light_color[1];
			record.color_intensity[2] = impl->light_color[2];
			record.color_intensity[3] = impl->light_intensity;
			record.direction[0] = impl->light_direction[0];
			record.direction[1] = impl->light_direction[1];
			record.direction[2] = impl->light_direction[2];
			record.cone[3] = impl->directional_specular;
			record.type = static_cast<uint32_t>(Light_Type::Directional);
			record.flags = 1u;
			push(record);
		}
		//补光（强度 0 = 关闭，仍占一条以保持索引稳定）
		{
			Light_Record record;
			record.color_intensity[0] = impl->fill_color[0];
			record.color_intensity[1] = impl->fill_color[1];
			record.color_intensity[2] = impl->fill_color[2];
			record.color_intensity[3] = impl->fill_intensity;
			record.direction[0] = impl->fill_direction[0];
			record.direction[1] = impl->fill_direction[1];
			record.direction[2] = impl->fill_direction[2];
			record.cone[3] = impl->fill_specular;
			record.type = static_cast<uint32_t>(Light_Type::Directional);
			record.flags = impl->fill_intensity > 0.0f ? 1u : 0u;
			push(record);
		}
		//逐盏点光 / 聚光
		for (uint32_t index = 0; index < impl->point_light_count && index < Impl::POINT_LIGHT_MAX; ++index)
		{
			const Impl::Point_Light& light = impl->point_lights[index];
			Light_Record record;
			record.pos_radius[0] = light.position[0];
			record.pos_radius[1] = light.position[1];
			record.pos_radius[2] = light.position[2];
			record.pos_radius[3] = light.radius;
			record.color_intensity[0] = light.color[0];
			record.color_intensity[1] = light.color[1];
			record.color_intensity[2] = light.color[2];
			record.color_intensity[3] = light.intensity;
			//聚光方向（点光为 0）
			if (light.is_spot)
			{
				record.direction[0] = light.spot_direction[0];
				record.direction[1] = light.spot_direction[1];
				record.direction[2] = light.spot_direction[2];
			}
			record.cone[0] = light.spot_inner_cos;
			record.cone[1] = light.spot_outer_cos;
			record.cone[2] = static_cast<float>(light.attenuation);
			record.cone[3] = light.specular_scale;
			record.shininess = light.shininess;
			record.type = static_cast<uint32_t>(light.is_spot ? Light_Type::Spot : Light_Type::Point);
			record.flags = 1u;
			//前向路径槽位（第四百八十八轮；供 Cluster 路径按同一序号定位聚光阴影）
			record.reserved = static_cast<float>(index);
			push(record);
		}
		return written < capacity ? written : capacity;
	}

	void Renderer_Scene::specular_set(float intensity, float shininess)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//参数校验（取反比较使 NaN 判非法）
		if (!(intensity >= 0.0f && intensity <= 1e6f) || !(shininess >= 1.0f && shininess <= 1e6f))
		{
			logger.warn("渲染器:: 高光参数非法（强度 {}、光泽度 {}），整次拒绝", intensity, shininess);
			return;
		}
		impl->specular_intensity = intensity;
		impl->specular_shininess = shininess;
	}

	bool Renderer_Scene::light_attenuation_set(uint32_t mode)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		//仅接受线性（0）与平方反比（1）
		if (mode > 1)
		{
			logger.warn("渲染器:: 距离衰减模式非法（{}），仅接受 0（线性）/ 1（平方反比）", mode);
			return false;
		}
		impl->attenuation_mode = mode;
		return true;
	}

	uint32_t Renderer_Scene::light_attenuation_get(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 0;
		return impl->attenuation_mode;
	}
}
