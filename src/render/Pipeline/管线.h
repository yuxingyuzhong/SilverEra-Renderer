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
	管线上下文
	持有描述符集布局、管线布局、描述符池与图形管线；set0 = 场景 UBO / set1 = 采样器。
	生命周期：build → pipeline_build → 使用 → destroy；destroy 幂等，析构兜底。
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
		//销毁（幂等）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		//构建图形管线（顶点输入 position vec3 + uv vec2；推送常量 mat4 模型矩阵；深度测试开 / 写开；背面剔除 BACK；颜色附件混合态由 blend_mode 决定）
		bool pipeline_build(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode = Blend_Mode::Opaque);
		/*
		按交换链格式与混合档重建图形管线
		图形管线把颜色 / 深度格式固化在 VkPipelineRenderingCreateInfo 中、把混合态固化在颜色混合状态里，
		交换链重建若换了格式（颜色或深度），或上层切了混合档，旧管线即与渲染目标 / 期望混合不一致，必须重建；
		格式与混合档均未变时不做任何操作（空操作，无等待、无重建开销）。
		*/
		bool pipeline_rebuild(VkFormat color_format, VkFormat depth_format, Blend_Mode blend_mode = Blend_Mode::Opaque);

		//获取图形管线
		VkPipeline pipeline_get(void) const;
		//获取管线布局
		VkPipelineLayout layout_get(void) const;
		//第 slot 帧槽的场景 UBO 描述符集（set 0）
		VkDescriptorSet scene_set_get(uint32_t slot) const;
		//第 slot 帧槽的采样器描述符集（set 1）
		VkDescriptorSet sampler_set_get(uint32_t slot) const;
		//向第 slot 帧槽写场景 UBO（set 0）
		bool scene_bind(uint32_t slot, VkBuffer ubo_buffer);
		//向第 slot 帧槽写采样器（set 1）
		bool texture_bind(uint32_t slot, VkSampler sampler, VkImageView view);
	private:
		//描述符集布局创建（实现见 core/描述符布局.cpp）
		bool descriptor_layouts_create(void);
		//管线布局创建（实现见 core/管线布局.cpp）
		bool pipeline_layout_create(void);
		//图形管线创建（实现见 core/图形管线.cpp）
		bool graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//配置
		Pipeline_Config config;
		//set 0 布局：场景 UBO
		VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
		//set 1 布局：采样器
		VkDescriptorSetLayout sampler_set_layout = VK_NULL_HANDLE;
		//管线布局
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		//描述符池
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		//场景 UBO 描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> scene_sets;
		//采样器描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> sampler_sets;
		//图形管线
		VkPipeline pipeline = VK_NULL_HANDLE;
		//图形管线创建时所用的顶点着色器模块（供格式变化时重建复用；模块所有权仍归上层）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//图形管线创建时所用的片元着色器模块（供格式变化时重建复用；模块所有权仍归上层）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//当前图形管线所用的混合档（切档即重建；供重建判据与日志）
		Blend_Mode blend_mode = Blend_Mode::Opaque;
	};
}
