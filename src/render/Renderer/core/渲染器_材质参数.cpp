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

namespace engine
{
	//设置后续绘制所用混合档（切档触发图形管线重建）

	bool Renderer_Material::material_pbr_set(float roughness, float metallic)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体材质失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(roughness > 0.05f && roughness <= 4.0f))
		{
			logger.warn("渲染器:: 逐物体材质粗糙度 {} 越界（合法域 (0.05, 4]），已忽略", roughness);
			return false;
		}
		if (!(metallic >= 0.0f && metallic <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体材质金属度 {} 越界（合法域 [0, 1]），已忽略", metallic);
			return false;
		}
		//写入吸附状态
		impl->material_pbr_active = true;
		impl->material_pbr_roughness = roughness;
		impl->material_pbr_metallic = metallic;
		return true;
	}

	void Renderer_Material::material_pbr_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落全局参数
		impl->material_pbr_active = false;
		impl->material_pbr_roughness = 1.0f;
		impl->material_pbr_metallic = 0.0f;
	}

	bool Renderer_Material::material_pbr_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_pbr_active;
	}

	float Renderer_Material::material_pbr_roughness(void) const
	{
		//未初始化返回默认 1
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->material_pbr_roughness;
	}

	float Renderer_Material::material_pbr_metallic(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_pbr_metallic;
	}

	bool Renderer_Material::material_clear_coat_set(float strength, float roughness)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体清漆层失败：尚未初始化");
			return false;
		}
		//参数合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(strength > 0.0f && strength <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体清漆强度 {} 越界（合法域 (0, 1]），已忽略", strength);
			return false;
		}
		if (!(roughness >= 0.03f && roughness <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体清漆粗糙度 {} 越界（合法域 [0.03, 1]），已忽略", roughness);
			return false;
		}
		//写入吸附状态
		impl->material_clear_coat = strength;
		impl->material_clear_coat_roughness = roughness;
		return true;
	}

	void Renderer_Material::material_clear_coat_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落清漆关闭
		impl->material_clear_coat = 0.0f;
		impl->material_clear_coat_roughness = 0.15f;
	}

	bool Renderer_Material::material_clear_coat_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_clear_coat > 0.0f;
	}

	float Renderer_Material::material_clear_coat_strength(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_clear_coat;
	}

	float Renderer_Material::material_clear_coat_roughness(void) const
	{
		//未初始化返回默认 0.15
		if (impl == nullptr || !impl->initialized)
			return 0.15f;
		return impl->material_clear_coat_roughness;
	}

	bool Renderer_Material::material_tint_set(float red, float green, float blue, float alpha)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置逐物体色调失败：尚未初始化");
			return false;
		}
		//各分量合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(red >= 0.0f && red <= 1.0f) || !(green >= 0.0f && green <= 1.0f)
			|| !(blue >= 0.0f && blue <= 1.0f) || !(alpha >= 0.0f && alpha <= 1.0f))
		{
			logger.warn("渲染器:: 逐物体色调分量越界（{} / {} / {} / {}），已忽略", red, green, blue, alpha);
			return false;
		}
		//写入吸附状态
		impl->material_tint_active = true;
		impl->material_tint[0] = red;
		impl->material_tint[1] = green;
		impl->material_tint[2] = blue;
		impl->material_tint[3] = alpha;
		return true;
	}

	void Renderer_Material::material_tint_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		//回落白色（画面与未设置时一致）
		impl->material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			impl->material_tint[index] = 1.0f;
	}

	bool Renderer_Material::material_tint_active(void) const
	{
		//未初始化视为未启用
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->material_tint_active;
	}

	void Renderer_Material::material_tint_get(float out[4]) const
	{
		//空指针直接忽略
		if (out == nullptr)
			return;
		//未初始化返回白色
		if (impl == nullptr || !impl->initialized)
		{
			for (std::size_t index = 0; index < 4; ++index)
				out[index] = 1.0f;
			return;
		}
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = impl->material_tint[index];
	}

	bool Renderer_Material::material_emissive_set(float emissive)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
		{
			logger.error("渲染器:: 设置自发光失败：尚未初始化");
			return false;
		}
		//合法性校验（NaN 比较恒假，故也被拒绝）
		if (!(emissive >= 0.0f && emissive <= 8.0f))
		{
			logger.warn("渲染器:: 自发光 {} 越界（合法域 [0, 8]），已忽略", emissive);
			return false;
		}
		impl->material_emissive = emissive;
		return true;
	}

	void Renderer_Material::material_emissive_clear(void)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return;
		impl->material_emissive = 0.0f;
		impl->material_normal_strength = 0.0f;
	}

	float Renderer_Material::material_emissive(void) const
	{
		//未初始化返回默认 0
		if (impl == nullptr || !impl->initialized)
			return 0.0f;
		return impl->material_emissive;
	}
}
