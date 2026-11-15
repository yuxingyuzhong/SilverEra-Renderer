#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
各模块对外头
本头属本层「实现细节」：实现体持有各模块实例，故需其完整类型；
不进公共头，故可以合法暴露 Vulkan / SDL3 类型（层外仍看不到）。
*/
#include "Engine/EngineRenderer/src/render/Platform/平台上下文.h"
#include "Engine/EngineRenderer/src/render/Device/设备上下文.h"
#include "Engine/EngineRenderer/src/render/Memory/内存上下文.h"
#include "Engine/EngineRenderer/src/render/Swapchain/交换链.h"
#include "Engine/EngineRenderer/src/render/Frame/帧上下文.h"
#include "Engine/EngineRenderer/src/render/Shader/着色器.h"
#include "Engine/EngineRenderer/src/render/Pipeline/管线.h"
//计算管线模块（第二百四十一轮）
#include "Engine/EngineRenderer/src/render/Compute/Compute.h"
//Hi-Z 深度历史与金字塔资源（第二百五十三轮）
#include "Engine/EngineRenderer/src/render/HiZ/HiZ.h"
//分簇网格与 CPU 逐簇计数参考（第二百六十七轮）
#include "Engine/EngineRenderer/src/render/Cluster/簇划分.h"
//粒子数据与粒子池（第三百零七 / 三百零八轮；位于 EngineSystem）
#include "Engine/EngineSystem/src/world/data/particle/Particle_Data/粒子数据.h"
//GPU 驱动模块：实例包围体与视锥剔除参考（第二百四十七 / 二百四十八轮）
#include "Engine/EngineRenderer/src/render/GPU驱动/包围体.h"
#include "Engine/EngineRenderer/src/render/GPU驱动/剔除数据.h"
#include "Engine/EngineRenderer/src/render/Mesh/网格.h"
#include "Engine/EngineRenderer/src/render/Texture/纹理.h"
#include "Engine/EngineRenderer/src/render/Scene/相机.h"
//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
#include "Engine/EngineRenderer/src/render/RenderTarget/渲染目标.h"
//线程池模块（帧内粗粒度并行执行器；第一百七十七轮）
#include "Engine/EngineRenderer/src/render/Thread/线程池.h"
//字体模块（TTF → 字形图集；文字绘制取用）
#include "Engine/EngineRenderer/src/render/Text/字体.h"
//文字绘制子系统（网格缓存 / 文字颜色 / 管线句柄；第四十七轮下沉）
#include "Engine/EngineRenderer/src/render/Text/文字渲染.h"
//门面声明（Renderer::Impl 为其私有嵌套类）
#include "Engine/EngineRenderer/src/render/Renderer/渲染器.h"
//UI 通道（实现体持有其实例，供上层接入 ImGui Vulkan 后端）
#include "Engine/EngineRenderer/src/render/Renderer/渲染器_UI通道.h"
//帧输入聚合器（渲染层自持原始输入）
#include "Engine/EngineRenderer/src/render/Input/输入聚合.h"
//原生 UI 控件树与绘制器
#include "Engine/EngineRenderer/src/render/UI/用户界面.h"
#include "Engine/EngineRenderer/src/render/UI/UI渲染器.h"
//UI 事件桥（与系统输入体系交互）
#include "Engine/EngineRenderer/src/render/UI/UI事件桥.h"
//事件终端（渲染层向系统层投递原始输入）
#include "Engine/EngineFoundation/src/event/Event_Terminal/事件终端.h"
#include "实现体_常量.h"
#include "实现体_平台.h"
#include "实现体_内存.h"
#include "实现体_交换链.h"
#include "实现体_帧.h"
#include "实现体_资源.h"
#include "实现体_场景.h"
#include "实现体_子系统.h"
#include "实现体_状态.h"
//界面域状态（从实现体迁出的界面成员）
#include "界面域状态.h"
//粒子域状态（从实现体迁出的粒子成员）
#include "粒子域状态.h"
//资源域状态（从实现体迁出的网格 / 纹理 / 材质 / 管线成员）
#include "资源域状态.h"
//计算域状态（从实现体迁出的计算上下文 / Hi-Z / Cluster 成员）
#include "计算域状态.h"
//设备域状态（从实现体迁出的渲染目标选择 / 后处理 / 超采样成员）
#include "设备域状态.h"
//场景域状态（从实现体迁出的相机 / 光源 / 雾 / 风格与阴影配置成员）
#include "场景域状态.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器实现体
	成员按域分散在 8 个聚合子结构（见 实现体_*.h），本类以继承方式聚合，
	故 impl->成员 与函数体内的无限定访问写法全部保持不变。
	*/
	class Renderer_Impl_Holder::Impl : public Renderer_Impl_平台, public Renderer_Impl_内存, public Renderer_Impl_交换链, public Renderer_Impl_帧,
		public Renderer_Impl_资源, public Renderer_Impl_场景, public Renderer_Impl_子系统, public Renderer_Impl_状态
	{
	public:
		/*
		重建交换链并把旧资源压入帧延迟销毁队列
		无整帧停顿：旧交换链及其附属资源（图像视图 / 两类信号量 / 深度图像与内存与视图）被移交延迟队列，
		延迟 = 在途帧数 + 1，保证在途帧用尽后才释放（§6.4）。返回重建是否成功。
		*/
		bool swapchain_rebuild_deferred(uint32_t width, uint32_t height);
		/*
		确保 Hi-Z 资源就绪（第二百五十三轮；惰性构建）
		计算路径可用且 R32 浮点存储图像可用时才构建「每帧槽深度历史 ＋ 金字塔」；
		已在当前尺寸下就绪时为空操作。失败返回假且不留下半初始化资源。
		*/
		bool hiz_ensure(void);
		/*
		确保光源记录 SSBO 就绪（第二百六十六轮；惰性构建）
		计算路径可用时按帧槽各建一个主机可见存储缓冲（容量 LIGHT_RECORD_CAPACITY × 80 字节）；
		已就绪时为空操作，构建失败回滚为无资源。
		*/
		bool cluster_ensure(void);
		//销毁光源记录 SSBO（第二百六十六轮；幂等）
		void cluster_destroy(void);
		/*
		确保簇数据占位缓冲就绪（第二百八十八轮；惰性构建）
		创建 64 字节主机可见存储缓冲；Cluster 关闭时 set0 binding 1..4 统一指向它，
		保证四条绑定始终有完整描述符。
		*/
		bool cluster_placeholder_ensure(void);
		/*
		确保图形 set0 的簇数据缓冲就绪（第二百八十八轮；惰性构建 / 按容量增长重建）
		计数 / 偏移容量 = 簇数、列表容量 = 列表条目数；容量不足时先等尽在途帧再重建，
		重建后重写全部帧槽的 binding 1..4。光源记录缓冲由 cluster_ensure 负责。
		*/
		bool cluster_graphics_ensure(uint32_t cluster_total, uint32_t list_capacity);
		//销毁图形 set0 的簇数据缓冲与占位缓冲（第二百八十八轮；幂等）
		void cluster_graphics_destroy(void);
		/*
		场景退场回调入口（第二百五十四轮）
		帧模块以 C 函数指针调用；user_data 为本实现体地址，内部完成深度拷贝与金字塔生成的录制。
		*/
		static void hiz_post_scene_entry(VkCommandBuffer command_buffer, VkImage depth_image, VkImageView depth_view,
			VkExtent2D extent, void* user_data);
		/*
		登记具名图形管线（第一百七十二轮抽出；门面 pipeline_load 与材质共享管线共用）
		加载着色器对并登记一条具名管线；cull_none 为真时关闭背面剔除（双面渲染）。
		成功时写出句柄并把着色器模块所有权记入门面；失败时回收已加载模块并返回假。
		*/
		bool pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
			const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
			bool depth_write = true, bool cull_front = false,
			bool depth_test = true, bool depth_format_override = false,
			VkFormat depth_format = VK_FORMAT_UNDEFINED, uint32_t sample_count = 0);
		//界面域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Ui* ui_domain = nullptr;
		//粒子域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Particle* particle_domain = nullptr;
		//资源域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Resource* resource_domain = nullptr;
		//计算域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Compute* compute_domain = nullptr;
		//设备域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Device* device_domain = nullptr;
		//场景域对象（由门面持有；关闭流程据此判断域对象是否仍可访问）
		Render_Scene* scene_domain = nullptr;
		/*
		登记反向外扩描边项（第二百二十八轮）
		把主体绘制项复制一份，换「剔除正面」的描边管线，并把宽度 / 颜色推入 offset 112 的推送块；
		描边关闭（宽度为 0）、描边管线未登记或正在绘制文字时为空操作。
		*/
		void outline_item_push(const Frame_Render_Item& item);
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
		/*
		把后处理采样集绑定到给定目标视图组（第九十七轮）
		签名未变时为空操作（避免逐帧更新在途描述符集）；变化时先等尽在途帧再写满全部帧槽。
		绑定规则：index i 的 binding 0 = views[i]、binding 1 = views[0]（场景目标）。
		*/
		bool post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler,
			VkImageView depth_view = VK_NULL_HANDLE);
		/*
		确保 SSAA 降采样具名管线就绪（惰性登记）
		着色器对为 后处理.vert / 降采样.frag（不透明档）；模块由本实现体持有，随 extra_pipelines 统一销毁。
		*/
		bool ssaa_pipeline_ensure(void);
		/*
		确保天空盒具名管线就绪（惰性登记；第一百二十五轮）
		着色器对为 天空盒.vert / 天空盒.frag；采样数跟随主场景配置，深度只读 ＋ LESS_OR_EQUAL，
		使全屏天空项只覆盖深度仍为清屏值的背景像素。
		*/
		bool sky_pipeline_ensure(void);
		/*
		确保 SSAA 超采样目标就绪（场景目标 = 输出 × 倍率；解析目标 = 输出尺寸）
		倍率或输出尺寸变化时先等尽在途帧、销毁旧目标再重建；创建失败置 ssaa_extent_failed 并由调用方回落。
		*/
		bool ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor);
		//销毁 SSAA 超采样目标（关闭时调用；句柄清零）
		void ssaa_targets_destroy(void);

		//按句柄取 Vulkan 缓冲（第三百四十一轮；供描述符集写入；未找到返回空）
		VkBuffer compute_buffer_native(uint64_t handle) const;
		/*
		计算存储缓冲创建（第三百四十二轮；可追加 Vulkan 用法位）
		主机可见 STORAGE（恒含 TRANSFER_SRC），extra_usage 追加额外用法（如 INDIRECT_BUFFER）；
		句柄自 1 起登记，失败时句柄置 0 且不留下任何资源。
		*/
		bool compute_buffer_create_usage(Compute_Buffer_Handle& handle_out, uint32_t size, const void* data,
			VkBufferUsageFlags extra_usage);

		//帧阶段：光源空间矩阵与级联拟合
		void frame_stage_shadow_matrices(void);
		//帧阶段：描述符集与实例句柄填装
		void frame_stage_descriptor_sets(uint32_t slot);
		//配置（init 成功后生效）

		//点光源类型与上限由场景域持有；别名供迁移期帧代码继续引用
		using Point_Light = Scene_State::Point_Light;
		static constexpr uint32_t POINT_LIGHT_UBO_MAX = Scene_State::POINT_LIGHT_UBO_MAX;
		static constexpr uint32_t POINT_LIGHT_MAX = Scene_State::POINT_LIGHT_MAX;

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
	};
}
