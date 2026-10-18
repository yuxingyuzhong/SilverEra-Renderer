#include "src/render/Pipeline/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//顶点布局（Vertex 步长与属性偏移据此推导）
#include "src/render/Mesh/网格数据.h"
//offsetof
#include <cstddef>

namespace engine
{
	/*
	顶点布局静态断言
	下方 binding 步长与属性偏移按字面量书写，此处断言其与 Mesh::Vertex 的实际布局一致，
	避免两侧各自漂移而静默错配（位置 vec3 在 0，纹理坐标 vec2 在 12，步长 20）。
	*/
	static_assert(sizeof(Vertex) == 20, "Mesh::Vertex 步长须为 20 字节（vec3 位置 + vec2 纹理坐标）");
	static_assert(offsetof(Vertex, position) == 0, "Mesh::Vertex::position 偏移须为 0");
	static_assert(offsetof(Vertex, uv) == 12, "Mesh::Vertex::uv 偏移须为 12");

	/*
	创建图形管线
	动态渲染（pNext 链入 VkPipelineRenderingCreateInfo，颜色 / 深度附件格式来自配置）；
	顶点输入 binding 0（step 20，位置 vec3 @0 / 纹理坐标 vec2 @12）；
	视口与裁剪动态设置；背面剔除 BACK、正面 COUNTER_CLOCKWISE（配合 cube_generate 绕序）；
	深度测试开 / 写开 / LESS；推送常量 mat4 模型矩阵。
	*/
	bool Pipeline_Context::graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module)
	{
		//着色器阶段（顶点 / 片元）
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert_module;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag_module;
		stages[1].pName = "main";

		//顶点绑定（binding 0，步长 = sizeof(Vertex) = 20）
		VkVertexInputBindingDescription vertex_binding{};
		vertex_binding.binding = 0;
		vertex_binding.stride = sizeof(Vertex);
		vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		//顶点属性（0：位置 vec3 @0；1：纹理坐标 vec2 @12）
		VkVertexInputAttributeDescription vertex_attributes[2]{};
		vertex_attributes[0].location = 0;
		vertex_attributes[0].binding = 0;
		vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attributes[0].offset = 0;
		vertex_attributes[1].location = 1;
		vertex_attributes[1].binding = 0;
		vertex_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertex_attributes[1].offset = 12;

		//顶点输入状态
		VkPipelineVertexInputStateCreateInfo vertex_input{};
		vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input.vertexBindingDescriptionCount = 1;
		vertex_input.pVertexBindingDescriptions = &vertex_binding;
		vertex_input.vertexAttributeDescriptionCount = 2;
		vertex_input.pVertexAttributeDescriptions = vertex_attributes;

		//图元装配（三角形列表）
		VkPipelineInputAssemblyStateCreateInfo input_assembly{};
		input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly.primitiveRestartEnable = VK_FALSE;

		//视口 / 裁剪（动态设置，故此处仅声明数量）
		VkPipelineViewportStateCreateInfo viewport_state{};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		//光栅化（填充模式、背面剔除、正面为逆时针）
		VkPipelineRasterizationStateCreateInfo rasterization{};
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.depthClampEnable = VK_FALSE;
		rasterization.rasterizerDiscardEnable = VK_FALSE;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterization.depthBiasEnable = VK_FALSE;
		rasterization.lineWidth = 1.0f;

		//多重采样（关闭）
		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample.sampleShadingEnable = VK_FALSE;

		//深度 / 模板（深度测试开、写开、LESS）
		VkPipelineDepthStencilStateCreateInfo depth_stencil{};
		depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil.depthTestEnable = VK_TRUE;
		depth_stencil.depthWriteEnable = VK_TRUE;
		depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
		depth_stencil.stencilTestEnable = VK_FALSE;

		//颜色混合（不混合，写全通道）
		VkPipelineColorBlendAttachmentState color_attachment{};
		color_attachment.blendEnable = VK_FALSE;
		color_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		//颜色混合状态
		VkPipelineColorBlendStateCreateInfo color_blend{};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.logicOpEnable = VK_FALSE;
		color_blend.attachmentCount = 1;
		color_blend.pAttachments = &color_attachment;

		//动态状态（视口与裁剪）
		VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic_state{};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = 2;
		dynamic_state.pDynamicStates = dynamic_states;

		//动态渲染信息（颜色 / 深度附件格式，经 pNext 链入）
		VkPipelineRenderingCreateInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachmentFormats = &config.color_format;
		rendering_info.depthAttachmentFormat = config.depth_format;

		//图形管线创建信息
		VkGraphicsPipelineCreateInfo pipeline_info{};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.pNext = &rendering_info;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = stages;
		pipeline_info.pVertexInputState = &vertex_input;
		pipeline_info.pInputAssemblyState = &input_assembly;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterization;
		pipeline_info.pMultisampleState = &multisample;
		pipeline_info.pDepthStencilState = &depth_stencil;
		pipeline_info.pColorBlendState = &color_blend;
		pipeline_info.pDynamicState = &dynamic_state;
		pipeline_info.layout = pipeline_layout;
		pipeline_info.renderPass = VK_NULL_HANDLE;

		//创建图形管线
		if (!vk_result_check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline), "vkCreateGraphicsPipelines"))
		{
			pipeline = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}
}
