#pragma once
//公共语义类型（配置 / 统计 / 句柄 / 前置声明）
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器实现体持有基类
	所有能力接口虚继承本类，故整个门面共享同一份实现体指针；
	构造 / 析构在 .cpp 内定义，保证层外看不到 Impl 完整类型。
	*/
	class Renderer_Impl_Holder
	{
	public:
		//实现体（内部细节；定义见 core/渲染器实现.h）
		class Impl;

		//构造（创建实现体）
		Renderer_Impl_Holder(void);
		//析构（内部调用 shutdown）
		~Renderer_Impl_Holder(void);
		//不可拷贝、不可移动
		Renderer_Impl_Holder(const Renderer_Impl_Holder&) = delete;
		Renderer_Impl_Holder& operator=(const Renderer_Impl_Holder&) = delete;

	protected:
		//实现体指针（虚继承下全门面唯一）
		std::unique_ptr<Impl> impl;
		//门面自身指针（由 Renderer 构造时绑定，供接口间互调）
		Renderer* self_ptr = nullptr;
		//取门面自身
		Renderer& self(void) const;

		//初始化阶段函数（自由函数形式；回调用到 impl，故授予访问权）
		friend bool renderer_init_platform(Renderer*, Impl*, Renderer_Config&);
		friend bool renderer_init_memory_swapchain(Renderer*, Impl*, Renderer_Config&);
		friend bool renderer_init_frame_shader_pipeline(Renderer*, Impl*, Renderer_Config&);
		friend bool renderer_init_services(Renderer*, Impl*, Renderer_Config&);
		friend bool renderer_init_ui(Renderer*, Impl*, Renderer_Config&);
		friend bool renderer_init_graphics(Renderer*, Impl*, const Renderer_Config&);

		//层内 GPU 粒子推进（实现位于 src/render/Particle/粒子GPU推进.cpp）
		friend uint32_t particle_gpu::emit(Renderer&, uint32_t);
		friend bool particle_gpu::step_resident(Renderer&, float);
	};
}