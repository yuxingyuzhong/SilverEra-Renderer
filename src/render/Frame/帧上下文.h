#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
//交换链模块对外头（帧需取其图像、范围与图像可用信号量）
#include "Engine/EngineRenderer/src/render/Swapchain/交换链.h"
//线程池（第一百九十七轮：场景 pass 的次级命令缓冲并行录制）
#include "Engine/EngineRenderer/src/render/Thread/线程池.h"

//游戏引擎命名空间
namespace engine
{
	/*
	帧槽
	每帧槽持有：命令池、主命令缓冲、完成栅栏。
	数量 = 在途帧数 frame_overlap，与交换链图像数无关（§5.2 / §6.2）。
	呈现完成信号量属「每交换链图像」，由交换链模块持有（数量 = 交换链图像数）。
	*/
	struct Frame_Slot
	{
		//命令池（每帧经 vkResetCommandPool 重置）
		VkCommandPool command_pool = VK_NULL_HANDLE;
		//主命令缓冲（逐帧录制）
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		/*
		次级命令缓冲（第一百九十七轮；按需分配，数量 = 并行段数）
		场景 pass 被切成若干段后分别记录在这里，主缓冲以 vkCmdExecuteCommands 顺序执行；
		每帧 vkResetCommandPool 后仍可复用（其录制内容随之失效）。
		*/
		std::vector<VkCommandBuffer> secondary_buffers;
		/*
		次级命令缓冲各自独占的命令池（第二百一十八轮）
		Vulkan 规范要求 VkCommandPool 由外部同步：同一池上的 vkBeginCommandBuffer /
		vkCmd* 不得被多个线程并发调用。并行录制时每段由不同工作线程录制，
		故每段缓冲必须从各自独立的命令池分配；主池只用于主命令缓冲（主线程）。
		与 secondary_buffers 一一对应、同增同减；每帧随主池一起重置。
		*/
		std::vector<VkCommandPool> secondary_pools;
		/*
		批量阴影命令缓冲（第六百七十一轮；C2 收尾）
		编号对应批量路径的第 1..N-1 段（第 0 段复用 command_buffer）；
		首次需要时按缺额从本槽命令池分配，池重置后仍可重复录制，故逐帧复用、不再每次分配。
		*/
		std::vector<VkCommandBuffer> batch_buffers;
		//完成栅栏（主机侧等待该槽前一次提交完成）
		VkFence in_flight_fence = VK_NULL_HANDLE;
		//GPU 时间戳查询池（每槽四个查询：帧始 / 场景结束 / 后处理结束 / 帧末；设备不支持时为空）
		VkQueryPool timestamp_pool = VK_NULL_HANDLE;
		//该槽是否已写入过时间戳（首次写入前不读回，避免读到未执行的查询）
		bool timestamp_written = false;
		/*
		阴影 pass 时间戳查询池（第一百二十八轮；每槽两个查询：始 / 末）
		阴影 pass 为独立提交，故单独计时，避免覆盖主帧时间戳。
		*/
		VkQueryPool shadow_timestamp_pool = VK_NULL_HANDLE;
		//该槽是否已写入过阴影时间戳
		bool shadow_timestamp_written = false;
		/*
		间接命令缓冲（第二百五十二轮）
		由门面在初始化时创建并写入本槽字段；录制时以 vkCmdDrawIndexedIndirect 读取。
		mapped 为该缓冲的持久映射指针，capacity 为命令条数（每条 20 字节）。
		*/
		VkBuffer indirect_buffer = VK_NULL_HANDLE;
		void* indirect_mapped = nullptr;
		uint32_t indirect_capacity = 0;
	};

	/*
	延迟归还的内存资源
	持裸 Vulkan / VMA 句柄，标记一组「经内存模块归还」的设备资源（网格缓冲 / 纹理图像 / 渲染目标附件）。
	由门面在运行时卸载路径摘出后移交帧模块，帧模块在到期时统一经内存模块归还（§6.4）。
	*/
	struct Deferred_Memory_Resource
	{
		//缓冲（网格顶点 / 索引共用缓冲；非零时有效）
		VkBuffer buffer = VK_NULL_HANDLE;
		//缓冲内存（与 buffer 成对）
		VmaAllocation buffer_allocation = VK_NULL_HANDLE;
		//图像（纹理图像 / 渲染目标附件图像；非零时有效）
		VkImage image = VK_NULL_HANDLE;
		//图像内存（与 image 成对）
		VmaAllocation image_allocation = VK_NULL_HANDLE;
		//图像视图（非零时有效）
		VkImageView view = VK_NULL_HANDLE;
	};

