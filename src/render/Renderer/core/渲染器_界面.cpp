#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

//游戏引擎命名空间
namespace engine
{
	//构造（绑定门面；创建界面域状态）
	Render_Ui::Render_Ui(Renderer* owner)
		: owner(owner), state_ptr(new(std::nothrow) Ui_State())
	{
	}

	//析构
	Render_Ui::~Render_Ui(void)
	{
	}

	//界面域状态（迁移期直取）
	Ui_State& Render_Ui::state(void)
	{
		return *state_ptr;
	}

	//界面域状态（迁移期直取，只读）
	const Ui_State& Render_Ui::state(void) const
	{
		return *state_ptr;
	}

	//UI 通道
	Ui_Channel_Context* Render_Ui::ui_channel(void)
	{
		//若状态未分配
		if (state_ptr == nullptr)
			return nullptr;
		return &state().ui_channel;
	}

	//原生 UI 上下文
	Ui_Context* Render_Ui::ui_context(void)
	{
		//若实现体未分配或未初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return nullptr;
		return &state().ui;
	}

	//帧输入聚合器
	Input_Aggregator* Render_Ui::input_aggregator(void)
	{
		//若状态未分配
		if (state_ptr == nullptr)
			return nullptr;
		return &state().input;
	}

	//系统输入终端
	Event_Terminal* Render_Ui::input_event_terminal(void)
	{
		//若状态未分配
		if (state_ptr == nullptr)
			return nullptr;
		return &state().ui_event.event_terminal;
	}

	//接入系统输入与 UI 事件
	bool Render_Ui::input_system_attach(void)
	{
		//前置：实现体存在且已初始化
		if (owner == nullptr || owner->impl == nullptr || !owner->impl->initialized)
			return false;
		//委托事件桥（登记订阅者 + 订阅焦点/文本/命令/文本语义）
		return state().ui_event.attach("Renderer_UI");
	}

	//退出系统输入与 UI 事件
	void Render_Ui::input_system_detach(void)
	{
		//状态未分配则忽略
		if (state_ptr == nullptr)
			return;
		state().ui_event.detach();
	}

	//是否已接入系统输入
	bool Render_Ui::input_system_attached(void) const
	{
		//状态未分配或未接入
		if (state_ptr == nullptr)
			return false;
		return state().ui_event.attached();
	}

	//取一条未消费的系统语义命令
	bool Render_Ui::ui_command_pop(std::string& command)
	{
		//状态未分配
		if (state_ptr == nullptr)
			return false;
		return state().ui_event.command_pop(command);
	}

	//取一条未消费的系统文本语义
	bool Render_Ui::ui_text_pop(std::string& text)
	{
		//状态未分配
		if (state_ptr == nullptr)
			return false;
		return state().ui_event.text_pop(text);
	}

	//取系统焦点状态（main / child / modal / text）
	bool Render_Ui::ui_focus_state_get(std::string& main, std::string& child,
		std::string& modal, bool& text) const
	{
		//状态未分配
		if (state_ptr == nullptr)
			return false;
		state().ui_event.focus_state_get(main, child, modal, text);
		return true;
	}

