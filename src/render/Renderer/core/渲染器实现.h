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
//字体模块（TTF → 字形图集；文字绘制取用）
#include "Application/Renderer/src/render/Text/字体.h"
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
		/*
		重建交换链并把旧资源压入帧延迟销毁队列
		无整帧停顿：旧交换链及其附属资源（图像视图 / 两类信号量 / 深度图像与内存与视图）被移交延迟队列，
		延迟 = 在途帧数 + 1，保证在途帧用尽后才释放（§6.4）。返回重建是否成功。
		*/
		bool swapchain_rebuild_deferred(uint32_t width, uint32_t height);
		/*
		确保后处理全屏四边形网格就绪（惰性创建；单级路径与链路径共用）
		全屏四边形（NDC -1..1）随 mesh 模块销毁。
		*/
		bool post_mesh_ensure(void);
		/*
		确保内置单级后处理具名管线就绪（惰性登记；仅既有单级默认路径使用）
		着色器对为 后处理.vert / 后处理.frag（不透明档）；着色器模块由本实现体持有，随 extra_pipelines 统一销毁。
		*/
		bool post_pipeline_ensure(void);
		//确保既有单级默认后处理所需资源就绪（默认后处理管线 ＋ 全屏网格）
		bool post_resources_ensure(void);
		//配置（init 成功后生效）
		Renderer_Config config;
		//清屏色（RGBA；由 scene_apply / clear_color_set 设置，frame_render 每帧取用）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
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
		//材质记录（管线 ＋ 纹理；有效标记用于句柄槽复用）
		struct Material_Record
		{
			//图形管线句柄（0 = 默认场景管线）
			uint64_t pipeline = 0;
			//纹理句柄（0 = 内置默认白纹理）
			uint64_t texture = 0;
			//记录是否有效
			bool valid = false;
		};
		//材质集合（下标 = 句柄 - 1）
		std::vector<Material_Record> materials;
		/*
		按混合档缓存的具名管线（下标 = 档位序，0 表该档尚未登记）
		非不透明档材质**共享**该档的具名管线（以默认场景着色器对登记），故同档多个材质不再各自登记；
		管线所有权归门面（随 shutdown 的 extra_pipelines 统一释放），材质卸载**不**释放它。
		*/
		std::vector<uint64_t> blend_pipelines;
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
		//本帧「渲染到目标并同帧后处理呈现」的目标句柄（0 = 关闭；见 render_target_present_set）
		uint64_t present_target = 0;
		/*
		本帧「同帧后处理链」的额外中间目标句柄（下标 1..N-1；空 = 只有 present_target 一个目标）
		由 render_target_present_chain_set 设置；链上第 i 级取 targets[i % 目标数]，故链长 ≥ 2 时至少需 2 个目标。
		*/
		std::vector<uint64_t> present_extra;
		//同帧后处理呈现的目标数上限（= 每帧槽后处理采样集数量，超出即整组拒绝）
		static constexpr uint32_t PRESENT_TARGET_MAX = Pipeline_Context::POST_SET_PER_SLOT;
		/*
		本帧后处理链（各级具名管线句柄，下标即级序；空 = 走既有单级默认后处理路径）
		由 post_chain_set 设置；各级使用同一全屏几何与场景 UBO，仅管线与源采样集不同。
		级数上限见 POST_CHAIN_MAX_STAGES。
		*/
		std::vector<uint64_t> post_chain;
		//后处理链级数上限（超出即整链拒绝；每级占用一次全屏 pass，超长链会带来隐性性能坍塌）
		static constexpr uint32_t POST_CHAIN_MAX_STAGES = 8;
		//后处理管线句柄（0 = 尚未登记；首次使用时懒登记，不透明档；仅用于既有单级默认路径）
		uint64_t post_pipeline = 0;
		//全屏四边形网格句柄（0 = 尚未创建；后处理 pass 的几何）
		Mesh_Handle fullscreen_mesh = 0;
		//字体模块（TTF → 字形图集）
		Font_Context font;
		//文字管线句柄（0 = 尚未登记；首次 text_draw 时懒登记，Alpha 混合档）
		uint64_t text_pipeline = 0;
		//文字颜色（RGBA，默认白；由 text_color_set 更新，text_draw* 以推送常量下发）
		float text_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		/*
		泛光参数（阈值 / 软膝 / 强度 / 保留；默认 0.6 / 0.1 / 1.0 / 0）
		由 bloom_param_set 更新；后处理链各级以推送常量（offset 96）下发。
		*/
		float bloom_param[4] = { 0.6f, 0.1f, 1.0f, 0.0f };
		//本帧推送常量存储（每项 20 浮点 = mat4 矩阵 ＋ vec4 颜色；deque 保证元素引用稳定，绘制项持其指针）
		std::deque<std::array<float, 20>> pending_push_constants;
		//文本网格缓存条目（(字体句柄, 文本, 布局) → 网格句柄；避免逐帧重建几何）
		struct Text_Mesh_Record
		{
			//字体句柄
			uint64_t font = 0;
			//文本（UTF-8）
			std::string text;
			//布局（换行宽度 / 行高 / 对齐；参与缓存键，避免同文本不同布局命中旧网格）
			Text_Layout layout;
			//网格句柄
			Mesh_Handle mesh = 0;
		};
		//文本网格集合
		std::vector<Text_Mesh_Record> text_meshes;
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
		/*
		当前绑定视图（写入各帧槽 set1 的采样源；持裸句柄）
		纹理与渲染目标共用同一绑定语义：无论来源是纹理还是渲染目标，均以「实际绑定的图像视图」记录，
		故采样器状态切换后可直接以该视图重写 set1（处置 R-FACADE-06：不再按句柄回落后覆盖目标绑定）。
		*/
		VkImageView bound_view = VK_NULL_HANDLE;
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