	/*
	延迟销毁条目
	记录一组「被运行时卸载牵连」的旧资源，标记可在某帧号及以后释放（§6.4）。
	来源有二：① 交换链模块重建移交（旧交换链 ＋ 图像视图 ＋ 两类信号量 ＋ 深度图像与内存与视图）；
	        ② 门面卸载路径摘出的内存资源（网格缓冲 / 纹理图像 / 渲染目标附件）。
	*/
	struct Deferred_Entry
	{
		//旧交换链及其附属资源
		Swapchain_Old_Resources resources;
		//经内存模块归还的资源（网格缓冲 / 纹理图像 / 渲染目标附件）
		std::vector<Deferred_Memory_Resource> memory_resources;
		//可在该帧号及以后释放
		uint64_t release_frame = 0;
	};

	/*
	渲染项
	持裸 Vulkan 句柄，由门面组装后交给帧模块录制（不引入模块间依赖边）。
	*/
	struct Frame_Render_Item
	{
		//图形管线
		VkPipeline pipeline = VK_NULL_HANDLE;
		//管线布局（推送常量与描述符集绑定需要）
		VkPipelineLayout layout = VK_NULL_HANDLE;
		//场景 UBO 描述符集（set 0；可为空）
		VkDescriptorSet scene_set = VK_NULL_HANDLE;
		//采样器描述符集（set 1；可为空）
		VkDescriptorSet sampler_set = VK_NULL_HANDLE;
		//描述符集数量（0/1/2）
		uint32_t descriptor_set_count = 0;
		/*
		set0 binding5 动态偏移（收敛轮 863）
		仅蒙皮绘制非零：字节偏移 = 演员起始条目 × 64；静态 / UI / 后处理恒 0。
		命令录制时作为 pDynamicOffsets 传入 vkCmdBindDescriptorSets。
		*/
		VkDeviceSize palette_offset = 0;
		/*
		逐绘制项采样状态（第一百七十四轮）
		登记绘制项时从门面的当前绑定状态快照，帧渲染阶段据此写一条专属采样器集；
		使同帧内不同绘制项可各自采样不同纹理（含文字图集与场景纹理共存）。
		视图为空时回落该帧槽的共享采样器集。
		*/
		VkSampler draw_sampler = VK_NULL_HANDLE;
		VkImageView draw_albedo = VK_NULL_HANDLE;
		VkImageView draw_normal = VK_NULL_HANDLE;
		VkImageView draw_roughness = VK_NULL_HANDLE;
		VkImageView draw_emissive = VK_NULL_HANDLE;
		VkImageView draw_ao = VK_NULL_HANDLE;
		VkImageView draw_metallic = VK_NULL_HANDLE;
		/*
		逐绘制项场景深度（set1 binding 2；第三百六十六轮）
		为空时该绑定回落反照率占位（与旧行为一致）；粒子软粒子路径填入 HiZ 历史视图与最近邻采样器。
		*/
		VkImageView draw_depth = VK_NULL_HANDLE;
		VkSampler draw_depth_sampler = VK_NULL_HANDLE;
		//顶点缓冲
		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		//索引缓冲
		VkBuffer index_buffer = VK_NULL_HANDLE;
		//索引区在索引缓冲内的字节偏移（顶点与索引共用一个缓冲时非零）
		VkDeviceSize index_offset = 0;
		//索引数
		uint32_t index_count = 0;
		/*
		是否使用实例属性（第三百四十一轮；默认真）
		为假时该绘制项的管线不消费实例属性（如「粒子GPU.vert」按 gl_InstanceIndex 从 SSBO 取状态），
		录制阶段只绑定顶点缓冲，instance_buffer 可为空；
		实例数仍由 instance_count 表达（绘制量统计与合并口径不变）。
		*/
		bool instance_input = true;
		/*
		是否使用「粒子专用场景集」（第三百四十一轮；默认假）
		为真时门面在帧渲染阶段把 set0 填装为粒子专用集（绑定 0 = 场景 UBO、1 = 状态 SSBO、2 = 紧凑索引），
		而非普通场景集；配合 instance_input = 假用于「粒子GPU.vert」的着色器内 billboard。
		*/
		bool particle_scene_set = false;
		/*
		GPU 侧间接绘制（第三百四十二轮；默认假）
		为真时该绘制项以 vkCmdDrawIndexedIndirect 下达**单条**命令（命令由计算着色器写入 indirect_command_buffer），
		实例数取自命令而非 instance_count；instance_count 仍参与绘制统计口径。
		与间接合批路径互斥（instance_input 恒为假，故逐项录制路径接管）。
		*/
		bool gpu_indirect = false;
		//间接命令缓冲（gpu_indirect 为真时必填；一条 20 字节命令，偏移 0）
		VkBuffer indirect_command_buffer = VK_NULL_HANDLE;
		VkDeviceSize indirect_command_offset = 0;
		//实例缓冲（模型矩阵来源；instance_input 为真时必填）
		VkBuffer instance_buffer = VK_NULL_HANDLE;
		//实例区在实例缓冲内的字节偏移（一个实例 = 20 浮点 = 80 字节）
		VkDeviceSize instance_offset = 0;
		//实例数（1 = 等价于逐次绘制；0 表该项不完整，会被录制时跳过）
		uint32_t instance_count = 0;
		//推送常量数据（可为空）
		const void* push_constants = nullptr;
		//推送常量字节数（0 表不推送）
		uint32_t push_constant_size = 0;
		//推送常量在管线布局中的字节偏移（文字在 0；后处理的「源 / 宿尺寸」在 80）
		uint32_t push_constant_offset = 0;
		/*
		后处理泛光参数（仅后处理链各级使用；x = 阈值、y = 软膝、z = 强度、w 保留）
		由门面写入，帧模块随「源 / 宿尺寸」一并推送（offset 96）。
		*/
		float post_params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		/*
		后处理描边色（第一百八十一轮；仅后处理链各级使用，rgb = 描边色、a 保留）
		由门面写入，帧模块随「源 / 宿尺寸 ＋ 泛光参数」一并推送（offset 112）；默认黑色。
		*/
		float post_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		/*
		后处理逐级额外四参（第四百零一轮；仅后处理链各级使用，offset 128 的 vec4）
		由门面写入（post_stage_extra_set），帧模块仅在 post_extra_enabled 时随前 12 个浮点一并推送；
		默认全 0，既有阶段不读取该组，故引入前后画面逐字节一致。
		*/
		float post_extra[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		//是否参与阴影投影（第五十二轮；文字绘制项为假，场景几何为真）
		bool cast_shadow = true;
		/*
		是否透明绘制项（第一百三十轮；混合档非不透明时为真，如文字）
		透明项的重叠混合结果依赖绘制顺序，故静态合批不合并此类项。
		*/
		bool transparent = false;
		/*
		该绘制项所用管线的采样数（第九十八轮；1 = 单采样）
		后处理链各级据此选择宿附件：1 直接写单采样宿，> 1 用多重采样附件并解析。
		0 / 1 均按单采样处理。
		*/
		uint32_t sample_count = 1;
	};

