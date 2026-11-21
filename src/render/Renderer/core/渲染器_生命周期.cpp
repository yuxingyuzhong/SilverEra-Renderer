#include "../局部命名空间使用.h"
//六域完整类型与核心上下文
#include "渲染器内部状态.h"

namespace engine
{
	//门面构造（先建设备域，再向其余五域注入核心上下文）
	Renderer::Renderer(void)
		: device_domain(new(std::nothrow) Render_Device(this))
	{
		//核心模块分配失败时保持空门面
		if (device_domain == nullptr)
			return;
		Render_Core_Context* core = device_domain->core_context();
		if (core == nullptr)
			return;
		resource_domain.reset(new(std::nothrow) Render_Resource(this, *core));
		compute_domain.reset(new(std::nothrow) Render_Compute(this, *core));
		scene_domain.reset(new(std::nothrow) Render_Scene(this, *core));
		particle_domain.reset(new(std::nothrow) Render_Particle(this, *core));
		ui_domain.reset(new(std::nothrow) Render_Ui(this, *core));
		//动画门面（方向十 · 收敛轮 861；不持有 GPU 资源）
		animation_domain.reset(new(std::nothrow) Render_Animation(this));
	}

	//门面析构（设备域统一按依赖顺序关闭）
	Renderer::~Renderer(void)
	{
		if (device_domain != nullptr)
			shutdown();
	}

	//界面域
	Render_Ui& Renderer::ui(void) { return *ui_domain; }
	//界面域（只读）
	const Render_Ui& Renderer::ui(void) const { return *ui_domain; }
	//粒子域
	Render_Particle& Renderer::particle(void) { return *particle_domain; }
	//粒子域（只读）
	const Render_Particle& Renderer::particle(void) const { return *particle_domain; }
	//资源域
	Render_Resource& Renderer::resource(void) { return *resource_domain; }
	//资源域（只读）
	const Render_Resource& Renderer::resource(void) const { return *resource_domain; }
	//计算域
	Render_Compute& Renderer::compute(void) { return *compute_domain; }
	//计算域（只读）
	const Render_Compute& Renderer::compute(void) const { return *compute_domain; }
	//设备域
	Render_Device& Renderer::device(void) { return *device_domain; }
	//设备域（只读）
	const Render_Device& Renderer::device(void) const { return *device_domain; }
	//场景域
	Render_Scene& Renderer::scene(void) { return *scene_domain; }
	//场景域（只读）
	const Render_Scene& Renderer::scene(void) const { return *scene_domain; }
	//动画门面（方向十 · 收敛轮 861）
	Render_Animation& Renderer::animation(void) { return *animation_domain; }
	//动画门面（只读）
	const Render_Animation& Renderer::animation(void) const { return *animation_domain; }

	//初始化
	bool Renderer::init(const Renderer_Config& config) { return device().init(config); }
	//关闭
	void Renderer::shutdown(void) { device().shutdown(); }
	//是否已初始化
	bool Renderer::valid(void) const { return device().valid(); }
	//每帧推进
	bool Renderer::frame_render(uint8_t* readback_pixels, uint32_t readback_capacity, uint32_t* readback_width, uint32_t* readback_height) { return device().frame_render(readback_pixels, readback_capacity, readback_width, readback_height); }
	//窗口是否请求退出
	bool Renderer::close_requested(void) const { return device().close_requested(); }
	//已呈现帧数
	uint64_t Renderer::frame_count(void) const { return device().frame_count(); }
	//帧统计快照
	Frame_Stats Renderer::frame_stats_get(void) const { return device().frame_stats_get(); }
	//等待全部在途帧
	void Renderer::frame_wait_idle(void) { device().frame_wait_idle(); }
	//清零累计帧统计
	void Renderer::frame_stats_reset(void) { device().frame_stats_reset(); }
	//资源计数快照
	Renderer_Resource_Counts Renderer::resource_counts(void) const { return device().resource_counts(); }
	//累计场景上传字节
	uint64_t Renderer::scene_upload_bytes(void) const { return device().scene_upload_bytes(); }
	//累计实例上传字节
	uint64_t Renderer::instance_upload_bytes(void) const { return device().instance_upload_bytes(); }
	//读回阴影图
	bool Renderer::shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out) { return device().shadow_readback(pixels, capacity, width_out, height_out); }
	//设置补光
	void Renderer::light_fill_set(const float direction[3], const float color[3], float intensity, float specular) { device().light_fill_set(direction, color, intensity, specular); }

	//全屏切换
	bool Render_Device::window_fullscreen_set(bool fullscreen)
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->platform.fullscreen_set(fullscreen);
	}

	//当前是否处于全屏
	bool Render_Device::window_fullscreen(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return false;
		return state_ptr->platform.fullscreen_get();
	}

	//窗口显示缩放比例
	float Render_Device::display_scale(void) const
	{
		//若尚未初始化
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 1.0f;
		return state_ptr->platform.display_scale_get();
	}

	//是否已初始化
	bool Render_Device::valid(void) const
	{
		return state_ptr != nullptr && state_ptr->initialized;
	}

	//渲染层版本查询
	string renderer_version(void)
	{
		return "0.1.0";
	}
}
