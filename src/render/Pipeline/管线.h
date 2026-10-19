#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
//管线公共语义类型（Vulkan-free）：混合档
#include "Application/Renderer/src/render/Pipeline/管线数据.h"

//游戏引擎命名空间
namespace engine
{
	//图形管线配置
	struct Pipeline_Config
	{
		//颜色附件格式（交换链格式）
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		//深度附件格式
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//在途帧数（描述符集按帧槽分配）
		uint32_t frame_overlap = 2;
	};

	/*
	管线条目
	一条图形管线 = 顶点 / 片元着色器模块 ＋ 混合档（颜色 / 深度格式由配置统一烘焙）。
	句柄 0 保留给「默认场景管线」，具名管线句柄自 1 起由本模块分配。
	着色器模块所有权一律在调用方（门面）：本模块只借用，销毁时不在内部释放。
	*/
	struct Pipeline_Entry
	{
		//句柄（0 = 默认场景管线；≥ 1 = 具名管线）
		uint64_t handle = 0;
		//顶点着色器模块（所有权在调用方）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//片元着色器模块（所有权在调用方）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//混合档（登记时确定）
		Blend_Mode blend_mode = Blend_Mode::Opaque;
		//图形管线
		VkPipeline pipeline = VK_NULL_HANDLE;
	};

	/*
	管线上下文
	持有描述符集布局、管线布局、描述符池与若干图形管线；
	set0 = 场景 UBO / set1 = 采样器，**全部图形管线共享同一管线布局**。
	生命周期：build → pipeline_build / pipeline_register → 使用 → destroy；destroy 幂等，析构兜底。
	*/
	class Pipeline_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Pipeline_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Pipeline_Context(void);
		Pipeline_Context(const Pipeline_Context&) = delete;
		Pipeline_Context& operator=(const Pipeline_Context&) = delete;

		//构建（建立描述符集布局 set0=场景UBO / set1=采样器、管线布局、描述符池并分配每帧槽描述符集）
		bool build(VkDevice device, const Pipeline_Config& config);
		//销毁（幂等；销毁全部图形管线与基础设施）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		/*
		登记 / 替换并激活句柄 0「默认场景管线」
		顶点输入 position vec3 + uv vec2；实例输入 mat4 模型矩阵；推送常量 mat4；深度测试开 / 写开；背面剔除 BACK。
		*/
		bool pipeline_build(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode = Blend_Mode::Opaque);
		/*
		登记具名图形管线
		句柄自 1 起递增且稳定不变；不改变当前激活管线；成功时写出句柄。
		具名管线与默认管线共享同一管线布局与描述符集布局，仅允许「着色器对 + 混合档」不同。
		*/
		bool pipeline_register(uint64_t& handle_out, VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode = Blend_Mode::Opaque);
		//切换当前激活管线（句柄不存在则返回假并保持激活不变）
		bool pipeline_activate(uint64_t handle);
		//注销具名管线（句柄 0 忽略；一并销毁其图形管线，不触碰着色器模块 —— 所有权在调用方）
		void pipeline_release(uint64_t handle);
		/*
		按新颜色 / 深度格式重建全部已登记图形管线
		颜色 / 深度格式被烘焙进 VkPipelineRenderingCreateInfo，交换链换格式后所有管线都必须重建；
		着色器模块与各自混合档保留；格式未变时为空操作（无等待、无重建开销）。
		*/
		bool pipeline_rebuild(VkFormat color_format, VkFormat depth_format);
		//重建句柄 0 默认管线的混合档（供门面 blend_mode_set；无默认管线时为空操作）
		bool pipeline_default_blend_set(Blend_Mode blend_mode);
		//句柄是否已登记
		bool pipeline_has(uint64_t handle) const;

		//获取当前激活图形管线
		VkPipeline pipeline_get(void) const;
		//获取指定句柄的图形管线（未登记返回空；不改变当前激活管线）
		VkPipeline pipeline_by_handle_get(uint64_t handle) const;
		//获取管线布局（全管线共享）
		VkPipelineLayout layout_get(void) const;
		//第 slot 帧槽的场景 UBO 描述符集（set 0）
		VkDescriptorSet scene_set_get(uint32_t slot) const;
		//第 slot 帧槽的采样器描述符集（set 1）
		VkDescriptorSet sampler_set_get(uint32_t slot) const;
		/*
		第 slot 帧槽的后处理采样器描述符集（set 1）
		与 sampler_set_get 同布局，但独立分配：使「同帧多 pass」中，场景 pass 用 sampler_set
		采样场景纹理，后处理 pass 用 post_set 采样离屏目标，两 pass 互不覆盖对方绑定。
		*/
		VkDescriptorSet post_set_get(uint32_t slot) const;
		//向第 slot 帧槽写场景 UBO（set 0）
		bool scene_bind(uint32_t slot, VkBuffer ubo_buffer);
		//向第 slot 帧槽写采样器（set 1）
		bool texture_bind(uint32_t slot, VkSampler sampler, VkImageView view);
		//向第 slot 帧槽的后处理采样器集写采样器（set 1）
		bool post_texture_bind(uint32_t slot, VkSampler sampler, VkImageView view);
	private:
		//描述符集布局创建（实现见 core/描述符布局.cpp）
		bool descriptor_layouts_create(void);
		//管线布局创建（实现见 core/管线布局.cpp）
		bool pipeline_layout_create(void);
		//图形管线创建（实现见 core/图形管线.cpp；结果写入 pipeline_out，不改动条目）
		bool graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode, VkPipeline& pipeline_out);
		//按句柄查找条目（未找到返回 nullptr）
		Pipeline_Entry* entry_find(uint64_t handle);
		//按句柄查找条目（只读）
		const Pipeline_Entry* entry_find(uint64_t handle) const;
		//按当前配置格式结合条目自身着色器 / 混合档创建图形管线（失败时条目管线保持空）
		bool entry_pipeline_create(Pipeline_Entry& entry);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//配置（颜色 / 深度格式）
		Pipeline_Config config;
		//set 0 布局：场景 UBO
		VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
		//set 1 布局：采样器
		VkDescriptorSetLayout sampler_set_layout = VK_NULL_HANDLE;
		//管线布局（全管线共享）
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		//描述符池
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		//场景 UBO 描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> scene_sets;
		//采样器描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> sampler_sets;
		//后处理采样器描述符集（长度 = frame_overlap；同布局、独立分配）
		std::vector<VkDescriptorSet> post_sets;
		//图形管线条目（句柄 0 为默认场景管线，其余为具名管线）
		std::vector<Pipeline_Entry> pipelines;
		//下一个待分配的具名管线句柄（自 1 起递增）
		uint64_t next_handle = 1;
		//当前激活管线句柄
		uint64_t active_handle = 0;
	};
}