	//初始化阶段：UI 通道 / 事件桥接 / 原生 UI / 文字宿主装配
	void Render_Ui::init(const Renderer_Config& effective)
	{
		//状态或实现体缺失则无法装配
		if (state_ptr == nullptr || owner == nullptr || owner->impl == nullptr)
			return;
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		/*
		⑲ UI 通道
		向上层提供 ImGui Vulkan 后端所需的实例 / 设备 / 队列 / 窗口 / 图像信息与动态渲染口径；
		绘制回调由上层经 ui_channel() 另行设置（渲染层不代管 UI）。
		*/
		state().ui_channel.build(impl->device.instance_get(), impl->device.physical_device_get(),
			impl->device.device_get(), impl->device.graphics_family_get(), impl->device.graphics_queue_get(),
			impl->platform.window_get(), effective.frame_overlap, impl->swapchain.image_count_get(),
			impl->swapchain.format_get(), impl->swapchain.depth_format_get());
		/*
		把事件泵的 SDL 事件桥接到 UI 通道
		上层经 ui_channel()->event_callback_set 注册回调后，事件泵每帧逐个事件转交，
		供上层平台后端（imgui_impl_sdl3）处理键鼠与文本输入；未注册时该桥接为空操作。
		桥接不持有资源；解除见 shutdown。
		*/
		impl->platform.event_forward_set(
			[](const SDL_Event* event, void* user_data) {
				//界面域（先喂输入聚合器，再转交外部 UI 平台后端）
				Render_Ui* self = static_cast<Render_Ui*>(user_data);
				self->state().input.event_feed(event);
				self->state().ui_channel.event_dispatch(event);
			},
			this);
		/*
		原生 UI 装配（保留模式控件树 + UI 阶段绘制）
		管线惰性登记失败只告警（原生 UI 不绘制），不影响既有渲染路径；
		UI 上下文随渲染器生命周期存续，控件由上层经 ui_context() 创建。
		*/
		state().ui.build();
		ui_pipeline_ensure();
		if (!state().ui_render.build(impl->device.device_get(), impl->memory, impl->pipeline,
			impl->resource_domain->state().texture, state().font, state().ui_solid_pipeline, state().ui_text_pipeline,
			effective.frame_overlap))
			logger.warn("渲染器:: 原生 UI 渲染器装配失败（原生 UI 不绘制）");
		/*
		文字绘制宿主绑定（第四十七轮下沉）
		文字绘制子系统只持网格缓存与文字颜色；字形补烘之外的 Vulkan 侧动作
		（管线登记、管线 / 纹理绑定、网格创建与归还、绘制项登记、目标尺寸查询）经本组回调转交本门面。
		回调均为无捕获 lambda（转函数指针），以界面域作上下文；生命周期不超过 Renderer。
		*/
		{
			Text_Host text_host{};
			text_host.user = this;
			//确保文字管线就绪（懒登记：Alpha 混合档，与场景管线共享同一管线布局）
			text_host.pipeline_ensure = [](void* user) -> uint64_t
			{
				Render_Ui* self = static_cast<Render_Ui*>(user);
				//尚未初始化则无管线可登记
				if (self->owner == nullptr || self->owner->impl == nullptr || !self->owner->impl->initialized)
					return self->owner == nullptr || self->owner->impl == nullptr ? 0 : self->state().text.pipeline_get();
				//已登记则直接返回
				if (self->state().text.pipeline_get() != 0)
					return self->state().text.pipeline_get();
				uint64_t handle = 0;
				//登记失败保持 0（文字覆盖率写入按配置决定；仅 MSAA > 1× 时生效）
				if (!self->owner->resource().pipeline_load(handle, "文字.vert.spv", "文字.frag.spv", Blend_Mode::Alpha,
					self->owner->impl->config.text_alpha_to_coverage))
					return 0;
				self->state().text.pipeline_set(handle);
				return handle;
			};
			//绑定文字管线
			text_host.pipeline_bind = [](void* user, uint64_t pipeline)
			{
				static_cast<Render_Ui*>(user)->owner->resource().pipeline_bind_set(pipeline);
			};
			//绑定字形图集纹理
			text_host.texture_bind = [](void* user, uint64_t texture)
			{
				static_cast<Render_Ui*>(user)->owner->resource().texture_bind_set(texture);
			};
			//创建文字网格
			text_host.mesh_create = [](void* user, Mesh_Handle& mesh, const Mesh_Data& data) -> bool
			{
				return static_cast<Render_Ui*>(user)->owner->impl->resource_domain->state().mesh.mesh_create(mesh, data);
			};
			//归还文字网格（网格模块未就绪时只丢弃句柄）
			text_host.mesh_destroy = [](void* user, Mesh_Handle mesh)
			{
				Render_Ui* self = static_cast<Render_Ui*>(user);
				//若网格模块仍有效
				if (self->owner != nullptr && self->owner->impl != nullptr && self->owner->impl->resource_domain->state().mesh.valid())
					self->owner->impl->resource_domain->state().mesh.mesh_destroy(mesh);
			};
			//登记文字绘制项
			text_host.mesh_draw = [](void* user, Mesh_Handle mesh, const float* transform, const float* color) -> bool
			{
				return static_cast<Render_Ui*>(user)->owner->resource().mesh_draw(mesh, transform, color);
			};
			//取当前绘制目标尺寸（像素）
			text_host.extent_get = [](void* user, float& width, float& height)
			{
				Render_Ui* self = static_cast<Render_Ui*>(user);
				width = static_cast<float>(self->owner->extent_width());
				height = static_cast<float>(self->owner->extent_height());
			};
			//绑定字体模块与宿主回调
			state().text.init(state().font, text_host);
		}
	}

