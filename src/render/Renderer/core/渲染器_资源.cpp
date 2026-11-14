#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

//游戏引擎命名空间
namespace engine
{
	//构造（只创建域状态，不接触 GPU 资源）
	Render_Resource::Render_Resource(Renderer* owner)
		: owner(owner),
		state_ptr(new(std::nothrow) Resource_State())
	{
	}

	//析构（资源由门面关闭流程统一回收）
	Render_Resource::~Render_Resource(void)
	{
	}

	//资源域
	Resource_State& Render_Resource::state(void)
	{
		return *state_ptr;
	}

	//资源域（只读）
	const Resource_State& Render_Resource::state(void) const
	{
		return *state_ptr;
	}

	/*
	初始化
	原 renderer_init_graphics 中的资源初始化部分：建立纹理模块与内置默认资源，
	并把默认白纹理 / 默认天空盒写入所有帧槽 set1；失败时沿用原语义直接关闭门面。
	*/
	bool Render_Resource::init(const Renderer_Config& effective)
	{
		/*
		⑭ 纹理：记录内存与设备，生成内置默认白纹理（2×2 全白）并绑定到所有帧槽 set1，
		保证 descriptor_set_count = 2 时不存在未写入的描述符集。
		*/
		if (!state().texture.build(owner->impl->memory, owner->impl->device.device_get()))
		{
			logger.error("渲染器:: 纹理模块初始化失败");
			owner->shutdown();
			return false;
		}
		//2×2 全白像素（RGBA8 紧排）
		const uint8_t white_pixels[16] = {
			255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!state().texture.texture_create_from_memory(state().default_texture, white_pixels, 2, 2))
		{
			logger.error("渲染器:: 默认白纹理创建失败");
			owner->shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!state().texture.texture_bind_get(state().default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			owner->shutdown();
			return false;
		}
		//初始采样状态（逐绘制项采样集按此快照写入；第一百七十四轮）
		state().bound_view = default_view;
		state().bound_sampler = default_sampler;
		for (VkImageView& material_view : state().material_views)
			material_view = default_view;
		//把默认纹理写入所有帧槽 set1（长命资源，一次写满）
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			if (!owner->impl->pipeline.texture_bind(slot, default_sampler, default_view))
			{
				logger.error("渲染器:: 默认纹理绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
			/*
			材质贴图槽同步写入默认白纹理（第一百二十四轮；第一百七十轮合并为一次批量写）
			场景片元静态声明 binding 3 / 4 / 6 / 7 / 8，即使未使用材质也必须有有效绑定；
			白纹理使法线 / 粗糙度 / 自发光 / 遮蔽 / 金属度乘子恒为 1，故不影响默认画面。
			*/
			if (!owner->impl->pipeline.material_textures_bind(slot, default_sampler, default_view, default_view,
				default_view, default_view, default_view))
			{
				logger.error("渲染器:: 默认材质贴图绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
		}
		/*
		内置纯白立方体（第一百二十五轮）
		场景片元静态声明 binding 5（samplerCube），故即使未启用天空盒也必须有有效绑定；
		1×1×6 面全白，且默认天空盒强度为 0，故不改变既有画面。
		*/
		const uint8_t white_cube_faces[6 * 4] = {
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!state().texture.texture_create_cube(state().default_sky_texture, white_cube_faces, 1, 1))
		{
			logger.error("渲染器:: 默认天空盒立方体创建失败");
			owner->shutdown();
			return false;
		}
		//取默认立方体的视图与采样器（与默认白纹理共用同一共享采样器）
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		if (!state().texture.texture_bind_get(state().default_sky_texture, sky_view, sky_sampler))
		{
			logger.error("渲染器:: 默认天空盒立方体视图获取失败");
			owner->shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
			if (!owner->impl->pipeline.sky_bind(slot, sky_sampler, sky_view))
			{
				logger.error("渲染器:: 默认天空盒绑定失败（帧槽 {}）", slot);
				owner->shutdown();
				return false;
			}
		return true;
	}

	/*
	关闭（幂等）
	网格 / 纹理模块依赖设备与内存，须先于两者销毁；
	材质吸附状态、天空盒状态与默认资源句柄一并复位；着色器模块须先于着色器上下文复位。
	*/
	void Render_Resource::shutdown(void)
	{
		//网格缓冲依赖内存模块，须先于 memory 销毁
		state().mesh.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		state().texture.destroy();
		//逐物体 PBR 材质恢复默认（不覆盖全局，第六十八轮）
		state().material_pbr_active = false;
		state().material_pbr_roughness = 1.0f;
		state().material_pbr_metallic = 0.0f;
		//逐物体卡通参数恢复默认（不覆盖全局，第四百零八轮）
		state().material_toon_active = false;
		state().material_toon_steps = 0.0f;
		state().material_toon_rim = 0.0f;
		state().material_toon_specular = 0.0f;
		//逐物体暗部色调恢复默认（(1,1,1) 不色偏，第四百零九轮）
		state().material_toon_tint[0] = 1.0f;
		state().material_toon_tint[1] = 1.0f;
		state().material_toon_tint[2] = 1.0f;
		//逐物体色调恢复默认（白色，第六十九轮）
		state().material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			state().material_tint[index] = 1.0f;
		//逐物体自发光恢复默认（0，第七十轮）
		state().material_emissive = 0.0f;
		state().material_normal_strength = 0.0f;
		//UV 旋转与 Alpha 裁剪恢复默认（0 = 不旋转 / 关闭；第一百八十三轮）
		state().material_uv_rotation = 0.0f;
		state().material_alpha_cutoff = 0.0f;
		//默认纹理句柄复位
		state().default_texture = 0;
		//天空盒状态复位（第一百二十五轮；默认关闭、强度 0）
		state().sky_enabled = false;
		state().sky_texture = 0;
		state().default_sky_texture = 0;
		state().sky_pipeline = 0;
		state().sky_ambient = 0.0f;
		state().sky_specular = 0.0f;
		//着色器模块须先于着色器上下文复位
		if (state().vert_module != VK_NULL_HANDLE)
		{
			owner->impl->shader.module_destroy(state().vert_module);
			state().vert_module = VK_NULL_HANDLE;
		}
		if (state().frag_module != VK_NULL_HANDLE)
		{
			owner->impl->shader.module_destroy(state().frag_module);
			state().frag_module = VK_NULL_HANDLE;
		}
		//材质记录与按档缓存的共享具名管线清空（具名管线已随 extra_pipelines 释放）
		state().materials.clear();
		state().material_pipelines.clear();
		state().bound_pipeline_request = 0;
	}
}
