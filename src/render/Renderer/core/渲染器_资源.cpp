#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//获取调色板容量契约（方向十阶段 A 第 3 轮）
#include "src/render/Animation/动画数据.h"

//游戏引擎命名空间
namespace engine
{
	//构造（只创建域状态，不接触 GPU 资源）
	Render_Resource::Render_Resource(Renderer* owner, const Render_Core_Context& core_context)
		: owner(owner), core(&core_context),
		state_ptr(new(std::nothrow) Resource_State())
	{
	}

	//析构（资源由门面关闭流程统一回收）
	Render_Resource::~Render_Resource(void)
	{
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
		if (!(*state_ptr).texture.build(core->memory, core->device.device_get()))
		{
			logger.error("渲染器:: 纹理模块初始化失败");
			owner->device().shutdown();
			return false;
		}
		//2×2 全白像素（RGBA8 紧排）
		const uint8_t white_pixels[16] = {
			255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255,
		};
		if (!(*state_ptr).texture.texture_create_from_memory((*state_ptr).default_texture, white_pixels, 2, 2))
		{
			logger.error("渲染器:: 默认白纹理创建失败");
			owner->device().shutdown();
			return false;
		}
		//取默认纹理的视图与采样器
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (!(*state_ptr).texture.texture_bind_get((*state_ptr).default_texture, default_view, default_sampler))
		{
			logger.error("渲染器:: 默认白纹理视图获取失败");
			owner->device().shutdown();
			return false;
		}
		//初始采样状态（逐绘制项采样集按此快照写入；第一百七十四轮）
		(*state_ptr).bound_view = default_view;
		(*state_ptr).bound_sampler = default_sampler;
		for (VkImageView& material_view : (*state_ptr).material_views)
			material_view = default_view;
		//把默认纹理写入所有帧槽 set1（长命资源，一次写满）
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			if (!(*state_ptr).pipeline.texture_bind(slot, default_sampler, default_view))
			{
				logger.error("渲染器:: 默认纹理绑定失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			/*
			材质贴图槽同步写入默认白纹理（第一百二十四轮；第一百七十轮合并为一次批量写）
			场景片元静态声明 binding 3 / 4 / 6 / 7 / 8，即使未使用材质也必须有有效绑定；
			白纹理使法线 / 粗糙度 / 自发光 / 遮蔽 / 金属度乘子恒为 1，故不影响默认画面。
			*/
			if (!(*state_ptr).pipeline.material_textures_bind(slot, default_sampler, default_view, default_view,
				default_view, default_view, default_view))
			{
				logger.error("渲染器:: 默认材质贴图绑定失败（帧槽 {}）", slot);
				owner->device().shutdown();
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
		if (!(*state_ptr).texture.texture_create_cube((*state_ptr).default_sky_texture, white_cube_faces, 1, 1))
		{
			logger.error("渲染器:: 默认天空盒立方体创建失败");
			owner->device().shutdown();
			return false;
		}
		//取默认立方体的视图与采样器（与默认白纹理共用同一共享采样器）
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		if (!(*state_ptr).texture.texture_bind_get((*state_ptr).default_sky_texture, sky_view, sky_sampler))
		{
			logger.error("渲染器:: 默认天空盒立方体视图获取失败");
			owner->device().shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
			if (!(*state_ptr).pipeline.sky_bind(slot, sky_sampler, sky_view))
			{
				logger.error("渲染器:: 默认天空盒绑定失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
		//渲染目标记录设备、图形队列与内存模块（离屏目标按需创建）
		if (!(*state_ptr).render_target.build(core->device.device_get(), core->device.graphics_family_get(),
			core->device.graphics_queue_get(), core->memory))
		{
			logger.error("渲染器:: 渲染目标模块初始化失败");
			owner->device().shutdown();
			return false;
		}
		//渲染目标默认采样数与交换链一致（初始化守卫尚未置位，直接读取交换链已择定值）
		(*state_ptr).render_target.sample_count_set(core->swapchain.sample_count_get());
		return true;
	}

	/*
	关闭（幂等）
	网格 / 纹理模块依赖设备与内存，须先于两者销毁；
	材质吸附状态、天空盒状态与默认资源句柄一并复位；着色器模块须先于着色器上下文复位。
	*/
	void Render_Resource::shutdown(void)
	{
		//蒙皮调色板缓冲依赖内存模块，须先于 memory 销毁（收敛轮 858）
		animation_palette_resource_destroy();
		//网格缓冲依赖内存模块，须先于 memory 销毁
		(*state_ptr).mesh.destroy();
		//纹理依赖设备与内存，须先于两者销毁
		(*state_ptr).texture.destroy();
		//渲染目标依赖设备与内存，须先于两者销毁
		(*state_ptr).render_target.destroy();
		//逐物体 PBR 材质恢复默认（不覆盖全局，第六十八轮）
		(*state_ptr).material_pbr_active = false;
		(*state_ptr).material_pbr_roughness = 1.0f;
		(*state_ptr).material_pbr_metallic = 0.0f;
		//逐物体 PBR 特性覆盖恢复默认（第 777 轮；无覆盖 → 片元回落全局开关）
		(*state_ptr).material_pbr_features_active = false;
		(*state_ptr).material_pbr_features = 0;
		//逐材质色调算子恢复默认（第 779 轮；继承全局）
		(*state_ptr).material_tone_operator = 0xFF;
		//逐物体卡通参数恢复默认（不覆盖全局，第四百零八轮）
		(*state_ptr).material_toon_active = false;
		(*state_ptr).material_toon_steps = 0.0f;
		(*state_ptr).material_toon_rim = 0.0f;
		(*state_ptr).material_toon_specular = 0.0f;
		//逐物体暗部色调恢复默认（(1,1,1) 不色偏，第四百零九轮）
		(*state_ptr).material_toon_tint[0] = 1.0f;
		(*state_ptr).material_toon_tint[1] = 1.0f;
		(*state_ptr).material_toon_tint[2] = 1.0f;
		//逐物体色调恢复默认（白色，第六十九轮）
		(*state_ptr).material_tint_active = false;
		for (std::size_t index = 0; index < 4; ++index)
			(*state_ptr).material_tint[index] = 1.0f;
		//逐物体自发光恢复默认（0，第七十轮）
		(*state_ptr).material_emissive = 0.0f;
		(*state_ptr).material_normal_strength = 0.0f;
		//UV 旋转与 Alpha 裁剪恢复默认（0 = 不旋转 / 关闭；第一百八十三轮）
		(*state_ptr).material_uv_rotation = 0.0f;
		(*state_ptr).material_alpha_cutoff = 0.0f;
		//默认纹理句柄复位
		(*state_ptr).default_texture = 0;
		//天空盒状态复位（第一百二十五轮；默认关闭、强度 0）
		(*state_ptr).sky_enabled = false;
		(*state_ptr).sky_texture = 0;
		(*state_ptr).default_sky_texture = 0;
		(*state_ptr).sky_pipeline = 0;
		(*state_ptr).sky_ambient = 0.0f;
		(*state_ptr).sky_specular = 0.0f;
		//着色器模块须先于着色器上下文复位
		if ((*state_ptr).vert_module != VK_NULL_HANDLE)
		{
			(*state_ptr).shader.module_destroy((*state_ptr).vert_module);
			(*state_ptr).vert_module = VK_NULL_HANDLE;
		}
		if ((*state_ptr).frag_module != VK_NULL_HANDLE)
		{
			(*state_ptr).shader.module_destroy((*state_ptr).frag_module);
			(*state_ptr).frag_module = VK_NULL_HANDLE;
		}
		//材质记录与按档缓存的共享具名管线清空（具名管线已随 extra_pipelines 释放）
		(*state_ptr).materials.clear();
		(*state_ptr).material_pipelines.clear();
		(*state_ptr).bound_pipeline_request = 0;
	}

	//预留逐绘制项描述符输入
	void Render_Resource::frame_inputs_reserve(void)
	{
		(*state_ptr).draw_set_inputs.reserve(Pipeline_Context::DRAW_SET_PER_SLOT);
	}

	//构建着色器宿主并加载默认场景着色器
	bool Render_Resource::scene_shaders_load(const std::string& shader_dir, uint32_t& vertex_input_locations)
	{
		if (!(*state_ptr).shader.build(core->device.device_get()))
			return false;
		(*state_ptr).shader.directory_set(shader_dir);
		if (!(*state_ptr).shader.module_load((*state_ptr).vert_module, "场景.vert.spv", &vertex_input_locations))
			return false;
		return (*state_ptr).shader.module_load((*state_ptr).frag_module, "场景.frag.spv");
	}

	//构建图形管线基础设施
	bool Render_Resource::scene_pipeline_infrastructure_build(uint32_t color_format, uint32_t depth_format,
		uint32_t frame_overlap, uint32_t sample_count, uint32_t max_push_constant_size)
	{
		Pipeline_Config pipeline_config{};
		pipeline_config.color_format = static_cast<VkFormat>(color_format);
		pipeline_config.depth_format = static_cast<VkFormat>(depth_format);
		pipeline_config.frame_overlap = frame_overlap;
		pipeline_config.sample_count = sample_count;
		pipeline_config.max_push_constant_size = max_push_constant_size;
		return (*state_ptr).pipeline.build(core->device.device_get(), pipeline_config);
	}

	//构建默认场景管线
	bool Render_Resource::scene_pipeline_build(uint32_t vertex_input_locations)
	{
		return (*state_ptr).pipeline.pipeline_build((*state_ptr).vert_module, (*state_ptr).frag_module,
			Blend_Mode::Opaque, vertex_input_locations);
	}

	//加载独立着色器模块
	bool Render_Resource::shader_module_load(uint64_t& module_out, const std::string& shader_name)
	{
		VkShaderModule module = VK_NULL_HANDLE;
		if (!(*state_ptr).shader.module_load(module, shader_name))
		{
			module_out = 0;
			return false;
		}
		module_out = reinterpret_cast<uint64_t>(module);
		return true;
	}

	//销毁独立着色器模块
	void Render_Resource::shader_module_destroy(uint64_t module)
	{
		if (module != 0)
			(*state_ptr).shader.module_destroy(reinterpret_cast<VkShaderModule>(module));
	}

	//登记阴影图形管线
	bool Render_Resource::shadow_pipeline_build(uint64_t& handle_out)
	{
		handle_out = 0;
		VkShaderModule shadow_vert = VK_NULL_HANDLE;
		VkShaderModule shadow_frag = VK_NULL_HANDLE;
		uint32_t shadow_input_locations = 0;
		if (!state_ptr->shader.module_load(shadow_vert, "阴影.vert.spv", &shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：顶点着色器加载失败，阴影保持关闭");
			return false;
		}
		if (!state_ptr->shader.module_load(shadow_frag, "阴影.frag.spv"))
		{
			logger.error("渲染器:: 阴影管线登记失败：片元着色器加载失败，阴影保持关闭");
			state_ptr->shader.module_destroy(shadow_vert);
			return false;
		}
		uint64_t shadow_pipeline = 0;
		if (!state_ptr->pipeline.pipeline_register(shadow_pipeline, shadow_vert, shadow_frag,
			Blend_Mode::Opaque, VK_FORMAT_R8G8B8A8_UNORM, true, 1, shadow_input_locations))
		{
			logger.error("渲染器:: 阴影管线登记失败：图形管线创建失败，阴影保持关闭");
			state_ptr->shader.module_destroy(shadow_vert);
			state_ptr->shader.module_destroy(shadow_frag);
			return false;
		}
		Extra_Pipeline_Record record{};
		record.handle = shadow_pipeline;
		record.vert_module = shadow_vert;
		record.frag_module = shadow_frag;
		state_ptr->extra_pipelines.push_back(record);
		handle_out = shadow_pipeline;
		return true;
	}

	//初始化网格资源模块
	bool Render_Resource::mesh_resource_build(void)
	{
		return state_ptr->mesh.build(core->memory);
	}

	//填充资源域资源计数
	void Render_Resource::resource_counts_fill(Renderer_Resource_Counts& counts) const
	{
		counts.meshes = state_ptr->mesh.mesh_count();
		counts.textures = state_ptr->texture.texture_count();
		counts.pipelines = state_ptr->pipeline.pipeline_count();
		counts.render_targets = state_ptr->render_target.target_count();
		for (const Material_Record& record : state_ptr->materials)
			if (record.valid)
				++counts.materials;
	}

	//查询图形管线是否有效
	bool Render_Resource::pipeline_valid(uint64_t handle) const
	{
		return state_ptr->pipeline.pipeline_has(handle);
	}

	//查询图形管线采样数
	uint32_t Render_Resource::pipeline_sample_count(uint64_t handle) const
	{
		return state_ptr->pipeline.pipeline_sample_count(handle);
	}

	//绑定后处理查找表
	void Render_Resource::post_lut_bind(uint64_t texture_handle, uint32_t frame_overlap)
	{
		const uint64_t target_handle = texture_handle == 0 ? state_ptr->default_texture : texture_handle;
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!state_ptr->texture.texture_bind_get(target_handle, view, sampler))
		{
			logger.warn("渲染器:: 查找表纹理句柄 {} 无效，绑定忽略", texture_handle);
			return;
		}
		core->frame.frames_wait();
		bool all_bound = true;
		for (uint32_t slot = 0; slot < frame_overlap; ++slot)
			for (uint32_t index = 0; index < Pipeline_Context::POST_SET_PER_SLOT; ++index)
				if (!state_ptr->pipeline.post_lut_bind(slot, sampler, view, index))
					all_bound = false;
		if (all_bound)
			state_ptr->lut_texture = texture_handle == 0 ? 0 : texture_handle;
		logger.info("渲染器:: 3D LUT 绑定已更新（句柄 {}）；清除时为 0", texture_handle);
	}

	//读取后处理查找表
	uint64_t Render_Resource::post_lut_get(void) const
	{
		return state_ptr->lut_texture;
	}

	//切换渲染目标采样数
	bool Render_Resource::render_target_sample_count_set(uint32_t samples)
	{
		return state_ptr->render_target.sample_count_set(samples);
	}

	//恢复默认纹理绑定
	void Render_Resource::default_texture_rebind(uint32_t frame_overlap)
	{
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		if (state_ptr->texture.texture_bind_get(state_ptr->default_texture, default_view, default_sampler))
		{
			state_ptr->bound_view = default_view;
			for (uint32_t slot = 0; slot < frame_overlap; ++slot)
				state_ptr->pipeline.texture_bind(slot, default_sampler, default_view);
		}
	}

	//装配原生 UI 绘制资源
	bool Render_Resource::ui_render_resource_build(Ui_Renderer& ui_render, Font_Context& font,
		uint64_t solid_pipeline, uint64_t text_pipeline, uint32_t frame_overlap)
	{
		return ui_render.build(core->device.device_get(), core->memory, state_ptr->pipeline, state_ptr->texture,
			font, solid_pipeline, text_pipeline, frame_overlap);
	}

	//装配字体纹理资源
	bool Render_Resource::font_resource_build(Font_Context& font)
	{
		return font.build(state_ptr->texture);
	}

	//创建文字网格
	bool Render_Resource::text_mesh_create(Mesh_Handle& mesh, const Mesh_Data& data)
	{
		return state_ptr->mesh.mesh_create(mesh, data);
	}

	//销毁文字网格
	void Render_Resource::text_mesh_destroy(Mesh_Handle mesh)
	{
		if (state_ptr->mesh.valid())
			state_ptr->mesh.mesh_destroy(mesh);
	}

	//读取默认白纹理原生句柄
	bool Render_Resource::default_texture_native_get(uint64_t& view_out, uint64_t& sampler_out) const
	{
		return texture_native_get(state_ptr->default_texture, view_out, sampler_out);
	}

	//读取网格原生缓冲值快照
	bool Render_Resource::mesh_native_get(Mesh_Handle handle, uint64_t& buffer_out,
		uint64_t& index_offset_out, uint32_t& index_count_out) const
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize index_offset = 0;
		if (!state_ptr->mesh.mesh_record_get(handle, buffer, index_offset, index_count_out))
			return false;
		buffer_out = reinterpret_cast<uint64_t>(buffer);
		index_offset_out = index_offset;
		return true;
	}

	//读取图形管线布局
	uint64_t Render_Resource::pipeline_layout_native(void) const
	{
		return reinterpret_cast<uint64_t>(state_ptr->pipeline.layout_get());
	}

	//读取帧槽场景描述符集
	uint64_t Render_Resource::pipeline_scene_set_native(uint32_t slot, bool particle) const
	{
		return reinterpret_cast<uint64_t>(particle ? state_ptr->pipeline.particle_set_get(slot)
			: state_ptr->pipeline.scene_set_get(slot));
	}

	//读取帧槽共享采样描述符集
	uint64_t Render_Resource::pipeline_sampler_set_native(uint32_t slot) const
	{
		return reinterpret_cast<uint64_t>(state_ptr->pipeline.sampler_set_get(slot));
	}

	//读取帧槽后处理描述符集
	uint64_t Render_Resource::pipeline_post_set_native(uint32_t slot, uint32_t stage) const
	{
		return reinterpret_cast<uint64_t>(state_ptr->pipeline.post_set_get(slot, stage));
	}

	//当前请求绑定的图形管线句柄
	uint64_t Render_Resource::pipeline_bound_handle(void) const
	{
		return state_ptr->bound_pipeline_request;
	}

	//绑定一个帧槽的粒子场景、状态与索引缓冲
	bool Render_Resource::particle_buffers_bind(uint32_t slot, uint64_t scene_buffer,
		uint64_t state_buffer, uint64_t index_buffer)
	{
		return state_ptr->pipeline.particle_buffers_bind(slot, reinterpret_cast<VkBuffer>(scene_buffer),
			reinterpret_cast<VkBuffer>(state_buffer), reinterpret_cast<VkBuffer>(index_buffer));
	}

	//读取渲染目标的基本帧附件
	bool Render_Resource::render_target_frame_get(uint64_t handle, Frame_Render_Target& target_out) const
	{
		target_out = Frame_Render_Target{};
		return state_ptr->render_target.target_get(handle, target_out.color_image, target_out.color_view,
			target_out.depth_view, target_out.extent);
	}

	//读取渲染目标的完整帧附件
	bool Render_Resource::render_target_frame_full_get(uint64_t handle, Frame_Render_Target& target_out) const
	{
		if (!render_target_frame_get(handle, target_out))
			return false;
		/*
		颜色 / 深度格式（第 757 轮）
		取目标自身记录，供动态渲染与并行录制的继承信息使用；
		16F 场景目标若沿用交换链格式会与图像视图不匹配。
		*/
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		if (state_ptr->render_target.target_format_get(handle, color_format, depth_format))
		{
			target_out.color_format = color_format;
			target_out.depth_format = depth_format;
		}
		if (!state_ptr->render_target.target_depth_get(handle, target_out.depth_image, target_out.depth_view)
			|| !state_ptr->render_target.target_msaa_get(handle, target_out.msaa_color_image,
				target_out.msaa_color_view, target_out.msaa_depth_view, target_out.sample_count))
			return false;
		/*
		逐材质色调编号附件（第 808 轮；可选，未建立时保持空句柄）
		该附件与既有渲染路径无关，取不到不影响目标可用性。
		*/
		state_ptr->render_target.target_tone_id_get(handle, target_out.tone_id_image,
			target_out.tone_id_view, target_out.tone_id_format);
		//多重采样编号附件（第 821 轮；单采样目标取不到，保持空）
		state_ptr->render_target.target_tone_id_msaa_get(handle, target_out.tone_id_msaa_image,
			target_out.tone_id_msaa_view);
		return true;
	}

	//读取多重采样深度图像
	uint64_t Render_Resource::render_target_msaa_depth_image_native(uint64_t handle) const
	{
		VkImage image = VK_NULL_HANDLE;
		state_ptr->render_target.target_msaa_depth_image_get(handle, image);
		return reinterpret_cast<uint64_t>(image);
	}

	//填装一帧绘制项的描述符集与实例缓冲
	void Render_Resource::frame_descriptor_sets_fill(std::vector<Frame_Render_Item>& items, uint32_t slot,
		uint64_t instance_buffer, uint64_t shadow_target, bool spot_atlas,
		uint64_t spot_atlas_target, const uint64_t spot_targets[8])
	{
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler default_sampler = VK_NULL_HANDLE;
		state_ptr->texture.texture_bind_get(state_ptr->default_texture, default_view, default_sampler);
		VkImageView shadow_view = default_view;
		if (shadow_target != 0)
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView depth_view = VK_NULL_HANDLE;
			VkExtent2D extent{};
			state_ptr->render_target.target_get(shadow_target, image, shadow_view, depth_view, extent);
		}
		VkImageView spot_views[8] = { default_view, default_view, default_view, default_view,
			default_view, default_view, default_view, default_view };
		if (spot_atlas && spot_atlas_target != 0)
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView atlas_view = VK_NULL_HANDLE;
			VkImageView depth_view = VK_NULL_HANDLE;
			VkExtent2D extent{};
			if (state_ptr->render_target.target_get(spot_atlas_target, image, atlas_view, depth_view, extent))
				for (std::size_t tile = 0; tile < 8; ++tile)
					spot_views[tile] = atlas_view;
		}
		else
		{
			for (std::size_t tile = 0; tile < 8; ++tile)
			{
				if (spot_targets[tile] == 0)
					continue;
				VkImage image = VK_NULL_HANDLE;
				VkImageView depth_view = VK_NULL_HANDLE;
				VkExtent2D extent{};
				state_ptr->render_target.target_get(spot_targets[tile], image, spot_views[tile], depth_view, extent);
			}
		}
		VkImageView sky_view = VK_NULL_HANDLE;
		VkSampler sky_sampler = VK_NULL_HANDLE;
		const uint64_t sky_handle = state_ptr->sky_texture != 0 ? state_ptr->sky_texture : state_ptr->default_sky_texture;
		state_ptr->texture.texture_bind_get(sky_handle, sky_view, sky_sampler);
		state_ptr->draw_set_inputs.clear();
		for (auto& item : items)
		{
			item.instance_buffer = item.instance_input ? reinterpret_cast<VkBuffer>(instance_buffer) : VK_NULL_HANDLE;
			item.scene_set = item.particle_scene_set ? state_ptr->pipeline.particle_set_get(slot)
				: state_ptr->pipeline.scene_set_get(slot);
			item.sampler_set = state_ptr->pipeline.sampler_set_get(slot);
			item.descriptor_set_count = 2;
			if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE
				&& state_ptr->draw_set_inputs.size() < Pipeline_Context::DRAW_SET_PER_SLOT)
			{
				Pipeline_Context::Draw_Set_Views views{};
				views.sampler = item.draw_sampler;
				views.albedo = item.draw_albedo;
				views.normal = item.draw_normal != VK_NULL_HANDLE ? item.draw_normal : default_view;
				views.roughness = item.draw_roughness != VK_NULL_HANDLE ? item.draw_roughness : default_view;
				views.emissive = item.draw_emissive != VK_NULL_HANDLE ? item.draw_emissive : default_view;
				views.ao = item.draw_ao != VK_NULL_HANDLE ? item.draw_ao : default_view;
				views.metallic = item.draw_metallic != VK_NULL_HANDLE ? item.draw_metallic : default_view;
				views.shadow = shadow_view != VK_NULL_HANDLE ? shadow_view : default_view;
				views.sky = sky_view != VK_NULL_HANDLE ? sky_view : default_view;
				for (std::size_t tile = 0; tile < 8; ++tile)
					views.spot_shadow[tile] = spot_views[tile];
				views.depth = item.draw_depth;
				views.depth_sampler = item.draw_depth_sampler;
				state_ptr->draw_set_inputs.push_back(views);
			}
		}
		const uint32_t written = static_cast<uint32_t>(state_ptr->draw_set_inputs.size());
		if (written > 0)
			state_ptr->pipeline.draw_sets_bind_batch(slot, state_ptr->draw_set_inputs.data(), written);
		uint32_t cursor = 0;
		for (auto& item : items)
		{
			if (item.draw_albedo != VK_NULL_HANDLE && item.draw_sampler != VK_NULL_HANDLE && cursor < written)
			{
				item.sampler_set = state_ptr->pipeline.draw_set_get(slot, cursor);
				++cursor;
			}
		}
	}

	//绑定帧槽场景 UBO
	bool Render_Resource::scene_buffer_bind(uint32_t slot, uint64_t scene_buffer)
	{
		return (*state_ptr).pipeline.scene_bind(slot, reinterpret_cast<VkBuffer>(scene_buffer));
	}

	//绑定帧槽方向光阴影图
	bool Render_Resource::scene_shadow_bind(uint32_t slot, uint64_t sampler, uint64_t shadow_view)
	{
		return (*state_ptr).pipeline.shadow_bind(slot, reinterpret_cast<VkSampler>(sampler),
			reinterpret_cast<VkImageView>(shadow_view));
	}

	//绑定帧槽聚光阴影瓦片
	bool Render_Resource::scene_spot_shadow_bind(uint32_t slot, uint64_t sampler, uint64_t shadow_view, uint32_t tile)
	{
		return (*state_ptr).pipeline.spot_shadow_bind(slot, reinterpret_cast<VkSampler>(sampler),
			reinterpret_cast<VkImageView>(shadow_view), tile);
	}

	//重建并改绑整组阴影目标，任一失败均销毁候选并回绑旧图
	bool Render_Resource::shadow_targets_resize(uint64_t old_directional, const uint64_t old_spot_targets[8],
		uint32_t resolution, uint32_t old_resolution, uint32_t frame_overlap, uint32_t depth_format,
		uint64_t& new_directional, uint64_t new_spot_targets[8])
	{
		new_directional = 0;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			new_spot_targets[tile] = 0;
		//旧阴影图视图（失败回绑用）
		VkImage old_image = VK_NULL_HANDLE;
		VkImageView old_view = VK_NULL_HANDLE;
		VkImageView old_depth_view = VK_NULL_HANDLE;
		VkExtent2D old_extent{};
		if (!state_ptr->render_target.target_get(old_directional, old_image, old_view, old_depth_view, old_extent))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：旧阴影图查询失败");
			return false;
		}
		VkImageView old_spot_views[Device_State::SPOT_SHADOW_TILES] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
			VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			if (old_spot_targets[tile] == 0)
				continue;
			VkImage old_spot_image = VK_NULL_HANDLE;
			VkImageView old_spot_depth_view = VK_NULL_HANDLE;
			VkExtent2D old_spot_extent{};
			if (!state_ptr->render_target.target_get(old_spot_targets[tile], old_spot_image, old_spot_views[tile],
				old_spot_depth_view, old_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：旧聚光灯阴影图查询失败（瓦片 {}）", tile);
				return false;
			}
		}
		//先创建全部候选，任一失败均保留旧目标
		uint64_t candidate = 0;
		if (!state_ptr->render_target.target_create(candidate, resolution, resolution,
			VK_FORMAT_R8G8B8A8_UNORM, static_cast<VkFormat>(depth_format), 1))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影目标创建失败（保持 {}）", old_resolution);
			return false;
		}
		uint64_t candidate_spot_tiles[Device_State::SPOT_SHADOW_TILES] = {};
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			if (old_spot_targets[tile] == 0)
				continue;
			if (!state_ptr->render_target.target_create(candidate_spot_tiles[tile], resolution, resolution,
				VK_FORMAT_R8G8B8A8_UNORM, static_cast<VkFormat>(depth_format), 1))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影目标创建失败（瓦片 {}）", tile);
				state_ptr->render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < tile; ++created)
					if (candidate_spot_tiles[created] != 0)
						state_ptr->render_target.target_destroy(candidate_spot_tiles[created]);
				return false;
			}
		}
		//解析新方向光视图与共享采样器
		VkImage new_image = VK_NULL_HANDLE;
		VkImageView new_view = VK_NULL_HANDLE;
		VkImageView new_depth_view = VK_NULL_HANDLE;
		VkExtent2D new_extent{};
		VkImageView default_view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!state_ptr->render_target.target_get(candidate, new_image, new_view, new_depth_view, new_extent)
			|| !state_ptr->texture.texture_bind_get(state_ptr->default_texture, default_view, sampler))
		{
			logger.error("渲染器:: 阴影分辨率切换失败：新阴影图查询失败");
			state_ptr->render_target.target_destroy(candidate);
			for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
				if (candidate_spot_tiles[tile] != 0)
					state_ptr->render_target.target_destroy(candidate_spot_tiles[tile]);
			return false;
		}
		VkImageView new_spot_views[Device_State::SPOT_SHADOW_TILES] = { default_view, default_view, default_view,
			default_view, default_view, default_view, default_view, default_view };
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			if (candidate_spot_tiles[tile] == 0)
				continue;
			VkImage new_spot_image = VK_NULL_HANDLE;
			VkImageView new_spot_depth_view = VK_NULL_HANDLE;
			VkExtent2D new_spot_extent{};
			if (!state_ptr->render_target.target_get(candidate_spot_tiles[tile], new_spot_image, new_spot_views[tile],
				new_spot_depth_view, new_spot_extent))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：新聚光灯阴影图查询失败（瓦片 {}）", tile);
				state_ptr->render_target.target_destroy(candidate);
				for (std::size_t created = 0; created < Device_State::SPOT_SHADOW_TILES; ++created)
					if (candidate_spot_tiles[created] != 0)
						state_ptr->render_target.target_destroy(candidate_spot_tiles[created]);
				return false;
			}
		}
		//方向光逐槽改绑，失败时含失败槽一并回绑
		for (std::size_t slot = 0; slot < frame_overlap; ++slot)
		{
			if (!state_ptr->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, new_view))
			{
				logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 绑定失败，回绑旧阴影图", slot);
				for (std::size_t revert = 0; revert <= slot; ++revert)
					state_ptr->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
				state_ptr->render_target.target_destroy(candidate);
				for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
					if (candidate_spot_tiles[tile] != 0)
						state_ptr->render_target.target_destroy(candidate_spot_tiles[tile]);
				return false;
			}
		}
		//聚光任一槽失败时方向光与全部已重建瓦片在所有槽回绑
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			if (candidate_spot_tiles[tile] == 0)
				continue;
			for (std::size_t slot = 0; slot < frame_overlap; ++slot)
			{
				if (!state_ptr->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, new_spot_views[tile],
					static_cast<uint32_t>(tile)))
				{
					logger.error("渲染器:: 阴影分辨率切换失败：帧槽 {} 聚光灯阴影绑定失败（瓦片 {}），回绑旧图", slot, tile);
					for (std::size_t revert = 0; revert < frame_overlap; ++revert)
					{
						state_ptr->pipeline.shadow_bind(static_cast<uint32_t>(revert), sampler, old_view);
						for (std::size_t target_tile = 0; target_tile < Device_State::SPOT_SHADOW_TILES; ++target_tile)
						{
							if (candidate_spot_tiles[target_tile] == 0)
								continue;
							VkImageView revert_view = old_spot_views[target_tile] != VK_NULL_HANDLE
								? old_spot_views[target_tile] : new_spot_views[target_tile];
							state_ptr->pipeline.spot_shadow_bind(static_cast<uint32_t>(revert), sampler, revert_view,
								static_cast<uint32_t>(target_tile));
						}
					}
					state_ptr->render_target.target_destroy(candidate);
					for (std::size_t created = 0; created < Device_State::SPOT_SHADOW_TILES; ++created)
						if (candidate_spot_tiles[created] != 0)
							state_ptr->render_target.target_destroy(candidate_spot_tiles[created]);
					return false;
				}
			}
		}
		//全部改绑成功后才销毁旧目标并返回候选句柄
		state_ptr->render_target.target_destroy(old_directional);
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			if (candidate_spot_tiles[tile] != 0)
				state_ptr->render_target.target_destroy(old_spot_targets[tile]);
		new_directional = candidate;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
			new_spot_targets[tile] = candidate_spot_tiles[tile];
		return true;
	}

	//写入一帧槽的一级后处理采样绑定
	bool Render_Resource::post_stage_bind(uint32_t slot, uint32_t stage, uint64_t sampler, uint64_t source_view,
		uint64_t scene_view, uint64_t depth_view, uint64_t shadow_view, uint64_t tone_id_view)
	{
		VkSampler native_sampler = reinterpret_cast<VkSampler>(sampler);
		bool result = (*state_ptr).pipeline.post_texture_bind(slot, native_sampler,
			reinterpret_cast<VkImageView>(source_view), stage);
		result = (*state_ptr).pipeline.post_scene_bind(slot, native_sampler,
			reinterpret_cast<VkImageView>(scene_view), stage) && result;
		if (depth_view != 0)
			result = (*state_ptr).pipeline.post_depth_bind(slot, native_sampler,
				reinterpret_cast<VkImageView>(depth_view), stage) && result;
		if (shadow_view != 0)
			result = (*state_ptr).pipeline.post_shadow_bind(slot, native_sampler,
				reinterpret_cast<VkImageView>(shadow_view), stage) && result;
		/*
		逐材质色调编号附件（第八百一十二轮；第 832 轮改用最近邻采样器）
		启用编号通道时为目标编号视图，否则为 1×1 零图；两者都写，保证材质色调映射片元的 binding 10 恒有效。
		编号附件为整数格式，线性过滤不合法，故不复用材质的线性采样器。
		*/
		if (tone_id_view != 0)
		{
			//编号附件专用采样器（取不到时回落材质采样器）
			VkSampler tone_sampler = VK_NULL_HANDLE;
			if (!(*state_ptr).render_target.tone_id_sampler_get(tone_sampler))
				tone_sampler = native_sampler;
			result = (*state_ptr).pipeline.post_tone_id_bind(slot, tone_sampler,
				reinterpret_cast<VkImageView>(tone_id_view), stage) && result;
		}
		return result;
	}

	//取默认编号附件视图（第八百一十二轮）
	bool Render_Resource::render_target_tone_id_default_get(uint64_t& view_out)
	{
		//出参先复位
		view_out = 0;
		//若尚未初始化
		if (owner == nullptr || !owner->device().valid())
			return false;
		//转交渲染目标模块（惰性创建 1×1 R8_UINT 零图）
		VkImageView view = VK_NULL_HANDLE;
		if (!(*state_ptr).render_target.tone_id_default_get(view))
			return false;
		view_out = reinterpret_cast<uint64_t>(view);
		return true;
	}

	//取多重采样编号附件（第 821 轮）
	bool Render_Resource::render_target_tone_id_msaa_get(uint64_t handle, uint64_t& image_out,
		uint64_t& view_out) const
	{
		//出参先复位
		image_out = 0;
		view_out = 0;
		//若尚未初始化
		if (state_ptr == nullptr)
			return false;
		//转交渲染目标模块
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		if (!(*state_ptr).render_target.target_tone_id_msaa_get(handle, image, view))
			return false;
		image_out = reinterpret_cast<uint64_t>(image);
		view_out = reinterpret_cast<uint64_t>(view);
		return true;
	}

	//按显式格式创建渲染目标
	bool Render_Resource::render_target_create_format(uint64_t& handle_out, uint32_t width, uint32_t height,
		uint32_t color_format, uint32_t depth_format, uint32_t sample_count)
	{
		return (*state_ptr).render_target.target_create(handle_out, width, height,
			static_cast<VkFormat>(color_format), static_cast<VkFormat>(depth_format), sample_count);
	}

	/*
	按显式格式创建渲染目标（含用户名调编号附件，第 801 轮）
	tone_id_format = VK_FORMAT_UNDEFINED 时与 render_target_create_format 完全一致；
	非 0 且采样数为 1 时额外创建单采样编号附件（建议 VK_FORMAT_R8_UINT），供 8 位路径逐材质色调使用。
	段 1 仅提供资源，管线与帧链路在后续段接入。
	*/
	bool Render_Resource::render_target_create_format_ex(uint64_t& handle_out, uint32_t width, uint32_t height,
		uint32_t color_format, uint32_t depth_format, uint32_t tone_id_format, uint32_t sample_count)
	{
		return (*state_ptr).render_target.target_create(handle_out, width, height,
			static_cast<VkFormat>(color_format), static_cast<VkFormat>(depth_format), sample_count,
			static_cast<VkFormat>(tone_id_format));
	}

	//查询渲染目标的色调编号附件（第 801 轮；未启用返回假）
	bool Render_Resource::render_target_tone_id_get(uint64_t handle, uint64_t& image_out,
		uint64_t& view_out, uint32_t& format_out) const
	{
		//出参先复位
		image_out = 0;
		view_out = 0;
		format_out = 0;
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		if (!(*state_ptr).render_target.target_tone_id_get(handle, image, view, format))
			return false;
		image_out = reinterpret_cast<uint64_t>(image);
		view_out = reinterpret_cast<uint64_t>(view);
		format_out = static_cast<uint32_t>(format);
		return true;
	}

	//读取渲染目标原生附件值快照
	bool Render_Resource::render_target_native_get(uint64_t handle, Render_Target_Native_Info& info_out) const
	{
		info_out = Render_Target_Native_Info{};
		VkImage color_image = VK_NULL_HANDLE;
		VkImageView color_view = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkExtent2D extent{};
		if (!(*state_ptr).render_target.target_get(handle, color_image, color_view, depth_view, extent))
			return false;
		VkImage depth_image = VK_NULL_HANDLE;
		VkImageView depth_view_check = VK_NULL_HANDLE;
		VkImage msaa_color_image = VK_NULL_HANDLE;
		VkImageView msaa_color_view = VK_NULL_HANDLE;
		VkImageView msaa_depth_view = VK_NULL_HANDLE;
		VkImage msaa_depth_image = VK_NULL_HANDLE;
		uint32_t sample_count = 1;
		if (!(*state_ptr).render_target.target_depth_get(handle, depth_image, depth_view_check)
			|| !(*state_ptr).render_target.target_msaa_get(handle, msaa_color_image, msaa_color_view,
				msaa_depth_view, sample_count)
			|| !(*state_ptr).render_target.target_msaa_depth_image_get(handle, msaa_depth_image))
			return false;
		info_out.color_image = reinterpret_cast<uint64_t>(color_image);
		info_out.color_view = reinterpret_cast<uint64_t>(color_view);
		info_out.depth_image = reinterpret_cast<uint64_t>(depth_image);
		info_out.depth_view = reinterpret_cast<uint64_t>(depth_view);
		info_out.msaa_color_image = reinterpret_cast<uint64_t>(msaa_color_image);
		info_out.msaa_color_view = reinterpret_cast<uint64_t>(msaa_color_view);
		info_out.msaa_depth_image = reinterpret_cast<uint64_t>(msaa_depth_image);
		info_out.msaa_depth_view = reinterpret_cast<uint64_t>(msaa_depth_view);
		info_out.width = extent.width;
		info_out.height = extent.height;
		info_out.sample_count = sample_count;
		//颜色 / 深度格式（第 828 轮）
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		if (!(*state_ptr).render_target.target_format_get(handle, color_format, depth_format))
			return false;
		info_out.color_format = static_cast<uint32_t>(color_format);
		info_out.depth_format = static_cast<uint32_t>(depth_format);
		return true;
	}

	//立即释放渲染目标记录
	void Render_Resource::render_target_release(uint64_t handle)
	{
		(*state_ptr).render_target.target_destroy(handle);
	}

	//读取纹理原生视图与采样器
	bool Render_Resource::texture_native_get(uint64_t handle, uint64_t& view_out, uint64_t& sampler_out) const
	{
		VkImageView view = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		if (!(*state_ptr).texture.texture_bind_get(handle, view, sampler))
			return false;
		view_out = reinterpret_cast<uint64_t>(view);
		sampler_out = reinterpret_cast<uint64_t>(sampler);
		return true;
	}

	//内置纹理句柄查询
	uint64_t Render_Resource::default_texture_handle(void) const
	{
		return (*state_ptr).default_texture;
	}

	uint64_t Render_Resource::default_sky_texture_handle(void) const
	{
		return (*state_ptr).default_sky_texture;
	}

	uint64_t Render_Resource::sky_pipeline_handle(void) const
	{
		return (*state_ptr).sky_pipeline;
	}

	//惰性创建天空盒具名管线
	bool Render_Resource::sky_pipeline_resource_ensure(void)
	{
		if ((*state_ptr).sky_pipeline != 0)
			return true;
		VkShaderModule vert_module = VK_NULL_HANDLE;
		VkShaderModule frag_module = VK_NULL_HANDLE;
		uint32_t vert_input_locations = 0;
		if (!(*state_ptr).shader.module_load(vert_module, "天空盒.vert.spv", &vert_input_locations))
		{
			logger.error("渲染器:: 天空盒顶点着色器加载失败");
			return false;
		}
		if (!(*state_ptr).shader.module_load(frag_module, "天空盒.frag.spv"))
		{
			logger.error("渲染器:: 天空盒片元着色器加载失败");
			(*state_ptr).shader.module_destroy(vert_module);
			return false;
		}
		uint64_t registered = 0;
		if (!(*state_ptr).pipeline.pipeline_register(registered, vert_module, frag_module, Blend_Mode::Opaque,
			VK_FORMAT_UNDEFINED, true, 0, vert_input_locations, false, true))
		{
			logger.error("渲染器:: 天空盒管线登记失败");
			(*state_ptr).shader.module_destroy(vert_module);
			(*state_ptr).shader.module_destroy(frag_module);
			return false;
		}
		(*state_ptr).sky_pipeline = registered;
		(*state_ptr).extra_pipelines.push_back(Extra_Pipeline_Record{ registered, vert_module, frag_module });
		logger.info("渲染器:: 天空盒管线已登记（句柄 {}）", registered);
		return true;
	}

	//注入与读取管线缓存
	void Render_Resource::pipeline_cache_set(const uint8_t* data, std::size_t size)
	{
		(*state_ptr).pipeline.cache_set(data, size);
	}

	bool Render_Resource::pipeline_cache_get(std::vector<uint8_t>& bytes_out) const
	{
		return (*state_ptr).pipeline.cache_get(bytes_out);
	}

	//销毁图形管线及资源域持有的具名管线着色器
	void Render_Resource::pipeline_shutdown(void)
	{
		(*state_ptr).pipeline.destroy();
		for (auto& record : (*state_ptr).extra_pipelines)
		{
			if (record.vert_module != VK_NULL_HANDLE)
				(*state_ptr).shader.module_destroy(record.vert_module);
			if (record.frag_module != VK_NULL_HANDLE)
				(*state_ptr).shader.module_destroy(record.frag_module);
		}
		(*state_ptr).extra_pipelines.clear();
		(*state_ptr).shader.destroy();
	}

	//按帧槽绑定 Cluster 图形描述符
	bool Render_Resource::scene_cluster_bind(uint32_t slot, uint64_t light_buffer, uint64_t counts_buffer,
		uint64_t offsets_buffer, uint64_t list_buffer)
	{
		return (*state_ptr).pipeline.scene_cluster_bind(slot,
			reinterpret_cast<VkBuffer>(light_buffer), reinterpret_cast<VkBuffer>(counts_buffer),
			reinterpret_cast<VkBuffer>(offsets_buffer), reinterpret_cast<VkBuffer>(list_buffer));
	}

	//按帧槽绑定蒙皮调色板存储缓冲
	bool Render_Resource::scene_palette_bind(uint32_t slot, uint64_t palette_buffer)
	{
		return (*state_ptr).pipeline.palette_bind(slot, reinterpret_cast<VkBuffer>(palette_buffer));
	}

	//每帧槽调色板容量
	uint32_t Render_Resource::animation_palette_capacity(void) const
	{
		return ANIMATION_PALETTE_CAPACITY;
	}

	//创建一条帧槽调色板缓冲
	bool Render_Resource::animation_palette_resource_create(uint32_t slot, uint64_t& buffer_out)
	{
		buffer_out = 0;
		//内存模块须可用
		if (!core->memory.valid())
			return false;
		//按帧槽序创建（只允许追加到末尾）
		if (slot > (*state_ptr).palette_buffers.size())
			return false;
		if (slot == (*state_ptr).palette_buffers.size())
		{
			(*state_ptr).palette_buffers.push_back(VK_NULL_HANDLE);
			(*state_ptr).palette_allocations.push_back(VK_NULL_HANDLE);
		}
		//创建主机可见存储缓冲
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		const VkDeviceSize bytes = static_cast<VkDeviceSize>(ANIMATION_PALETTE_CAPACITY) * ANIMATION_PALETTE_MATRIX_BYTES;
		if (!core->memory.buffer_create(buffer, allocation, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
			return false;
		//写入单位矩阵初值
		std::vector<float> initial(static_cast<std::size_t>(ANIMATION_PALETTE_CAPACITY) * ANIMATION_PALETTE_MATRIX_FLOATS,
			0.0f);
		palette_identity_fill(initial.data(), ANIMATION_PALETTE_CAPACITY);
		if (!core->memory.buffer_write(buffer, allocation, initial.data(), bytes, 0))
		{
			core->memory.buffer_destroy(buffer, allocation);
			return false;
		}
		(*state_ptr).palette_buffers[slot] = buffer;
		(*state_ptr).palette_allocations[slot] = allocation;
		buffer_out = reinterpret_cast<uint64_t>(buffer);
		return true;
	}

	//归还全部调色板缓冲
	void Render_Resource::animation_palette_resource_destroy(void)
	{
		//依赖内存模块，须先于 memory 销毁
		if (core->memory.valid())
			for (std::size_t slot = 0; slot < (*state_ptr).palette_buffers.size(); ++slot)
				//归还有效缓冲
				if ((*state_ptr).palette_buffers[slot] != VK_NULL_HANDLE)
					core->memory.buffer_destroy((*state_ptr).palette_buffers[slot],
						(*state_ptr).palette_allocations[slot]);
		(*state_ptr).palette_buffers.clear();
		(*state_ptr).palette_allocations.clear();
	}

	//蒙皮调色板写入
	bool Render_Resource::animation_palette_upload(const float* matrices, uint32_t count, uint32_t offset)
	{
		//空操作
		if (count == 0)
			return true;
		//空指针或越界整体拒绝
		if (matrices == nullptr || offset > ANIMATION_PALETTE_CAPACITY
			|| count > ANIMATION_PALETTE_CAPACITY - offset)
			return false;
		//缓冲未建立
		if (!core->memory.valid() || (*state_ptr).palette_buffers.empty())
			return false;
		const VkDeviceSize byte_offset = static_cast<VkDeviceSize>(offset) * ANIMATION_PALETTE_MATRIX_BYTES;
		const VkDeviceSize byte_size = static_cast<VkDeviceSize>(count) * ANIMATION_PALETTE_MATRIX_BYTES;
		//逐帧槽写入相同内容
		for (std::size_t slot = 0; slot < (*state_ptr).palette_buffers.size(); ++slot)
			if (!core->memory.buffer_write((*state_ptr).palette_buffers[slot],
				(*state_ptr).palette_allocations[slot], matrices, byte_size, byte_offset))
				return false;
		return true;
	}

	//蒙皮调色板读回
	bool Render_Resource::animation_palette_readback(float* matrices, uint32_t count, uint32_t offset)
	{
		//空操作
		if (count == 0)
			return true;
		//空指针或越界整体拒绝
		if (matrices == nullptr || offset > ANIMATION_PALETTE_CAPACITY
			|| count > ANIMATION_PALETTE_CAPACITY - offset)
			return false;
		//缓冲未建立
		if (!core->memory.valid() || (*state_ptr).palette_buffers.empty())
			return false;
		//首帧槽读回（各帧槽由写入接口同步更新）
		return core->memory.buffer_read((*state_ptr).palette_buffers[0], (*state_ptr).palette_allocations[0],
			matrices, static_cast<VkDeviceSize>(count) * ANIMATION_PALETTE_MATRIX_BYTES,
			static_cast<VkDeviceSize>(offset) * ANIMATION_PALETTE_MATRIX_BYTES);
	}

	//演员调色板区间分配
	bool Render_Resource::animation_palette_allocate(uint32_t bone_count, uint32_t& offset_out)
	{
		offset_out = 0;
		//骨骼数须在单演员上限内
		if (bone_count == 0 || bone_count > ANIMATION_ACTOR_BONES_MAX)
			return false;
		//区间分配
		const uint32_t offset = (*state_ptr).palette_pool.allocate(bone_count);
		if (offset == UINT32_MAX)
			return false;
		//新区间写入单位矩阵（新演员从绑定姿态开始）
		std::vector<float> identity(static_cast<std::size_t>(bone_count) * ANIMATION_PALETTE_MATRIX_FLOATS, 0.0f);
		palette_identity_fill(identity.data(), bone_count);
		if (!animation_palette_upload(identity.data(), bone_count, offset))
		{
			(*state_ptr).palette_pool.release(offset);
			return false;
		}
		offset_out = offset;
		return true;
	}

	//演员调色板区间回收
	bool Render_Resource::animation_palette_release(uint32_t offset)
	{
		uint32_t count = 0;
		if (!(*state_ptr).palette_pool.release(offset, &count))
			return false;
		//区间复位为单位矩阵，避免后续演员读到旧数据
		std::vector<float> identity(static_cast<std::size_t>(count) * ANIMATION_PALETTE_MATRIX_FLOATS, 0.0f);
		palette_identity_fill(identity.data(), count);
		animation_palette_upload(identity.data(), count, offset);
		return true;
	}
}