	/*
	渲染目标附件集合
	由门面在「渲染到离屏目标」时传入：帧链路据此把场景先渲染进该目标（颜色附件 ＋ 深度附件）。
	布局约定：颜色进入时须为 SHADER_READ_ONLY_OPTIMAL（目标创建时已转换，帧间恒保持该布局），
	          深度进入时恒为 DEPTH_ATTACHMENT_OPTIMAL（逐帧以 CLEAR 清空）。
	*/
	struct Frame_Render_Target
	{
		//颜色图像（作颜色附件与像素回读来源；MSAA 开启时同时作为解析目标）
		VkImage color_image = VK_NULL_HANDLE;
		//颜色图像视图
		VkImageView color_view = VK_NULL_HANDLE;
		//深度图像视图（MSAA 开启时为单采样深度，不参与主 pass）
		VkImageView depth_view = VK_NULL_HANDLE;
		/*
		深度图像句柄（第一百二十三轮）
		供「场景 pass → 后处理」之间的深度布局转换（采样前转 SHADER_READ_ONLY、用后转回深度附件）；
		为空时后处理链不做深度转换（景深阶段将读到未定义内容）。
		*/
		VkImage depth_image = VK_NULL_HANDLE;
		/*
		MSAA 附件（第九十六轮；sample_count > 1 时有效）
		颜色 pass 写入多重采样附件，结束时解析（resolve）到 color_view；深度用多重采样附件。
		*/
		VkImage msaa_color_image = VK_NULL_HANDLE;
		VkImageView msaa_color_view = VK_NULL_HANDLE;
		VkImageView msaa_depth_view = VK_NULL_HANDLE;
		/*
		MSAA 深度图像（第一百九十轮；sample_count > 1 且开启深度解析时非空）
		场景 pass 结束时由动态渲染的深度解析写入单采样深度（depth_image）；
		该字段为空即表示本路径不做深度解析（后处理深度类阶段读到未定义内容）。
		*/
		VkImage msaa_depth_image = VK_NULL_HANDLE;
		uint32_t sample_count = 1;
		/*
		逐材质色调编号附件（第 808 轮 · 8 位路径编号预通道）
		非空时帧链路在场景 pass 结束后以同一深度图再绘一遍不透明项，
		把每个可见片元的逐材质算子写入该附件（R8_UINT，0 = 无覆盖）；
		主 pass 保持单颜色附件，因此不产生任何管线变体。
		*/
		VkImage tone_id_image = VK_NULL_HANDLE;
		VkImageView tone_id_view = VK_NULL_HANDLE;
		VkFormat tone_id_format = VK_FORMAT_UNDEFINED;
		//多重采样编号附件（第 821 轮；采样数 = 1 时为空）
		VkImage tone_id_msaa_image = VK_NULL_HANDLE;
		VkImageView tone_id_msaa_view = VK_NULL_HANDLE;
		/*
		颜色 / 深度格式（第二百轮）
		供并行录制时次级命令缓冲的 VkCommandBufferInheritanceRenderingInfo 使用；
		离屏目标通常与交换链同格式，阴影目标为独立固定格式。
		*/
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//渲染范围
		VkExtent2D extent = { 0, 0 };
		/*
		子视口（第一百二十一轮；级联阴影按象限分片绘制）
		宽或高为 0 表示整幅目标（既有行为）；非零时仅在该矩形内设置视口与裁剪，
		渲染区与清屏仍覆盖整幅目标（故同一目标的多个分片须由调用方自行控制清屏时机）。
		*/
		VkRect2D viewport_rect = { { 0, 0 }, { 0, 0 } };
		/*
		渲染区跟随子视口（第六百七十六轮）
		为真且 viewport_rect 非空时，动态渲染的 renderArea 收缩到该矩形（清屏也只作用于该矩形）；
		用于图集瓦片等「各分片独立清屏」的场景，避免整图 load / store 的带宽浪费。
		为假（默认）时保持「渲染区覆盖整幅目标」的既有行为（级联分片依赖首片整图清屏）。
		*/
		bool render_area_from_viewport = false;
	};

