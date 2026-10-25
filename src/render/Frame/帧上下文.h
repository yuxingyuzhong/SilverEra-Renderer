#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
//交换链模块对外头（帧需取其图像、范围与图像可用信号量）
#include "Application/Renderer/src/render/Swapchain/交换链.h"

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
		//顶点缓冲
		VkBuffer vertex_buffer = VK_NULL_HANDLE;
		//索引缓冲
		VkBuffer index_buffer = VK_NULL_HANDLE;
		//索引区在索引缓冲内的字节偏移（顶点与索引共用一个缓冲时非零）
		VkDeviceSize index_offset = 0;
		//索引数
		uint32_t index_count = 0;
		//实例缓冲（模型矩阵来源；渲染层统一走实例路径，故所有绘制项均须给出）
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
		uint32_t sample_count = 1;
		//渲染范围
		VkExtent2D extent = { 0, 0 };
		/*
		子视口（第一百二十一轮；级联阴影按象限分片绘制）
		宽或高为 0 表示整幅目标（既有行为）；非零时仅在该矩形内设置视口与裁剪，
		渲染区与清屏仍覆盖整幅目标（故同一目标的多个分片须由调用方自行控制清屏时机）。
		*/
		VkRect2D viewport_rect = { { 0, 0 }, { 0, 0 } };
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
		//构造（不创建任何 Vulkan 对象）
		Frame_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Frame_Context(void);
		//不可拷贝、不可移动（持有 Vulkan 句柄与队列）
		Frame_Context(const Frame_Context&) = delete;
		Frame_Context& operator=(const Frame_Context&) = delete;

		/*
		构建
		创建 frame_overlap 个帧槽（命令池 / 命令缓冲 / 栅栏）。
		swapchain：已构建的交换链（帧链路取图像依赖它）。
		memory：内存模块（像素回读缓冲的创建 / 销毁依赖它）。
		*/
		bool build(VkDevice device, VkPhysicalDevice physical_device,
			uint32_t graphics_family, VkQueue graphics_queue,
			uint32_t present_family, VkQueue present_queue,
			Swapchain_Context& swapchain, Memory_Context& memory, uint32_t frame_overlap);
		//销毁（幂等；先等设备空闲，再逆序销毁帧槽）
		void destroy(void);
		//是否已构建完成（至少一个帧槽）
		bool valid(void) const;

		/*
		渲染一帧（取图像 → 录制 → 提交 → 呈现，可选像素回读）
		swapchain：当前交换链；rebuild_needed_out：置真表示本帧遇到过期 / 次优，需上层重建；
		items / item_count：渲染项列表（可为空，此时只清屏）；clear_color：清屏色；
		ui_callback：每帧 UI 绘制回调（可为空；在场景 pass 结束后交还上层绘制 UI）；
		readback：像素回读请求（可为空；非空且 pixels 非空时把本帧颜色附件拷回该缓冲）。
		返回假视为硬错误。
		*/
		bool render(Swapchain_Context& swapchain, const Frame_Render_Item* items, uint32_t item_count,
			bool& rebuild_needed_out, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr,
			Frame_Readback_Request* readback = nullptr);
		/*
		渲染一帧到离屏目标（不取交换链图像、不呈现）
		帧槽等待 → 重置命令池 → 录制（目标附件 ＋ 场景 pass ＋ 可选像素回读）→ 仅以栅栏提交 → 回读；
		与 render 共用同一批帧槽与帧计数轮转，故两者可交替调用而不打乱在途帧同步。
		目标颜色在帧间恒处于 SHADER_READ_ONLY_OPTIMAL，故离屏结果可直接作纹理采样。
		返回假视为硬错误。
		*/
		bool render_to_target(const Frame_Render_Target& target, const Frame_Render_Item* items,
			uint32_t item_count, const float clear_color[4], const Frame_Ui_Callback* ui_callback = nullptr,
			Frame_Readback_Request* readback = nullptr, bool shadow_pass = false);
		/*
		渲染一帧：先把场景渲染进离屏目标，再在同一命令缓冲内以该目标为纹理做一次全屏后处理 pass 呈现到交换链
		（同帧多 pass；与 render_to_target 的跨帧闭环不同，本路径在同一帧内完成「目标 → 后处理 → 呈现」）。
		★ 第十轮收口：本函数是 render_chain_present 的**单级特例薄封装**（后处理级数 = 1、无第二目标），
		  两条路径共用同一套录制 / 提交 / 统计实现（不再各自维护一份）。
		target 为离屏目标附件；items / item_count 为**第一个 pass（场景）**的渲染项；
		post_item 为**第二个 pass（后处理）**的单个绘制项（管线 / 布局 / 描述符集 / 全屏几何 / 实例缓冲）；
		readback 非空时回读的是**最终呈现到交换链**的颜色附件（即后处理输出）。
		要求目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源），不一致按硬错误拒绝。
		返回假视为硬错误。
		*/
		bool render_to_target_present(Swapchain_Context& swapchain, const Frame_Render_Target& target,
			const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item& post_item,
			const float clear_color[4], bool& rebuild_needed_out,
			Frame_Readback_Request* readback = nullptr);
		/*
		渲染一帧：场景渲染进离屏目标后，在同一命令缓冲内顺序执行 N 级后处理并呈现到交换链
		targets / target_count 为中间目标组：targets[0] 为场景目标，其余为额外中间目标（2..4 个）；
		第 i 级的源 = targets[i % target_count]、非末级宿 = targets[(i + 1) % target_count]，末级恒呈现到交换链；
		故 post_count ≥ 2 时须 target_count ≥ 2（单目标无法做 ping-pong）。
		中间目标在帧间恒保持 SHADER_READ_ONLY_OPTIMAL（每级写完后转回）。
		post_items 为各级后处理绘制项（其 sampler_set 须分别绑定对应源的采样集）；
		readback 非空时回读的是**最终呈现到交换链**的颜色附件。
		要求**每个**目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源），不一致按硬错误拒绝。
		返回假视为硬错误。
		*/
		bool render_chain_present(Swapchain_Context& swapchain, const Frame_Render_Target* targets,
			uint32_t target_count, const Frame_Render_Item* items, uint32_t item_count,
			const Frame_Render_Item* post_items, uint32_t post_count, const float clear_color[4],
			bool& rebuild_needed_out, Frame_Readback_Request* readback = nullptr);
		/*
		实际绘制量（第十一轮新增）
		区分「登记量」（上层登记的渲染项数）与「实际绘制量」（真正下达绘制命令的量）：
		items_record 会跳过不完整项（管线 / 缓冲缺失、索引数或实例数为 0），故实际量可能小于登记量。
		*/
		struct Recorded_Counts
		{
			//实际绘制的项数
			uint32_t items = 0;
			//实际绘制的实例数
			uint64_t instances = 0;
		};

		//已推进帧数
		uint32_t frame_count_get(void) const;
		/*
		本次 render 将要使用的帧槽序号（frame_count % frame_overlap）
		供门面在调用 render 前取用「同一槽」的场景 UBO / 描述符集，保证与帧内部一致。
		*/
		uint32_t slot_index_get(void) const;
		/*
		等待本帧槽的完成栅栏
		供门面在写入「本帧槽」的资源（如场景 UBO）之前调用，避免覆盖 GPU 仍在读取的上一轮内容；
		render 内部还会再等一次，届时栅栏已发出，vkWaitForFences 立即返回，无副作用。
		返回假视为硬错误（等待失败或帧上下文未构建）。
		*/
		bool slot_wait(void);
		/*
		等待全部帧槽的完成栅栏
		供门面在「写满全部帧槽描述符集」或「重写共享采样器 / 重建共享采样器」之前调用，
		避免在命令缓冲仍在途时更新（或销毁）描述符集所引用的资源。
		*/
		bool frames_wait(void);
		//在途帧数量（= 帧槽数；供上层决定延迟销毁的释放帧号）
		uint32_t in_flight_count_get(void) const;
		/*
		帧统计（只读观测值；供门面 frame_stats_get 取用）
		last_frame_ms 为最近一帧的主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用，不含 GPU 时间戳）；
		total_draw_items / total_instances 为自构建以来的累计绘制项数与实例数。
		*/
		double last_frame_ms_get(void) const;
		//累计主机侧耗时（毫秒；自构建以来各帧「录制 ＋ 提交 ＋ 呈现调用」之和）
		double total_frame_ms_get(void) const;
		/*
		清零累计统计（第九十一轮）
		frames 以外的累计量（绘制项 / 实例 / 实际绘制 / 累计耗时 / GPU 样本）全部归零；
		最近一帧的即时量（last_frame_ms / last_gpu_frame_ms / last_post_stages）保持不变。
		*/
		void totals_reset(void);
		uint64_t total_draw_items_get(void) const;
		uint64_t total_instances_get(void) const;
		//累计实际绘制项数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_items_get(void) const;
		//累计实际绘制实例数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_instances_get(void) const;
		/*
		GPU 时间戳能力（设备与图形队列族均支持时为真；为假时 GPU 耗时字段恒 0 且不报错）
		供门面 gpu_timestamp_supported 取用。
		*/
		bool gpu_timestamp_supported_get(void) const;
		//最近一帧 GPU 侧耗时（毫秒；无能力或尚未读回时为 0）
		double last_gpu_frame_ms_get(void) const;
		//最近一帧场景 pass 的 GPU 耗时（毫秒；第一百二十八轮）
		double last_gpu_scene_ms_get(void) const;
		//最近一帧后处理各级合计的 GPU 耗时（毫秒；无后处理时为 0）
		double last_gpu_post_ms_get(void) const;
		//最近一次阴影 pass 的 GPU 耗时（毫秒；未启用阴影或能力不可用时为 0）
		double last_gpu_shadow_ms_get(void) const;
		/*
		读回最近一次阴影 pass 的 GPU 耗时（毫秒；第一百二十八轮）
		须在阴影 pass 提交且等尽在途帧之后调用；无能力 / 未写过 / 结果不可用时返回 0 且不改动已有值。
		*/
		double shadow_gpu_ms_read(void);
		//累计 GPU 侧耗时（毫秒；自构建以来各次成功读回的时间戳之和；无能力时恒 0）
		double total_gpu_frame_ms_get(void) const;
		//已读回 GPU 时间戳的样本数（无能力 / 该帧槽首轮未写时间戳时不增；自构建以来累计）
		uint64_t gpu_sample_count_get(void) const;
		//最近一帧实际执行的后处理级数（0 = 未走后处理链；第二十二轮新增）
		uint32_t last_post_stages_get(void) const;
		//最近一帧实际使用的采样数（第一百零一轮；1 = 单采样）
		uint32_t last_sample_count_get(void) const;
		/*
		压入延迟销毁条目（指示「release_frame 帧及以后」可释放）
		条目中的旧资源（旧交换链 / 图像视图 / 两类信号量 / 深度图像与内存与视图）由帧模块在到期时释放；
		由交换链重建路径在移交旧资源后调用，延迟通常取「在途帧数 + 1」。
		*/
		void deferred_push(Deferred_Entry entry, uint32_t delay_frames);
	private:
		/*
		等待某帧槽前一次提交完成（实现见 core/帧槽创建.cpp）
		快速路径：先查栅栏状态，已完成则免去一次阻塞等待调用；
		未完成或查询出错才落到 vkWaitForFences / 错误返回。
		*/
		bool slot_fence_wait(Frame_Slot& slot);
		//创建单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		bool frame_slot_create(Frame_Slot& slot);
		//销毁单个帧槽及其对象（实现见 core/帧槽创建.cpp）
		void frame_slot_destroy(Frame_Slot& slot);
		/*
		录制命令缓冲（实现见 core/命令录制.cpp）
		颜色 / 深度附件布局转换为动态渲染，随后按渲染项依次绑定并 vkCmdDrawIndexed；
		场景 pass 结束后若给定 UI 回调则交还上层绘制 UI；
		若给定回读缓冲则在呈现转换前转入 TRANSFER_SRC 并把颜色附件拷入该缓冲（再经呈现前布局转换收尾）。
		*/
		void command_record(VkCommandBuffer command_buffer, VkImage image, VkImageView image_view,
			VkImageView depth_view, VkExtent2D extent,
			const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr, VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE,
			VkImage msaa_image = VK_NULL_HANDLE, VkImageView msaa_color_view = VK_NULL_HANDLE,
			VkImageView msaa_depth_view = VK_NULL_HANDLE, uint32_t sample_count = 1);
		/*
		录制「渲染到离屏目标」的命令缓冲（实现见 core/命令录制.cpp）
		颜色进入布局为 SHADER_READ_ONLY_OPTIMAL，故先转入 COLOR_ATTACHMENT_OPTIMAL；
		场景 pass 结束后按是否回读转入 TRANSFER_SRC（回读）或直接转回 SHADER_READ_ONLY，使目标帧间可采样。
		*/
		void command_record_target(VkCommandBuffer command_buffer, const Frame_Render_Target& target,
			const Frame_Render_Item* items, uint32_t item_count, const float clear_color[4],
			const Frame_Ui_Callback* ui_callback = nullptr, VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE, bool shadow_pass = false);
		/*
		录制「场景 pass ＋ N 级后处理 pass（多目标轮转）＋ 呈现」的命令缓冲（实现见 core/命令录制.cpp）
		场景 pass：targets[0] 颜色 SHADER_READ_ONLY → COLOR_ATTACHMENT，绘制 items 后转回 SHADER_READ_ONLY；
		后处理各级：源（targets[i % target_count]）恒 SHADER_READ_ONLY → 以 post_items[i] 全屏绘制到宿
		（末级为交换链，否则 targets[(i + 1) % target_count]，写完后转回 SHADER_READ_ONLY）；
		交换链图像收尾按是否回读转入 TRANSFER_SRC + PRESENT_SRC 或直接转 PRESENT_SRC（与既有路径同口径）。
		*/
		void command_record_chain_present(VkCommandBuffer command_buffer, VkImage swapchain_image,
			VkImageView swapchain_view, VkImageView swapchain_depth_view, VkExtent2D swapchain_extent,
			const Frame_Render_Target* targets, uint32_t target_count,
			const Frame_Render_Item* items, uint32_t item_count, const Frame_Render_Item* post_items,
			uint32_t post_count, const float clear_color[4], VkBuffer readback_buffer = VK_NULL_HANDLE,
			VkQueryPool timestamp_pool = VK_NULL_HANDLE,
			VkImage swapchain_msaa_image = VK_NULL_HANDLE, VkImageView swapchain_msaa_color_view = VK_NULL_HANDLE,
			VkImageView swapchain_msaa_depth_view = VK_NULL_HANDLE, uint32_t swapchain_sample_count = 1);
		/*
		逐渲染项绘制（状态缓存的管线 / 描述符集绑定 ＋ vkCmdDrawIndexed；实现见 core/命令录制.cpp）
		由 command_record（交换链路径）与 command_record_target（离屏路径）共用。
		*/
		Recorded_Counts items_record(VkCommandBuffer command_buffer, const Frame_Render_Item* items, uint32_t item_count);
		//提交并呈现（实现见 core/提交呈现.cpp；render_finished 为按交换链图像持有的呈现完成信号量）
		bool submit_present(Frame_Slot& slot, uint32_t image_index, VkSemaphore image_available,
			VkSemaphore render_finished, Swapchain_Context& swapchain, bool& rebuild_needed_out);
		/*
		累计本帧绘制统计（实现见 core/提交呈现.cpp）
		绘制项数按登记项计；实例数取各绘制项 instance_count 之和（0 表该项不完整、无实例可计）。
		*/
		void frame_stats_accumulate(const Frame_Render_Item* items, uint32_t item_count);
		/*
		读回某帧槽上一轮 GPU 时间戳并换算毫秒（实现见 core/提交呈现.cpp）
		调用时机：该槽栅栏等待完成之后、本帧录制之前（此时上一轮已执行完，结果可读）。
		无查询池 / 尚未写过 / 结果不可用时静默跳过，不改动 last_gpu_frame_ms。
		*/
		void gpu_timestamp_read(Frame_Slot& slot);
		//释放到期的延迟销毁条目（实现见 core/延迟销毁.cpp）
		void deferred_flush(void);
		//释放单个延迟条目的资源（实现见 core/延迟销毁.cpp）
		void deferred_entry_release(Deferred_Entry& entry);
		/*
		立即释放全部延迟销毁条目（销毁期调用；实现见 core/延迟销毁.cpp）
		关闭时已等设备空闲，无需再按 release_frame 判据，直接释放全部残留条目，
		避免旧交换链附属资源（尤其深度图像与其内存）泄漏到内存模块销毁之后。
		*/
		void deferred_release_all(void);
		/*
		确保回读缓冲就绪（实现见 core/像素回读.cpp）
		尚未创建或字节数不足时按需创建 / 重建（主机可见，用途 TRANSFER_DST）；
		返回假视为硬错误。
		*/
		bool readback_buffer_ensure(VkDeviceSize size);
		//销毁回读缓冲（实现见 core/像素回读.cpp）
		void readback_buffer_destroy(void);
		/*
		帧后读出像素（实现见 core/像素回读.cpp）
		等本帧槽栅栏（拷贝与渲染同在一个命令缓冲，栅栏发出即数据就绪）→ 主机读回 →
		回填宽 / 高 / 字节数。
		*/
		bool readback_read(Frame_Slot& slot, Frame_Readback_Request& request, VkExtent2D extent);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//物理设备
		VkPhysicalDevice physical_device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//呈现队列族索引
		uint32_t present_family = 0;
		//呈现队列
		VkQueue present_queue = VK_NULL_HANDLE;
		//在途帧数
		uint32_t frame_overlap = 0;
		//帧槽集合（数量 = frame_overlap）
		std::vector<Frame_Slot> slots;
		//图像可用信号量轮转游标
		uint32_t acquire_cursor = 0;
		//已推进帧数（同时用作帧槽轮转依据与延迟销毁的释放判据）
		uint32_t frame_count = 0;
		//最近一帧主机侧耗时（毫秒；录制 ＋ 提交 ＋ 呈现调用）
		double last_frame_ms = 0.0;
		//累计主机侧耗时（毫秒；自构建以来）
		double total_frame_ms = 0.0;
		//累计绘制项数（自构建以来）
		uint64_t total_draw_items = 0;
		//累计实例数（按登记项计；含被跳过的不完整项）
		uint64_t total_instances = 0;
		//累计实际绘制项数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_items = 0;
		//累计实际绘制实例数（第十一轮；不含被跳过的不完整项）
		uint64_t total_drawn_instances = 0;
		//本帧已录制期间累计的实际绘制量（每次 render 起始处清零，录制时由 items_record 递增）
		Recorded_Counts frame_recorded{};
		//GPU 时间戳是否可用（设备支持图形与计算时间戳 ＋ 周期有效 ＋ 图形队列族有效位非零）
		bool timestamp_supported = false;
		//时间戳周期（纳秒 / tick；用于把 tick 差换算为时间）
		float timestamp_period = 0.0f;
		//图形队列族时间戳有效位（小于 64 时读回后须掩去高位）
		uint32_t timestamp_valid_bits = 0;
		//最近一帧 GPU 侧耗时（毫秒；无能力或尚未读回时为 0）
		double last_gpu_frame_ms = 0.0;
		//最近一帧场景 pass 耗时（毫秒；第一百二十八轮）
		double last_gpu_scene_ms = 0.0;
		//最近一帧后处理耗时（毫秒；无后处理时为 0）
		double last_gpu_post_ms = 0.0;
		//最近一次阴影 pass 耗时（毫秒）
		double last_gpu_shadow_ms = 0.0;
		//最近一次阴影 pass 使用的帧槽（供阴影时间戳读回定位）
		uint32_t last_shadow_slot = 0;
		//累计 GPU 侧耗时（毫秒；自构建以来各次成功读回之和）
		double total_gpu_frame_ms = 0.0;
		//已读回 GPU 时间戳的样本数（自构建以来）
		uint64_t gpu_sample_count = 0;
		//最近一帧实际执行的后处理级数（非链路径清零；第二十二轮新增）
		uint32_t last_post_stages = 0;
		//最近一帧实际使用的采样数（第一百零一轮；1 = 单采样）
		uint32_t last_sample_count = 1;
		//延迟销毁队列
		std::vector<Deferred_Entry> deferred_entries;
		//内存模块（像素回读缓冲的创建 / 销毁依赖它）
		Memory_Context* memory = nullptr;
		//像素回读缓冲（主机可见；按需创建，交换链尺寸变化时重建）
		VkBuffer readback_buffer = VK_NULL_HANDLE;
		//像素回读缓冲内存
		VmaAllocation readback_alloc = VK_NULL_HANDLE;
		//像素回读缓冲当前字节数
		VkDeviceSize readback_size = 0;
	};
}
