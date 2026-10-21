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
	避免两侧各自漂移而静默错配（位置 vec3 在 0，纹理坐标 vec2 在 12，法线 vec3 在 20，步长 32）。
	*/
	static_assert(sizeof(Vertex) == 32, "Mesh::Vertex 步长须为 32 字节（vec3 位置 + vec2 纹理坐标 + vec3 法线）");
	static_assert(offsetof(Vertex, position) == 0, "Mesh::Vertex::position 偏移须为 0");
	static_assert(offsetof(Vertex, uv) == 12, "Mesh::Vertex::uv 偏移须为 12");
	static_assert(offsetof(Vertex, normal) == 20, "Mesh::Vertex::normal 偏移须为 20");

	/*
	创建图形管线
	动态渲染（pNext 链入 VkPipelineRenderingCreateInfo，颜色 / 深度附件格式来自配置）；
	顶点输入 binding 0（step 32，位置 vec3 @0 / 纹理坐标 vec2 @12 / 法线 vec3 @20）＋ binding 1（step 64，模型矩阵 4 列 vec4 @0/16/32/48，实例步进）；
	视口与裁剪动态设置；背面剔除 BACK、正面 COUNTER_CLOCKWISE（配合 cube_generate 绕序）；
	深度测试开 / 写开 / LESS；推送常量 mat4 模型矩阵。
	结果写入 pipeline_out（不改动任何条目）：本模块可持有多条管线，每条由调用方传入其着色器对与混合档。
	*/
	bool Pipeline_Context::graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode, VkPipeline& pipeline_out)
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

		//顶点绑定（binding 0：顶点属性，步长 = sizeof(Vertex) = 20）
		VkVertexInputBindingDescription vertex_bindings[2]{};
		vertex_bindings[0].binding = 0;
		vertex_bindings[0].stride = sizeof(Vertex);
		vertex_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		//实例绑定（binding 1：模型矩阵 4 列 vec4，步长 64，按实例步进）
		vertex_bindings[1].binding = 1;
		vertex_bindings[1].stride = static_cast<uint32_t>(sizeof(float) * 16);
		vertex_bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

		/*
		顶点属性（0：位置 vec3 @0；1：纹理坐标 vec2 @12；2：法线 vec3 @20）＋ 实例属性（3..6：模型矩阵 4 列 vec4）
		实例属性**恒定声明**：渲染层只有一条绘制路径（模型矩阵统一由实例缓冲承载，
		实例数为 1 即逐次绘制的等价路径），故不存在「管线要实例属性而未绑定实例缓冲」的合法状态。
		*/
		VkVertexInputAttributeDescription vertex_attributes[7]{};
		vertex_attributes[0].location = 0;
		vertex_attributes[0].binding = 0;
		vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attributes[0].offset = 0;
		vertex_attributes[1].location = 1;
		vertex_attributes[1].binding = 0;
		vertex_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertex_attributes[1].offset = 12;
		//顶点法线（第二十九轮新增；location 2，偏移 20，零向量表示未提供）
		vertex_attributes[2].location = 2;
		vertex_attributes[2].binding = 0;
		vertex_attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attributes[2].offset = 20;
		//模型矩阵 4 列（列主序：列 i 位于字节偏移 i × 16）
		for (uint32_t index = 0; index < 4; ++index)
		{
			vertex_attributes[3 + index].location = 3 + index;
			vertex_attributes[3 + index].binding = 1;
			vertex_attributes[3 + index].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			vertex_attributes[3 + index].offset = index * 16;
		}

		//顶点输入状态（两个绑定：0 = 顶点步进，1 = 实例步进；共 7 条属性）
		VkPipelineVertexInputStateCreateInfo vertex_input{};
		vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input.vertexBindingDescriptionCount = 2;
		vertex_input.pVertexBindingDescriptions = vertex_bindings;
		vertex_input.vertexAttributeDescriptionCount = 7;
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

		/*
		颜色混合（按混合档生成；写全通道）
		Opaque：关闭混合，直接写全通道（行为与本轮之前一致）；
		Alpha ：源 alpha 混合（src.rgb * src.a + dst.rgb * (1 - src.a)），颜色与 alpha 通道同源同参。
		sRGB 附件的混合在线性空间进行（写入时编码回 sRGB），故半透明结果值随附件传输函数不同。
		*/
		VkPipelineColorBlendAttachmentState color_attachment{};
		color_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		//若为源 alpha 混合档
		if (blend_mode == Blend_Mode::Alpha)
		{
			color_attachment.blendEnable = VK_TRUE;
			color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		//不透明档：关闭混合
		else
			color_attachment.blendEnable = VK_FALSE;

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
		if (!vk_result_check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_out), "vkCreateGraphicsPipelines"))
		{
			pipeline_out = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}
}