	/*
	UI 绘制回调
	每帧在「场景渲染 pass 内、vkCmdEndRendering 之前」由帧链路调用，
	把本帧命令缓冲交给上层（上层在此调用 ImGui_ImplVulkan_RenderDrawData，
	该方法直接画在本帧已开启的动态渲染通道内（与场景共用同一通道），不自开渲染通道）。
	*/
	struct Frame_Ui_Callback
	{
		//绘制回调（命令缓冲参数）
		void (*fn)(VkCommandBuffer command_buffer, void* user_data) = nullptr;
		//用户数据
		void* user_data = nullptr;
	};

	/*
	场景 pass 退场回调（第二百五十四轮）
	在场景动态渲染通道结束之后、深度仍处于深度附件布局时调用，用于「深度 → 历史」与金字塔生成等
	需要命令缓冲但必须在渲染通道之外的阶段。depth_image / depth_view 为本次场景以单采样写出的深度；
	回调须自行完成布局往返（进入 DEPTH_ATTACHMENT、退出恢复 DEPTH_ATTACHMENT）。
	*/
	struct Frame_Post_Scene_Callback
	{
		//回调（命令缓冲 ＋ 深度图像与视图 ＋ 范围 ＋ 用户数据）
		void (*fn)(VkCommandBuffer command_buffer, VkImage depth_image, VkImageView depth_view,
			VkExtent2D extent, void* user_data) = nullptr;
		//用户数据
		void* user_data = nullptr;
	};

	/*
	像素回读请求
	由门面在 render 时传入：pixels 非空即请求本帧把颜色附件拷回该主机缓冲。
	capacity 为 pixels 的字节容量（须不小于 宽 × 高 × 4）；宽 / 高 / 字节数由帧模块回填。
	*/
	struct Frame_Readback_Request
	{
		//目标主机缓冲（为空表示本帧不回读）
		uint8_t* pixels = nullptr;
		//目标缓冲字节容量
		VkDeviceSize capacity = 0;
		//回填：回读宽度
		uint32_t width = 0;
		//回填：回读高度
		uint32_t height = 0;
		//回填：回读字节数
		VkDeviceSize bytes = 0;
	};

	/*
	帧上下文
	持有帧槽集合，负责取图像、命令录制、提交与呈现、延迟销毁、像素回读（§5.2 / §6.2）。
	生命周期：build → render × N → destroy；destroy 幂等，析构兜底。
	同步原语归属（§5.2 表）：帧栅栏属「每帧槽」，数量 = frame_overlap；
	图像可用与呈现完成信号量均属「每交换链图像」，由交换链模块持有（数量 = 交换链图像数）。
	*/
	class Frame_Context
	{
	public:
#include "帧上下文_接口_生命周期.inl"
#include "帧上下文_接口_录制与延迟.inl"

	private:
#include "帧上下文_私有方法.inl"
#include "帧上下文_成员.inl"
	};
}
