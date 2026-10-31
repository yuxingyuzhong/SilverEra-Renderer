#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
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
	//计算上下文（前置声明；第二百五十四轮：金字塔生成经其调度与描述符集）
	class Compute_Context;

	//Hi-Z 金字塔级数上限（12 级足够覆盖 4096×4096；超出按此截断，末级仍为 1×1 量级）
	constexpr uint32_t HIZ_LEVEL_MAX = 12;
	/*
	Hi-Z 金字塔格式（第二百五十三轮）
	单通道 32 位浮点：深度拷贝与降采样都按「最小深度更近」取最大值（保守），
	该格式在桌面显卡上普遍支持 STORAGE_IMAGE，便于 254 轮以计算着色器逐级生成。
	*/
	constexpr VkFormat HIZ_PYRAMID_FORMAT = VK_FORMAT_R32_SFLOAT;

	/*
	Hi-Z 上下文（第二百五十三轮）
	职责：按帧槽持有「深度历史纹理（上帧场景深度拷贝）」与「mip 金字塔图像 ＋ 逐级视图」，
	并提供「场景深度 → 历史」的录制辅助；**不负责**遮挡判定与调度（255 / 256 轮另做）。

	深度来源核查结论（规划 §2.4）：交换链深度按「交换链图像」轮转，离屏深度归渲染目标，
	均非每在途帧槽一份，故此处为每帧槽各建一份历史，历史语义 = 同一槽上一次使用时的场景深度。

	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	*/
	class HiZ_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		HiZ_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~HiZ_Context(void);
		//不可拷贝、不可移动（持有 VMA 句柄）
		HiZ_Context(const HiZ_Context&) = delete;
		HiZ_Context& operator=(const HiZ_Context&) = delete;

		/*
		构建
		按 frame_overlap 建立逐帧槽资源；depth_format 为场景深度格式（历史纹理与其同格式，
		`vkCmdCopyImage` 要求源 / 目标格式一致）。级数按 max(width, height) 逐级折半。
		任一步失败整体回滚并返回假。
		*/
		bool build(VkDevice device, Memory_Context& memory, uint32_t frame_overlap,
			uint32_t width, uint32_t height, VkFormat depth_format);
		//销毁（幂等）
		void destroy(void);
		//是否已构建完成（设备、槽数与图像齐备）
		bool valid(void) const;

		/*
		尺寸 / 格式设置（交换链重建后调用）
		尺寸或深度格式变化时重建全部槽；完全一致时为空操作并返回真。
		*/
		bool extent_set(uint32_t width, uint32_t height, VkFormat depth_format);
		//当前宽度（未构建为 0）
		uint32_t width_get(void) const;
		//当前高度（未构建为 0）
		uint32_t height_get(void) const;
		//当前深度格式（未构建为 UNDEFINED）
		VkFormat depth_format_get(void) const;
		//金字塔级数（未构建为 0）
		uint32_t level_count(void) const;
		//第 level 级尺寸（越界或未构建返回假且出参归零）
		bool level_extent_get(uint32_t level, uint32_t& width_out, uint32_t& height_out) const;
		//帧槽数量（未构建为 0）
		uint32_t slot_count(void) const;

		//第 slot 帧槽的深度历史图像（无效返回空）
		VkImage depth_history_image(uint32_t slot) const;
		//第 slot 帧槽的深度历史视图（采样用途；无效返回空）
		VkImageView depth_history_view(uint32_t slot) const;
		//第 slot 帧槽的金字塔图像（无效返回空）
		VkImage pyramid_image(uint32_t slot) const;
		//第 slot 帧槽第 level 级的视图（存储 / 采样用途；越界返回空）
		VkImageView pyramid_level_view(uint32_t slot, uint32_t level) const;
		/*
		第 slot 帧槽金字塔的完整链视图（第二百五十六轮）
		供剔除着色器以 texelFetch(…, level) 读取任意级；越界返回空。
		*/
		VkImageView pyramid_full_view(uint32_t slot) const;
		//深度采样器（最近邻 ＋ 钳边；供剔除着色器的组合图像采样器绑定）
		VkSampler depth_sampler_get(void) const;

		//该槽是否已有有效历史（重建 / 尺寸变化后清零；首帧据此跳过遮挡）
		bool history_valid(uint32_t slot) const;
		//作废该槽历史（无历史时为空操作）
		void history_invalidate(uint32_t slot);

		/*
		搭建计算调度关联（第二百五十四轮）
		金字塔生成需要经计算模块写描述符集与调度；须在 build 之后调用，传空即解除关联。
		*/
		void compute_set(Compute_Context* compute);
		/*
		生成金字塔（第二百五十四轮）
		录制内容：场景深度转采样布局 → 深度拷贝 compute（写入历史）→ 逐级降采样 compute（取 2×2 最大值）
		→ 场景深度恢复深度附件布局。depth_image / depth_view 为场景单采样深度（进入时 DEPTH_ATTACHMENT）。
		depth_copy_pipeline / downsample_pipeline 为本周期的两个计算管线句柄；任一无效或关联缺失时返回假且不录制。
		完成后该槽 history_valid 置真、generation_count 累加。
		*/
		bool pyramid_generate(VkCommandBuffer command_buffer, uint32_t slot, VkImage depth_image, VkImageView depth_view,
			uint64_t depth_copy_pipeline, uint64_t downsample_pipeline);
		//累计金字塔生成次数（诊断用）
		uint64_t generation_count(void) const;

	private:
		//单个帧槽资源
		struct Slot
		{
			/*
			深度历史图像（R32_SFLOAT；第二百五十四轮改为颜色面）
			深度拷贝 compute 以采样方式读场景深度、以存储图像方式写入本图，故须颜色面 ＋ 存储用途。
			*/
			VkImage history_image = VK_NULL_HANDLE;
			//深度历史内存
			VmaAllocation history_allocation = VK_NULL_HANDLE;
			//深度历史视图（颜色面；采样 / 存储两用）
			VkImageView history_view = VK_NULL_HANDLE;
			//金字塔图像（R32_SFLOAT，含完整 mip 链）
			VkImage pyramid_image = VK_NULL_HANDLE;
			//金字塔内存
			VmaAllocation pyramid_allocation = VK_NULL_HANDLE;
			//完整 mip 链视图（第二百五十六轮；剔除着色器按级 texelFetch）
			VkImageView pyramid_view = VK_NULL_HANDLE;
			//逐级视图（baseMipLevel = 级号，levelCount = 1）
			std::vector<VkImageView> level_views;
			//是否已有有效历史
			bool history_valid = false;
		};
		//创建单个槽的全部资源（失败时该槽已回滚）
		bool slot_create(Slot& slot);
		//销毁单个槽的全部资源（幂等）
		void slot_destroy(Slot& slot);
		//销毁全部槽并清空尺寸状态
		void slots_destroy(void);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//内存模块（图像创建 / 销毁）
		Memory_Context* memory = nullptr;
		//计算上下文（第二百五十四轮；生成时写描述符集与调度）
		Compute_Context* compute = nullptr;
		/*
		深度采样器（第二百五十四轮；最近邻 ＋ 钳边 ＋ 点采样）
		深度拷贝着色器按纹素取深度，须避免线性过滤把边界深度平滑成更近值（破坏保守性）。
		*/
		VkSampler depth_sampler = VK_NULL_HANDLE;
		//深度历史格式（固定 R32_SFLOAT；场景深度格式仅作记录）
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//当前尺寸
		uint32_t width = 0;
		uint32_t height = 0;
		//金字塔级数
		uint32_t levels = 0;
		//逐帧槽资源
		std::vector<Slot> slots;
		//累计金字塔生成次数
		uint64_t generation_total = 0;
	};
}
