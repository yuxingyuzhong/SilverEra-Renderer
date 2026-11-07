#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 与 VMA 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露这两者的类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

//游戏引擎命名空间
namespace engine
{
	//内存上下文（前置声明；仅作引用入参，其定义由实现文件包含）
	class Memory_Context;

	/*
	渲染目标记录
	颜色图像（可作颜色附件 / 可被采样 / 可作传输源）＋ 深度图像（可作深度附件）＋ 两者视图。
	布局不变量：颜色恒处于 SHADER_READ_ONLY_OPTIMAL（帧间可采样；作附件时由帧链路进入 /
	            退出时切换到该布局），深度恒处于 DEPTH_ATTACHMENT_OPTIMAL（逐帧以 CLEAR 清空）。
	*/
	struct Render_Target_Record
	{
		//颜色图像
		VkImage color_image = VK_NULL_HANDLE;
		//颜色图像内存
		VmaAllocation color_alloc = VK_NULL_HANDLE;
		//颜色图像视图
		VkImageView color_view = VK_NULL_HANDLE;
		//深度图像
		VkImage depth_image = VK_NULL_HANDLE;
		//深度图像内存
		VmaAllocation depth_alloc = VK_NULL_HANDLE;
		//深度图像视图
		VkImageView depth_view = VK_NULL_HANDLE;
		/*
		MSAA 附件（第九十六轮；sample_count > 1 时有效，关闭时为空）
		颜色 / 深度均按 sample_count 采样，渲染结束时颜色解析（resolve）到上面的单采样颜色图像。
		*/
		VkImage msaa_color_image = VK_NULL_HANDLE;
		VmaAllocation msaa_color_alloc = VK_NULL_HANDLE;
		VkImageView msaa_color_view = VK_NULL_HANDLE;
		VkImage msaa_depth_image = VK_NULL_HANDLE;
		VmaAllocation msaa_depth_alloc = VK_NULL_HANDLE;
		VkImageView msaa_depth_view = VK_NULL_HANDLE;
		//颜色 / 深度格式（重建采样数时复用）
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//采样数（1 = 关闭）
		uint32_t sample_count = 1;
		//是否跟随上下文的默认采样数（假 = 固定采样数，如阴影目标固定 1）
		bool follow_sample_count = true;
		//渲染目标范围（像素）
		VkExtent2D extent = { 0, 0 };
		//记录是否有效
		bool valid = false;
	};

	/*
	渲染目标上下文
	持有若干离屏渲染目标，负责其颜色 / 深度图像与视图的创建、销毁与一次性布局转换。
	生命周期：build → target_create / target_destroy → destroy；destroy 幂等，析构兜底。
	句柄自 1 起（0 表无效），下标 = 句柄 - 1。
	*/
	class Render_Target_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Render_Target_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Render_Target_Context(void);
		//不可拷贝、不可移动（持有 VMA 句柄）
		Render_Target_Context(const Render_Target_Context&) = delete;
		Render_Target_Context& operator=(const Render_Target_Context&) = delete;

		//构建（记录逻辑设备 / 图形队列与内存模块）
		bool build(VkDevice device, uint32_t graphics_family, VkQueue graphics_queue, Memory_Context& memory);
		//销毁（幂等；销毁全部渲染目标与一次性转换资源）
		void destroy(void);
		//是否已构建完成（逻辑设备与内存模块齐备）
		bool valid(void) const;
		//当前有效渲染目标数量（第一百一十三轮；只读诊断）
		uint32_t target_count(void) const;

