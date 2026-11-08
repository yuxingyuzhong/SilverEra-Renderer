#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//UI 通道声明
#include "src/render/Renderer/渲染器_UI通道.h"

namespace engine
{
	//析构（内部调用 destroy，保证无残留）
	Ui_Channel_Context::~Ui_Channel_Context(void)
	{
		destroy();
	}

	//配置通道
	void Ui_Channel_Context::build(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
		uint32_t graphics_family, VkQueue graphics_queue,
		SDL_Window* window, uint32_t min_image_count, uint32_t image_count, VkFormat color_format,
		VkFormat depth_format)
	{
		//填充通道信息
		info.instance = instance;
		info.physical_device = physical_device;
		info.device = device;
		info.graphics_family = graphics_family;
		info.graphics_queue = graphics_queue;
		info.window = window;
		info.min_image_count = min_image_count;
		info.image_count = image_count;
		info.color_format = color_format;
		info.depth_format = depth_format;
		info.use_dynamic_rendering = true;
		//动态渲染信息（颜色附件 = 交换链格式；深度附件 = 交换链深度格式，使 UI 管线与场景通道相容；模板不参与）
		info.pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		info.pipeline_rendering.colorAttachmentCount = 1;
		info.pipeline_rendering.pColorAttachmentFormats = &info.color_format;
		info.pipeline_rendering.depthAttachmentFormat = depth_format;
		info.pipeline_rendering.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
		//若最小图像数不足 2（imgui 要求）
		if (min_image_count < 2)
			logger.warn("渲染器:: UI 通道最小图像数 {} 低于 imgui 要求的 2，建议把在途帧数设为 2 或 3", min_image_count);
		//标记已配置
		built = true;
	}

	//销毁（幂等）
	void Ui_Channel_Context::destroy(void)
	{
		//复位信息与回调
		info = Ui_Channel_Info{};
		callback = Frame_Ui_Callback{};
		event_callback = Ui_Event_Callback{};
		built = false;
	}

	//是否已配置完成
	bool Ui_Channel_Context::valid(void) const
	{
		return built && info.device != VK_NULL_HANDLE;
	}

	//取通道信息
	const Ui_Channel_Info& Ui_Channel_Context::info_get(void) const
	{
		return info;
	}

	//设置每帧 UI 绘制回调
	void Ui_Channel_Context::draw_callback_set(void (*callback_fn)(VkCommandBuffer command_buffer, void* user_data), void* user_data)
	{
		//记录回调
		callback.fn = callback_fn;
		callback.user_data = user_data;
	}

	//清除每帧 UI 绘制回调
	void Ui_Channel_Context::draw_callback_clear(void)
	{
		//置空回调
		callback = Frame_Ui_Callback{};
	}

	//是否设置了绘制回调
	bool Ui_Channel_Context::draw_callback_active(void) const
	{
		return callback.fn != nullptr;
	}

	//取当前绘制回调
	const Frame_Ui_Callback* Ui_Channel_Context::draw_callback_get(void) const
	{
		//若未设置则返回空
		if (callback.fn == nullptr)
			return nullptr;
		return &callback;
	}

	//设置 SDL 事件转发回调
	void Ui_Channel_Context::event_callback_set(void (*callback_fn)(const SDL_Event* event, void* user_data), void* user_data)
	{
		//记录回调与用户数据
		event_callback.fn = callback_fn;
		event_callback.user_data = user_data;
	}

	//清除 SDL 事件转发回调
	void Ui_Channel_Context::event_callback_clear(void)
	{
		//置空回调
		event_callback = Ui_Event_Callback{};
	}

	//是否已设置 SDL 事件转发回调
	bool Ui_Channel_Context::event_callback_active(void) const
	{
		return event_callback.fn != nullptr;
	}

	//事件转发（未设置回调则不做任何事）
	void Ui_Channel_Context::event_dispatch(const SDL_Event* event)
	{
		//若已设置回调则转发该事件
		if (event_callback.fn != nullptr)
			event_callback.fn(event, event_callback.user_data);
	}
}