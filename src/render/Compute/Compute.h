#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
#include <functional>
//计算公共语义类型（Vulkan-free）：配置与统计
#include "Application/Renderer/src/render/Compute/计算数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	计算管线上下文（第二百四十一轮）
	职责：独立的计算描述符集布局（set 0 = 若干存储缓冲）、计算管线布局、计算管线缓存、
	每帧槽描述符集分配、单次提交辅助与调度封装。**不负责**存储缓冲的分配与内容管理（门面 / 内存模块负责）。

	与图形管线上下文的关系：完全独立（自有布局 / 池 / 管线），不共享 set 0 / set 1，
	避免图形侧新增字段导致计算布局失效（见方向一规划 §2.2）。

	生命周期：build → pipeline_register → 使用 → destroy；destroy 幂等，析构兜底。
	着色器模块所有权一律在调用方（门面），本类只借用。
	*/
	class Compute_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Compute_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Compute_Context(void);
		Compute_Context(const Compute_Context&) = delete;
		Compute_Context& operator=(const Compute_Context&) = delete;

		/*
		构建
		探测 graphics_family 是否具备 COMPUTE 位（具备则复用该队列族，否则 supported 置假并返回真 —— 不视为构建失败）；
		创建计算描述符集布局 / 管线布局 / 描述符池 / 每帧槽描述符集 / 一次性命令池与围栏。
		*/
		bool build(VkPhysicalDevice physical_device, VkDevice device, uint32_t graphics_family, VkQueue graphics_queue,
			const Compute_Config& config);
		//销毁（幂等；销毁全部计算管线与基础设施）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;
		//本机是否支持计算路径（队列族含 COMPUTE 位）
		bool supported(void) const;

		/*
		登记计算管线条目
		句柄自 1 起递增且稳定；成功时写出句柄。着色器模块须为已编译的 .comp SPIR-V，入口固定 main。
		*/
		bool pipeline_register(uint64_t& handle_out, VkShaderModule module);
		//切换当前激活计算管线（句柄不存在则返回假并保持激活不变）
		bool pipeline_activate(uint64_t handle);
		//当前激活计算管线（无则空）
		VkPipeline pipeline_get(void) const;
		//已登记计算管线数量
		uint32_t pipeline_count(void) const;
		//计算管线布局（全计算管线共享）
		VkPipelineLayout layout_get(void) const;

		/*
		第 slot 帧槽的计算描述符集
		binding 0..storage_buffer_count-1 为存储缓冲，由 storage_bind 逐条写入。
		*/
		VkDescriptorSet set_get(uint32_t slot) const;
		/*
		向第 slot 帧槽写存储缓冲（binding ∈ [0, storage_buffer_count)）
		offset / size 允许为整个缓冲或其中一段；越界参数返回假。
		*/
		bool storage_bind(uint32_t slot, uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

		/*
		单次提交辅助
		在图形队列上开一次性命令缓冲、调用 record 录制、提交并等待围栏（超时 5 秒）。
		供自检 / 回读 / 单元测试使用；**不得**用于常规帧（常规帧的 compute pass 由门面插入主命令缓冲）。
		*/
		bool single_submit(const std::function<bool(VkCommandBuffer)>& record);

		/*
		调度
		在给定命令缓冲内绑定指定计算管线、写入推送常量（可为空）、下发工作项，并累计 dispatch 计数。
		调用方负责在 dispatch 前后放合适的 VkMemoryBarrier2（见规划 §2.9）。
		*/
		bool compute_dispatch(VkCommandBuffer command_buffer, uint64_t handle, uint32_t group_x, uint32_t group_y, uint32_t group_z,
			const void* push_data = nullptr, uint32_t push_size = 0);
		//指定后续调度使用的帧槽（默认 0；门面按在途帧槽切换）
		bool slot_set(uint32_t slot);
		//当前帧槽（默认 0）
		uint32_t slot_get(void) const;

		/*
		按元素数调度（第二百四十二轮）
		group 数由 compute_group_count(elements, local_size) 换算；elements 为 0 时直接返回真（不调度）。
		*/
		bool dispatch_ceil(VkCommandBuffer command_buffer, uint64_t handle, uint32_t elements, uint32_t local_size,
			const void* push_data = nullptr, uint32_t push_size = 0);

		/*
		屏障（第二百四十二轮）
		把 Compute_Barrier 枚举展开为 VkMemoryBarrier2 ＋ VkDependencyInfo 并下发；
		用途与阶段/访问对应关系见 计算数据.h 的枚举注释。命令缓冲为空返回假。
		*/
		bool barrier(VkCommandBuffer command_buffer, Compute_Barrier purpose);

		//当前统计快照（dispatch 次数与最近计时；计时在第二百四十五轮接入前恒为 -1）
		Compute_Stats stats_get(void) const;

		/*
		内置健康检查（第二百四十一轮）
		分配一个 64 元素的主机可见存储缓冲，把 magic 写入每个元素后回读首元素：
		用于确认「计算管线创建 → 描述符绑定 → 推送常量 → 调度 → 内存可见性」整条链路可用。
		成功返回真并写出回读值。
		*/
		bool self_check_run(uint32_t magic, uint32_t& value_out);

		//累计调度次数（第二百四十一轮起可读；第二百四十五轮并入统计）
		uint64_t dispatch_count_get(void) const;
	private:
		//计算描述符布局创建（实现见 core/Compute.cpp）
		bool descriptor_layout_create(void);
		//计算管线布局创建
		bool pipeline_layout_create(void);
		//计算管线创建（结果写入 pipeline_out）
		bool compute_pipeline_create(VkShaderModule module, VkPipeline& pipeline_out);
		//按句柄查找管线条目（未找到返回 nullptr）
		struct Entry
		{
			//句柄（自 1 起）
			uint64_t handle = 0;
			//计算管线
			VkPipeline pipeline = VK_NULL_HANDLE;
		};
		Entry* entry_find(uint64_t handle);
	private:
		//物理设备（能力探测）
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//使用的队列族（与图形队列族相同）
		uint32_t family = 0;
		//图形队列（单次提交用）
		VkQueue queue = VK_NULL_HANDLE;
		//配置
		Compute_Config config;
		//是否支持计算路径
		bool supported_flag = false;
		//set 0 布局：存储缓冲数组
		VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
		//计算管线布局
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		//描述符池
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		//每帧槽描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> sets;
		//当前帧槽（调度时绑定该槽的描述符集）
		uint32_t active_slot = 0;
		//计算管线条目
		std::vector<Entry> pipelines;
		//下一个待分配句柄（自 1 起）
		uint64_t next_handle = 1;
		//当前激活句柄
		uint64_t active_handle = 0;
		//单次提交用命令池（图形队列族）
		VkCommandPool single_pool = VK_NULL_HANDLE;
		//单次提交用围栏
		VkFence single_fence = VK_NULL_HANDLE;
		//累计调度次数
		uint64_t dispatch_count = 0;
	};
}
