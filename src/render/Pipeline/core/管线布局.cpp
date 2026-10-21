#include "src/render/Pipeline/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	/*
	创建管线布局
	描述符集顺序固定 set0（场景 UBO）→ set1（采样器）；
	推送常量：顶点 ＋ 片元阶段可见，offset 0，大小 112：
	  0..63   = mat4（文字路径占位；场景矩阵由实例属性承载）
	  64..79  = vec4 文字颜色
	  80..95  = vec4 后处理「源 / 宿尺寸」（x/y = 源宽高，z/w = 宿宽高）
	  96..111 = vec4 泛光参数（x = 阈值、y = 软膝、z = 强度、w 保留）
	*/
	bool Pipeline_Context::pipeline_layout_create(void)
	{
		//描述符集布局顺序（set 0 → set 1）
		VkDescriptorSetLayout set_layouts[2] = { scene_set_layout, sampler_set_layout };

		//推送常量范围（模型矩阵 ＋ 文字颜色 ＋ 后处理尺寸 ＋ 泛光参数）
		VkPushConstantRange push_range{};
		push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push_range.offset = 0;
		push_range.size = 112;

		//管线布局创建信息
		VkPipelineLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.setLayoutCount = 2;
		layout_info.pSetLayouts = set_layouts;
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &push_range;

		//创建管线布局
		if (!vk_result_check(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout), "vkCreatePipelineLayout"))
			return false;

		return true;
	}
}
