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
//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
#include "Application/Renderer/src/render/RenderTarget/渲染目标.h"
//门面声明（Renderer::Impl 为其私有嵌套类）
#include "Application/Renderer/src/render/Renderer/渲染器.h"
//UI 通道（实现体持有其实例，供上层接入 ImGui Vulkan 后端）
#include "Application/Renderer/src/render/Renderer/渲染器_UI通道.h"

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
		//具名管线记录（句柄 → 顶点 / 片元着色器模块；着色器模块所有权在门面，随卸载 / 关闭销毁）
		struct Extra_Pipeline_Record
		{
			//句柄（≥ 1）
			uint64_t handle = 0;
			//顶点着色器模块
			VkShaderModule vert_module = VK_NULL_HANDLE;
			//片元着色器模块
			VkShaderModule frag_module = VK_NULL_HANDLE;
		};
		//管线模块
		Pipeline_Context pipeline;
		//具名管线集合（默认场景管线不在内）
		std::vector<Extra_Pipeline_Record> extra_pipelines;
		//当前绘制所选管线句柄（0 = 默认场景管线）
		uint64_t bound_pipeline_request = 0;
		//网格模块
		Mesh_Context mesh;
		//纹理模块
		Texture_Context texture;
		//相机（生成每帧视图 × 投影矩阵）
		Camera camera;
		//UI 通道（供上层接入 ImGui Vulkan 后端；不代管 UI）
		Ui_Channel_Context ui_channel;
		//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
		Render_Target_Context render_target;
		//本帧渲染目标句柄（0 = 直接渲染到交换链）
		uint64_t active_target = 0;
		//场景 UBO 缓冲（长度 = 在途帧数，主机可见，逐帧写入相机矩阵）
		std::vector<VkBuffer> scene_ubos;
		//场景 UBO 内存（与 scene_ubos 一一对应）
		std::vector<VmaAllocation> scene_ubo_allocs;
		//实例缓冲（长度 = 在途帧数，主机可见，逐帧写入本帧全部实例模型矩阵）
		std::vector<VkBuffer> instance_buffers;
		//实例缓冲内存（与 instance_buffers 一一对应）
		std::vector<VmaAllocation> instance_allocations;
		//每帧槽实例缓冲容量（模型矩阵个数；一个实例 = 16 浮点 = 64 字节）
		uint32_t instance_capacity = 256;
		//内置默认白纹理句柄
		uint64_t default_texture = 0;
		//最后一次「设置当前纹理」请求的句柄（供采样器状态切换后重新绑定；0 表回落默认白纹理）
		uint64_t bound_texture_request = 0;
		//当前混合档（与管线模块内记录保持一致；交换链重建时随重建传入）
		Blend_Mode blend_mode = Blend_Mode::Opaque;
		//顶点着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//片元着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//待绘制项（由 mesh_draw 逐次登记，frame_render 消费后清空；同帧可登记多个网格）
		std::vector<Frame_Render_Item> pending_items;
		//本帧待绘制的实例模型矩阵（列主序 16 浮点 × 实例数；按登记顺序连续存放，一次写入帧槽实例缓冲）
		std::vector<std::array<float, 16>> pending_instances;
		//是否已初始化
		bool initialized = false;
		//已呈现帧数
		uint64_t frame_count = 0;
	};
}
