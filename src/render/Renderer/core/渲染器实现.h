#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
各模块对外头
本头属本层「实现细节」：实现体持有各模块实例，故需其完整类型；
不进公共头，故可以合法暴露 Vulkan / SDL3 类型（层外仍看不到）。
*/
#include "Application/Renderer/src/render/Platform/平台上下文.h"
#include "Application/Renderer/src/render/Device/设备上下文.h"
#include "Application/Renderer/src/render/Memory/内存上下文.h"
#include "Application/Renderer/src/render/Swapchain/交换链.h"
#include "Application/Renderer/src/render/Frame/帧上下文.h"
#include "Application/Renderer/src/render/Shader/着色器.h"
#include "Application/Renderer/src/render/Pipeline/管线.h"
#include "Application/Renderer/src/render/Mesh/网格.h"
#include "Application/Renderer/src/render/Texture/纹理.h"
#include "Application/Renderer/src/render/Scene/相机.h"
//门面声明（Renderer::Impl 为其私有嵌套类）
#include "Application/Renderer/src/render/Renderer/渲染器.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器实现体
	持有各模块的实例并按依赖顺序编排创建与销毁；本类只被 渲染器.cpp 使用。
	*/
	class Renderer::Impl
	{
	public:
		//配置（init 成功后生效）
		Renderer_Config config;
		//平台模块
		Platform_Context platform;
		//设备模块
		Device_Context device;
		//内存模块
		Memory_Context memory;
		//交换链模块
		Swapchain_Context swapchain;
		//帧模块
		Frame_Context frame;
		//着色器模块
		Shader_Context shader;
		//管线模块
		Pipeline_Context pipeline;
		//网格模块
		Mesh_Context mesh;
		//纹理模块
		Texture_Context texture;
		//相机（生成每帧视图 × 投影矩阵）
		Camera camera;
		//场景 UBO 缓冲（长度 = 在途帧数，主机可见，逐帧写入相机矩阵）
		std::vector<VkBuffer> scene_ubos;
		//场景 UBO 内存（与 scene_ubos 一一对应）
		std::vector<VmaAllocation> scene_ubo_allocs;
		//内置默认白纹理句柄
		uint64_t default_texture = 0;
		//顶点着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//片元着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//待绘制项（由 mesh_draw 逐次登记，frame_render 消费后清空；同帧可登记多个网格）
		std::vector<Frame_Render_Item> pending_items;
		/*
		待绘制项的推送常量
		每登记一个网格追加一块「列主序 16 浮点模型矩阵」存储；
		用 deque 而非 vector：push_back 不使既有元素地址失效，
		故 pending_items 中的指针在同帧内始终有效。
		*/
		std::deque<std::array<float, 16>> pending_transforms;
		//是否已初始化
		bool initialized = false;
		//已呈现帧数
		uint64_t frame_count = 0;
	};
}