		/*
		创建渲染目标
		color_format / depth_format 由调用方给出（通常取交换链的颜色 / 深度格式）；
		颜色图像具备「颜色附件 ＋ 采样 ＋ 传输源」用法，深度图像具备「深度附件」用法；
		创建完成后执行一次布局转换，使颜色进入 SHADER_READ_ONLY、深度进入 DEPTH_ATTACHMENT。
		成功写出句柄（≥ 1）；失败返回假且句柄保持 0，不留下半初始化资源。
		sample_count（第九十六轮）：0 = 跟随上下文默认采样数；非 0 = 该目标固定按此采样数创建。
		*/
		bool target_create(uint64_t& handle_out, uint32_t width, uint32_t height,
			VkFormat color_format, VkFormat depth_format, uint32_t sample_count = 0);
		/*
		设置上下文默认采样数（第九十六轮）
		变化时重建全部「跟随默认采样数」的目标（句柄保持不变）；固定采样数的目标不受影响。
		失败返回假（此时已重建的目标可能处于新采样数，调用方应回退到旧值重试）。
		*/
		bool sample_count_set(uint32_t count);
		//当前上下文默认采样数
		uint32_t sample_count_get(void) const;
		//销毁渲染目标（句柄无效则忽略）
		void target_destroy(uint64_t handle);
		/*
		分离渲染目标资源（摘出记录但**不销毁**，归还责任移交调用方）
		供门面在运行时销毁路径把颜色 / 深度图像与视图压入帧延迟销毁队列；
		成功返回真并写出被摘出的记录；句柄无效或记录已空时返回假（不写出任何资源）。
		*/
		bool target_detach(uint64_t handle, Render_Target_Record& record_out);
		//取渲染目标各附件与范围（句柄无效返回假）
		bool target_get(uint64_t handle, VkImage& color_image_out, VkImageView& color_view_out,
			VkImageView& depth_view_out, VkExtent2D& extent_out) const;
		//取渲染目标尺寸（第七十七轮新增；句柄无效返回假且出参归零）
		bool target_extent_get(uint64_t handle, VkExtent2D& extent_out) const;
		/*
		取渲染目标的深度图像与视图（第一百二十三轮新增）
		供后处理阶段（景深）以纹理方式采样深度；句柄无效或记录已空时返回假且出参为空。
		注意：采样前调用方须把深度转入 SHADER_READ_ONLY_OPTIMAL，用完转回 DEPTH_ATTACHMENT_OPTIMAL。
		*/
		bool target_depth_get(uint64_t handle, VkImage& depth_image_out, VkImageView& depth_view_out) const;
		//取目标 MSAA 附件与采样数（第九十六轮；关闭 MSAA 时图像 / 视图为空且采样数为 1）
		bool target_msaa_get(uint64_t handle, VkImage& msaa_color_image_out, VkImageView& msaa_color_view_out,
			VkImageView& msaa_depth_view_out, uint32_t& sample_count_out) const;
		/*
		取目标 MSAA 深度图像（第一百九十轮）
		供场景 pass 结束时以「深度解析」把多重采样深度写入单采样深度图像（后处理阶段采样那张）。
		关闭 MSAA 时写出空图像（调用方据此跳过解析）。句柄无效或记录已空时返回假且出参为空。
		*/
		bool target_msaa_depth_image_get(uint64_t handle, VkImage& msaa_depth_image_out) const;
		/*
		读回渲染目标的颜色图像到主机缓冲（第二十六轮新增）
		pixels 为 RGBA8 紧排（逐行自上而下、无行填充），capacity 须不小于 宽×高×4；
		width_out / height_out 非空时回填实际尺寸。读取完成后颜色图像仍回到 SHADER_READ_ONLY。
		调用方须保证此刻无在途帧引用该目标（门面会先调 frames_wait）。
		*/
		bool target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回渲染目标的指定子矩形（第二十七轮新增）
		x / y 为左上角（像素），region_width / region_height 为子区域尺寸；须完整落在目标范围内，
		否则整次拒绝（返回假、尺寸出参保持 0）。pixels 为该子区域 RGBA8 紧排，capacity ≥ 区域宽×高×4。
		*/
		bool target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回渲染目标的深度图像（第二十八轮新增）
		values 为 32 位深度原始值（本层深度格式为 D32_SFLOAT，按 float 解释即可）；
		capacity 为 values 的**元素个数**，须不小于 宽×高；depth 图像读回后仍回到 DEPTH_ATTACHMENT。
		*/
		bool target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回深度图像的子区域（第一百六十三轮新增）
		x / y 为左上角，区域须完整落在目标内；values 为该区域 32 位深度原始值（按 float 解释）。
		MSAA 目标不可用（实际深度写入多重采样附件）；失败时尺寸出参归零。
		*/
		bool target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//句柄是否有效
		bool target_valid(uint64_t handle) const;
	private:
		//读回实现（整幅 / 子区域共用；区域须已校验）
		bool target_readback_impl(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		//确保一次性命令池与栅栏就绪（首次创建，之后复用）
		bool transient_resources_ensure(void);
		//一次性布局转换：颜色 UNDEFINED → SHADER_READ_ONLY、深度 UNDEFINED → DEPTH_ATTACHMENT（含 MSAA 附件）
		bool target_transition(Render_Target_Record& record);
		//创建 / 销毁记录的 MSAA 附件（第九十六轮）
		bool record_msaa_create(Render_Target_Record& record);
		void record_msaa_destroy(Render_Target_Record& record);
		//归还单个记录的图像与视图（内存模块就绪时；不触碰 valid 标记）
		void record_resources_destroy(Render_Target_Record& record);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//图形队列族索引
		uint32_t graphics_family = 0;
		//图形队列（一次性布局转换的提交目标）
		VkQueue graphics_queue = VK_NULL_HANDLE;
		//内存模块（图像创建 / 销毁）
		Memory_Context* memory = nullptr;
		//默认采样数（第九十六轮；1 = 关闭）
		uint32_t sample_count = 1;
		//一次性命令池（布局转换用；跨多个目标复用）
		VkCommandPool transient_pool = VK_NULL_HANDLE;
		//一次性完成栅栏（布局转换用；跨多个目标复用）
		VkFence transient_fence = VK_NULL_HANDLE;
		//渲染目标记录集合（下标 = 句柄 - 1）
		std::vector<Render_Target_Record> records;
	};
}
