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

	Renderer& Renderer_Impl_Holder::self(void) const
	{
		return *self_ptr;
	}

	//门面构造（绑定自身指针并创建界面域、粒子域与资源域；实现体由共享基类构造）
	Renderer::Renderer(void)
		: ui_domain(new(std::nothrow) Render_Ui(this)),
		particle_domain(new(std::nothrow) Render_Particle(this)),
		resource_domain(new(std::nothrow) Render_Resource(this))
	{
		self_ptr = this;
		//登记域对象（关闭流程据此判断域对象是否仍可访问）
		if (impl != nullptr)
		{
			impl->ui_domain = ui_domain.get();
			impl->particle_domain = particle_domain.get();
			impl->resource_domain = resource_domain.get();
		}
	}

	/*
	门面析构
	域状态依赖实现体各模块（内存 / 网格 / 纹理），须在门面成员析构前关闭；
	关闭后把实现体登记置空，基类析构的关闭流程据此跳过对应域。
	*/
	Renderer::~Renderer(void)
	{
		if (particle_domain != nullptr)
			particle_domain->shutdown();
		if (ui_domain != nullptr)
			ui_domain->shutdown();
		if (resource_domain != nullptr)
			resource_domain->shutdown();
		if (impl != nullptr)
		{
			impl->particle_domain = nullptr;
			impl->ui_domain = nullptr;
			impl->resource_domain = nullptr;
		}
	}

	//界面域
	Render_Ui& Renderer::ui(void)
	{
		return *ui_domain;
	}

	//界面域（只读）
	const Render_Ui& Renderer::ui(void) const
	{
		return *ui_domain;
	}

	//粒子域
	Render_Particle& Renderer::particle(void)
	{
		return *particle_domain;
	}

	//粒子域（只读）
	const Render_Particle& Renderer::particle(void) const
	{
		return *particle_domain;
	}

	//资源域
	Render_Resource& Renderer::resource(void)
	{
		return *resource_domain;
	}

	//资源域（只读）
	const Render_Resource& Renderer::resource(void) const
	{
		return *resource_domain;
	}

	//构造（不创建任何 GPU 资源）
	Renderer_Impl_Holder::Renderer_Impl_Holder(void)
		: impl(new(std::nothrow) Impl())
	{
	}

	//析构（内部调用 shutdown，保证资源不泄漏）
	Renderer_Impl_Holder::~Renderer_Impl_Holder(void)
	{
		self().shutdown();
	}

	//初始化


	//全屏切换（第一百一十一轮）
	bool Renderer_Output::window_fullscreen_set(bool fullscreen)
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->platform.fullscreen_set(fullscreen);
	}

	//当前是否处于全屏（第一百一十一轮）
	bool Renderer_Output::window_fullscreen(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return false;
		return impl->platform.fullscreen_get();
	}

	//窗口显示缩放比例（第一百一十二轮）
	float Renderer_Output::display_scale(void) const
	{
		//若尚未初始化
		if (impl == nullptr || !impl->initialized)
			return 1.0f;
		return impl->platform.display_scale_get();
	}

	//是否已初始化
	bool Renderer_Core::valid(void) const
	{
		return impl != nullptr && impl->initialized;
	}

	//渲染层版本查询
	string renderer_version(void)
	{
		//渲染层版本号
		return "0.1.0";
	}
}
