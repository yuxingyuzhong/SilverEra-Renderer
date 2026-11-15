#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//内部完整类型
#include "渲染器内部状态.h"

namespace engine
{
	uint32_t msaa_supported_max(VkPhysicalDevice physical_device, VkFormat color_format,
		VkFormat depth_format, uint32_t requested);
	void spot_shadow_tiles_ensure_impl(Device_State& state, std::size_t max_tiles);
	void spot_shadow_atlas_apply_impl(Device_State& state);

	struct Frame_Dispatch_Input
	{
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t slot = 0;
		bool rebuilt = false;
		bool post_frame = false;
		bool post_chain_mode = false;
		bool ssaa_frame = false;
		VkDeviceSize post_instance_offset = 0;
		VkDeviceSize ssaa_instance_offset = 0;
		const float* view_proj = nullptr;
		Frame_Ui_Callback ui_callback{};
		uint8_t* readback_pixels = nullptr;
		uint32_t readback_capacity = 0;
		uint32_t* readback_width = nullptr;
		uint32_t* readback_height = nullptr;
	};
	bool renderer_init_graphics(Renderer* owner, Device_State& state, const Renderer_Config& effective);
	bool renderer_init_platform(Renderer* owner, Device_State& state, Renderer_Config& effective);
	bool renderer_init_memory_swapchain(Renderer* owner, Device_State& state, Renderer_Config& effective);
	bool renderer_init_frame_shader_pipeline(Renderer* owner, Device_State& state, Renderer_Config& effective);
	bool renderer_init_services(Renderer* owner, Device_State& state, Renderer_Config& effective);
	bool renderer_init_ui(Renderer* owner, Device_State& state, Renderer_Config& effective);
}