	//关闭（界面域资源；幂等）
	void Render_Ui::shutdown(void)
	{
		//状态未分配则无内容可清理
		if (state_ptr == nullptr)
			return;
		/*
		原生 UI 与输入先于依赖模块销毁
		ui_render 持有每帧槽缓冲（依赖内存模块）；ui / input 只持有主机侧状态，
		但文本输入需在平台视频子系统仍存活时关闭。
		*/
		state().ui_render.shutdown();
		state().ui.shutdown();
		state().input.reset();
		state().ui_solid_pipeline = 0;
		state().ui_text_pipeline = 0;
		state().ui_pipeline_ready = false;
		//退出系统输入（须在中转站仍存活时发送退出事件）
		state().ui_event.detach();
		//文本网格缓存依赖网格模块，须先归还网格（含管线句柄与文字颜色复位）
		state().text.shutdown();
		//字体图集纹理依赖纹理模块，须先归还
		state().font.destroy();
		//UI 通道仅清句柄与回调（不拥有资源）
		state().ui_channel.destroy();
	}

	//确保 UI 管线（纯色 / 文字）就绪（惰性登记；失败返回假且不留下半登记状态）
	bool Render_Ui::ui_pipeline_ensure(void)
	{
		//状态或实现体缺失则无法登记
		if (state_ptr == nullptr || owner == nullptr || owner->impl == nullptr)
			return false;
		//已尝试过则直接返回结果
		if (state().ui_pipeline_ready)
			return state().ui_solid_pipeline != 0 && state().ui_text_pipeline != 0;
		state().ui_pipeline_ready = true;

		//登记纯色管线
		uint64_t solid = 0;
		if (owner->impl->pipeline_load_ex(solid, "UI纯色.vert.spv", "UI纯色.frag.spv", Blend_Mode::Alpha,
			false, true, false, false, false, true, VK_FORMAT_UNDEFINED, 1))
			state().ui_solid_pipeline = solid;
		else
			logger.warn("渲染器:: UI 纯色管线登记失败（原生 UI 只绘制文字）");

		//登记文字管线
		uint64_t text = 0;
		if (owner->impl->pipeline_load_ex(text, "UI文字.vert.spv", "UI文字.frag.spv", Blend_Mode::Alpha,
			false, true, false, false, false, true, VK_FORMAT_UNDEFINED, 1))
			state().ui_text_pipeline = text;
		else
			logger.warn("渲染器:: UI 文字管线登记失败（原生 UI 只绘制纯色）");

		return state().ui_solid_pipeline != 0 && state().ui_text_pipeline != 0;
	}

	/*
	UI 阶段绘制入口（帧模块以 C 函数指针调用）
	先绘制原生 UI，再把同一命令缓冲交给外部 UI 回调（ImGui 等平台后端），最后一个绘制者在最上层。
	*/
	void Render_Ui::ui_draw_entry(void* command_buffer_raw, void* user_data)
	{
		//命令缓冲按 Vulkan 句柄还原（公共头不引入 Vulkan 类型）
		VkCommandBuffer command_buffer = static_cast<VkCommandBuffer>(command_buffer_raw);
		//用户数据为界面域
		Render_Ui* self = static_cast<Render_Ui*>(user_data);
		//空域 / 空命令缓冲 / 实现体缺失直接返回
		if (self == nullptr || command_buffer == VK_NULL_HANDLE || self->owner == nullptr || self->owner->impl == nullptr)
			return;
		/*
		原生 UI 绘制
		目标尺寸取交换链尺寸：UI 阶段恒在最终呈现图像上绘制（SSAA / 后处理链均不吃 UI）。
		*/
		if (self->state().ui_render.valid())
		{
			const VkExtent2D extent = self->owner->impl->swapchain.extent_get();
			const uint32_t slot = self->owner->impl->frame.slot_index_get();
			self->state().ui_render.draw(self->state().ui, command_buffer, extent, slot);
		}
		//外部 UI 回调（未注册时为空操作）
		if (const Frame_Ui_Callback* external = self->state().ui_channel.draw_callback_get())
			external->fn(command_buffer, external->user_data);
	}
}
