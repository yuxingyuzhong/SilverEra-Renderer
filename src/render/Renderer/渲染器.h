#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//公共语义类型与六域完整接口
#include "渲染器类型.h"
#include "渲染设备.h"
#include "渲染资源.h"
#include "渲染计算.h"
#include "渲染场景.h"
#include "渲染粒子.h"
#include "渲染界面.h"

//游戏引擎命名空间
namespace engine
{
	class Render_Resource;
	class Render_Compute;
	class Render_Scene;
	class Render_Particle;
	class Render_Ui;
	class Render_Device;

	//渲染器根门面（六域容器与生命周期入口）
	class Renderer
	{
	public:
		Renderer(void);
		~Renderer(void);
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		Render_Ui& ui(void);
		const Render_Ui& ui(void) const;
		Render_Particle& particle(void);
		const Render_Particle& particle(void) const;
		Render_Resource& resource(void);
		const Render_Resource& resource(void) const;
		Render_Compute& compute(void);
		const Render_Compute& compute(void) const;
		Render_Device& device(void);
		const Render_Device& device(void) const;
		Render_Scene& scene(void);
		const Render_Scene& scene(void) const;

		bool init(const Renderer_Config& config);
		void shutdown(void);
		bool valid(void) const;
		bool frame_render(uint8_t* readback_pixels = nullptr, uint32_t readback_capacity = 0,
			uint32_t* readback_width = nullptr, uint32_t* readback_height = nullptr);
		bool close_requested(void) const;
		uint64_t frame_count(void) const;
		Frame_Stats frame_stats_get(void) const;
		void frame_wait_idle(void);
		void frame_stats_reset(void);
		Renderer_Resource_Counts resource_counts(void) const;
		uint64_t scene_upload_bytes(void) const;
		uint64_t instance_upload_bytes(void) const;
		bool shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out);
		void light_fill_set(const float direction[3], const float color[3], float intensity, float specular = 1.0f);

	private:
		std::unique_ptr<Render_Device> device_domain;
		std::unique_ptr<Render_Resource> resource_domain;
		std::unique_ptr<Render_Compute> compute_domain;
		std::unique_ptr<Render_Scene> scene_domain;
		std::unique_ptr<Render_Particle> particle_domain;
		std::unique_ptr<Render_Ui> ui_domain;
	};
}
