#pragma once
//公共语义类型（配置 / 统计 / 句柄 / 前置声明）
#include "渲染器类型.h"
//共享实现体持有基类
#include "渲染器实现访问.h"
//界面域
#include "渲染界面.h"
//粒子域
#include "渲染粒子.h"
//资源域
#include "渲染资源.h"
//计算域
#include "渲染计算.h"
//设备域
#include "渲染设备.h"
//场景域
#include "渲染场景.h"
//各能力接口
#include "渲染器_接口_核心.h"
#include "渲染器_接口_输出.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器（门面）
	生命周期：init → frame_render × N → shutdown
	状态机：未初始化 → 已初始化 → 已销毁（shutdown 幂等，可重复调用）
	非线程安全：要求所有公开 API（init / shutdown / frame_render / mesh_draw / scene_apply / 各查询与 setter）
	来自同一线程（见 §4.5）。
	说明（第六百四十四轮）：本层的「多线程渲染」指**内部并行**——初始化时按 `render_threads` 启动自己的工作线程池，
	由它们并行执行实例级视锥剔除、实例缓冲差异扫描，并把场景 / 阴影 pass 的绘制命令录制进**次级命令缓冲**，
	最后由调用线程在主命令缓冲里以 `vkCmdExecuteCommands` 汇总提交。
	工作线程不对外暴露、也不接受外部调用；Vulkan 对象（命令池 / 缓冲 / 描述符集）的创建、提交与呈现仍全部在调用线程上完成。
	故「内部多线程」与「对外线程安全」是两件事：不要从多个线程并发调用本类接口，也不要与外部自行提交共用同一队列。

	实现组织：生命周期与输出能力由接口组合，场景 / 光照 / 风格等其余能力由独立域对象承载；
	所有接口虚继承 Renderer_Impl_Holder，故整个门面共享同一份实现体指针。
	*/
	class Renderer : public Renderer_Core, public Renderer_Output
	{
	public:
		//构造（绑定门面自身指针；实现体由共享基类创建）
		Renderer(void);
		//析构（域对象为不完整类型，须在 .cpp 定义）
		~Renderer(void);

		//界面域
		Render_Ui& ui(void);
		const Render_Ui& ui(void) const;

		//粒子域
		Render_Particle& particle(void);
		const Render_Particle& particle(void) const;

		//资源域
		Render_Resource& resource(void);
		const Render_Resource& resource(void) const;

		//计算域
		Render_Compute& compute(void);
		const Render_Compute& compute(void) const;

		//设备域
		Render_Device& device(void);
		const Render_Device& device(void) const;

		//场景域
		Render_Scene& scene(void);
		const Render_Scene& scene(void) const;

	private:
		//界面域对象
		std::unique_ptr<Render_Ui> ui_domain;
		//粒子域对象
		std::unique_ptr<Render_Particle> particle_domain;
		//资源域对象
		std::unique_ptr<Render_Resource> resource_domain;
		//计算域对象
		std::unique_ptr<Render_Compute> compute_domain;
		//设备域对象
		std::unique_ptr<Render_Device> device_domain;
		//场景域对象
		std::unique_ptr<Render_Scene> scene_domain;
	};
}
