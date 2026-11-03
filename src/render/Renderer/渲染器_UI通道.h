#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属渲染层「公共扩展头」：含 Vulkan / SDL3 类型，供上层（Test 层）接入
ImGui Vulkan 后端使用；不并入 渲染器.h（公共主头仍保持零 Vulkan / SDL 类型）。
上层需先经「对外接口.cmake 的公共包含根」取得 Vulkan SDK 与 SDL3 公共头，方可包含本头。
*/
#include <vulkan/vulkan.h>
//SDL3 窗口类型（平台后端绑定输入事件需要）
#include <SDL3/SDL.h>
//帧绘制回调（帧链路在场景 pass 结束后调用）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Frame/帧上下文.h"

//游戏引擎命名空间
namespace engine
{
	/*
	UI 通道信息
	字段与 imgui_impl_vulkan 的 ImGui_ImplVulkan_InitInfo 对齐（但不 include imgui），
	上层将其填充进 ImGui_ImplVulkan_InitInfo 后调用 ImGui_ImplVulkan_Init。
	本结构由渲染层持有，生命周期 = 渲染器 init 之后至 shutdown 之前。
	*/
	struct Ui_Channel_Info
	{
		//应用 API 版本（渲染层按 Vulkan 1.3 构建）
		uint32_t api_version = VK_API_VERSION_1_3;
		//实例
		VkInstance instance = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//渲染层自建的 SDL3 窗口（供平台后端绑定输入事件）
		SDL_Window* window = nullptr;
		//最小图像数（= 在途帧数；imgui 要求 >= 2，默认配置为 2）
		uint32_t min_image_count = 2;
		//交换链图像数
		uint32_t image_count = 0;
		//颜色附件格式（交换链格式）
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		//深度附件格式（交换链深度附件；UI 与场景共用同一渲染通道，ImGui 管线须与本格式相容）
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//使用动态渲染（渲染层帧链路为动态渲染，后端须同步开启）
		bool use_dynamic_rendering = true;
		//动态渲染信息（pColorAttachmentFormats 指向本结构的 color_format）
		VkPipelineRenderingCreateInfo pipeline_rendering{};
	};

	/*
	UI 事件回调
	渲染层每帧事件泵逐个 SDL 事件转发给上层（上层据此驱动 imgui_impl_sdl3 等平台后端）。
	*/
	struct Ui_Event_Callback
	{
		//事件转发回调
		void (*fn)(const SDL_Event* event, void* user_data) = nullptr;
		//用户数据
		void* user_data = nullptr;
	};

	/*
	UI 通道
	向上层提供「接入 ImGui Vulkan 后端」所需的通道信息与每帧绘制时机。
	不 include imgui、不持有 ImGui 上下文 —— 渲染层不代管 UI。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	*/
	class Ui_Channel_Context
	{
	public:
		//构造（不创建任何 Vulkan / SDL 对象）
		Ui_Channel_Context(void) = default;
		//析构（内部调用 destroy，保证无残留）
		~Ui_Channel_Context(void);
		//不可拷贝、不可移动
		Ui_Channel_Context(const Ui_Channel_Context&) = delete;
		Ui_Channel_Context& operator=(const Ui_Channel_Context&) = delete;

		/*
		配置通道（渲染器 init 末尾调用）
		window：渲染层自建窗口；min_image_count：在途帧数；
		image_count：交换链图像数；color_format：交换链格式。
		*/
		void build(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
			uint32_t graphics_family, VkQueue graphics_queue,
			SDL_Window* window, uint32_t min_image_count, uint32_t image_count, VkFormat color_format,
			VkFormat depth_format);
		//销毁（幂等；仅清句柄与回调，不拥有资源）
		void destroy(void);
		//是否已配置完成
		bool valid(void) const;
		//取通道信息（供上层填充 ImGui_ImplVulkan_InitInfo）
		const Ui_Channel_Info& info_get(void) const;
		//设置每帧 UI 绘制回调（在场景渲染 pass 结束后、呈现转换前调用；传入本帧命令缓冲）
		void draw_callback_set(void (*callback)(VkCommandBuffer command_buffer, void* user_data), void* user_data);
		//清除每帧 UI 绘制回调
		void draw_callback_clear(void);
		//是否设置了绘制回调
		bool draw_callback_active(void) const;
		//取当前绘制回调（帧链路每帧查询；未设置时返回空）
		const Frame_Ui_Callback* draw_callback_get(void) const;
		//设置 SDL 事件转发回调（供上层驱动 UI 平台后端处理输入）
		void event_callback_set(void (*callback)(const SDL_Event* event, void* user_data), void* user_data);
		//清除 SDL 事件转发回调
		void event_callback_clear(void);
		//是否已设置 SDL 事件转发回调
		bool event_callback_active(void) const;
		//事件转发（事件泵桥接调用；未设置则不做任何事）
		void event_dispatch(const SDL_Event* event);
	private:
		//通道信息
		Ui_Channel_Info info;
		//绘制回调（fn 为空表示未设置）
		Frame_Ui_Callback callback;
		//事件转发回调（fn 为空表示未设置）
		Ui_Event_Callback event_callback;
		//是否已配置
		bool built = false;
	};
}
