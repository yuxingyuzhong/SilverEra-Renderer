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
	namespace
	{
		//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；第一百零六轮）
		std::filesystem::path path_from_utf8(const std::string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}
	}

	//取门面自身

	//初始化（任一环节失败即逆序回退）

	//初始化阶段：UI 通道 / 事件桥接 / 原生 UI
	bool renderer_init_ui(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective)
	{
		/*
		⑲ UI 通道
		向上层提供 ImGui Vulkan 后端所需的实例 / 设备 / 队列 / 窗口 / 图像信息与动态渲染口径；
		绘制回调由上层经 owner->ui_channel() 另行设置（渲染层不代管 UI）。
		*/
		impl->ui_channel.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->platform.window_get(), effective.frame_overlap, impl->swapchain.image_count_get(),
		impl->swapchain.format_get(), impl->swapchain.depth_format_get());
		/*
		把事件泵的 SDL 事件桥接到 UI 通道
		上层经 owner->ui_channel()->event_callback_set 注册回调后，事件泵每帧逐个事件转交，
		供上层平台后端（imgui_impl_sdl3）处理键鼠与文本输入；未注册时该桥接为空操作。
		桥接不持有资源；解除见 shutdown。
		*/
		impl->platform.event_forward_set(
			[](const SDL_Event* event, void* user_data) {
				//本实现体（先喂输入聚合器，再转交外部 UI 平台后端）
				Renderer_Impl_Holder::Impl* self = static_cast<Renderer_Impl_Holder::Impl*>(user_data);
				self->input.event_feed(event);
				self->ui_channel.event_dispatch(event);
			},
			impl);

		/*
		原生 UI 装配（保留模式控件树 + UI 阶段绘制）
		管线惰性登记失败只告警（原生 UI 不绘制），不影响既有渲染路径；
		UI 上下文随渲染器生命周期存续，控件由上层经 owner->ui_context() 创建。
		*/
		impl->ui.build();
		impl->ui_pipeline_ensure();
		if (!impl->ui_render.build(impl->device.device_get(), impl->memory, impl->pipeline,
			impl->texture, impl->font, impl->ui_solid_pipeline, impl->ui_text_pipeline,
			effective.frame_overlap))
			logger.warn("渲染器:: 原生 UI 渲染器装配失败（原生 UI 不绘制）");
		return true;
	}
}
