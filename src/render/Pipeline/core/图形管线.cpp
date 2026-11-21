#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//顶点布局（Vertex 步长与属性偏移据此推导）
#include "src/render/Mesh/网格数据.h"
//offsetof

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
	//蒙皮顶点布局（收敛轮 859）：joint u16×4 @32、weight u16×4 @40，步长 48
	static_assert(sizeof(Skinned_Vertex) == 48, "Skinned_Vertex 步长须为 48 字节");
	static_assert(offsetof(Skinned_Vertex, joint) == 32, "Skinned_Vertex::joint 偏移须为 32");
	static_assert(offsetof(Skinned_Vertex, weight) == 40, "Skinned_Vertex::weight 偏移须为 40");

	/*
	创建图形管线
	动态渲染（pNext 链入 VkPipelineRenderingCreateInfo，颜色 / 深度附件格式来自配置）；
	顶点输入 binding 0（step 32，位置 vec3 @0 / 纹理坐标 vec2 @12 / 法线 vec3 @20）＋ binding 1（step 80，模型矩阵 4 列 vec4 @0/16/32/48 ＋ 逐物体材质 vec4 @64，实例步进）；
	视口与裁剪动态设置；背面剔除 BACK、正面 COUNTER_CLOCKWISE（配合 cube_generate 绕序）；
	深度测试开 / 写开 / LESS；推送常量 mat4 模型矩阵。
	结果写入 pipeline_out（不改动任何条目）：本模块可持有多条管线，每条由调用方传入其着色器对与混合档。
	*/
	bool Pipeline_Context::graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode, VkPipeline& pipeline_out,
		VkFormat color_format, bool cull_none, uint32_t sample_count, uint32_t input_locations, bool depth_write, bool depth_lequal,
		bool alpha_to_coverage, bool cull_front,
		bool depth_test, bool depth_format_override, VkFormat depth_format, VkFormat tone_id_format, bool skinned)
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

		/*
		顶点输入按「着色器实际声明的位置」裁剪（第九十七轮）
		input_locations 为 SPIR-V 解析所得位集（0 视为解析失败 → 全量 0xFF）；
		只在位集命中时声明对应属性，避免校验层「属性未被消费」告警。
		*/
		uint32_t declared_locations = input_locations == 0 ? 0xFF : input_locations;
		//属性集合（最多 13 条：0..10 静态顶点 / 11..12 蒙皮关节与权重）
		VkVertexInputAttributeDescription vertex_attributes[13]{};
		uint32_t attribute_count = 0;
		//是否需要顶点绑定 0 / 实例绑定 1
		bool use_vertex_binding = false;
		bool use_instance_binding = false;
		//声明单条属性（位集未命中则跳过）
		auto add_attribute = [&](uint32_t location, uint32_t binding, VkFormat format, uint32_t offset)
		{
			//位集未命中则跳过
			if ((declared_locations & (1u << location)) == 0)
				return;
			VkVertexInputAttributeDescription& attribute = vertex_attributes[attribute_count++];
			attribute.location = location;
			attribute.binding = binding;
			attribute.format = format;
			attribute.offset = offset;
			//记录绑定使用情况
			if (binding == 0)
				use_vertex_binding = true;
			else
				use_instance_binding = true;
		};
		//位置 vec3 @0 / 纹理坐标 vec2 @12 / 法线 vec3 @20
		add_attribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
		add_attribute(1, 0, VK_FORMAT_R32G32_SFLOAT, 12);
		add_attribute(2, 0, VK_FORMAT_R32G32B32_SFLOAT, 20);
		//模型矩阵 4 列（列主序：列 i 位于字节偏移 i × 16）
		for (uint32_t index = 0; index < 4; ++index)
			add_attribute(3 + index, 1, VK_FORMAT_R32G32B32A32_SFLOAT, index * 16);
		//逐物体材质（第六十八轮；location 7，偏移 64）
		add_attribute(7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, INSTANCE_MATERIAL_OFFSET * 4);
		//逐物体卡通参数（第四百零八轮；location 8，偏移 80）
		add_attribute(8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, INSTANCE_TOON_OFFSET * 4);
		//逐物体卡通暗部色调（第四百零九轮；location 9，偏移 96）
		add_attribute(9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, INSTANCE_TOON_TINT_OFFSET * 4);
		//逐物体 PBR 特性覆盖（第 777 轮；location 10，偏移 112）
		add_attribute(10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, INSTANCE_PBR_FEATURES_OFFSET * 4);
		//蒙皮关节与权重（收敛轮 859；location 11 / 12，顶点绑定 0）
		if (skinned)
		{
			add_attribute(11, 0, VK_FORMAT_R16G16B16A16_UINT, 32);
			add_attribute(12, 0, VK_FORMAT_R16G16B16A16_UNORM, 40);
		}

		//顶点绑定（binding 0：顶点属性，步长 = sizeof(Vertex)；binding 1：实例步长 80）
		VkVertexInputBindingDescription vertex_bindings[2]{};
		uint32_t binding_count = 0;
		//顶点步进绑定
		if (use_vertex_binding)
		{
			vertex_bindings[binding_count].binding = 0;
			//蒙皮管线按 48 字节步长，静态管线保持 32 字节
			vertex_bindings[binding_count].stride = skinned ? sizeof(Skinned_Vertex) : sizeof(Vertex);
			vertex_bindings[binding_count].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			++binding_count;
		}
		//实例步进绑定
		if (use_instance_binding)
		{
			vertex_bindings[binding_count].binding = 1;
			vertex_bindings[binding_count].stride = INSTANCE_STRIDE_BYTES;
			vertex_bindings[binding_count].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			++binding_count;
		}

		//顶点输入状态（仅声明实际使用的绑定与属性）
		VkPipelineVertexInputStateCreateInfo vertex_input{};
		vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input.vertexBindingDescriptionCount = binding_count;
		vertex_input.pVertexBindingDescriptions = vertex_bindings;
		vertex_input.vertexAttributeDescriptionCount = attribute_count;
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

		//光栅化（填充模式、背面剔除可关、正面为逆时针）
		VkPipelineRasterizationStateCreateInfo rasterization{};
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.depthClampEnable = VK_FALSE;
		rasterization.rasterizerDiscardEnable = VK_FALSE;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = cull_none ? VK_CULL_MODE_NONE
			: (cull_front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
		rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterization.depthBiasEnable = VK_FALSE;
		rasterization.lineWidth = 1.0f;

		/*
		多重采样（第九十六轮）
		采样数 = 条目覆盖值（非 0）或上下文配置值；须与动态渲染颜色 / 深度附件的采样数一致。
		*/
		uint32_t effective_samples = sample_count != 0 ? sample_count : config.sample_count;
		//采样数兜底为 1
		if (effective_samples == 0)
			effective_samples = 1;
		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(effective_samples);
		multisample.sampleShadingEnable = VK_FALSE;
		/*
		覆盖率写入（第一百二十六轮）
		仅多重采样管线启用；单采样时强制关闭，保持既有 alpha 混合路径逐字节不变。
		*/
		multisample.alphaToCoverageEnable = (alpha_to_coverage && effective_samples > 1) ? VK_TRUE : VK_FALSE;

		//深度 / 模板（深度测试由参数决定；无深度附件的管线必须关闭深度测试与写入）
		VkFormat attachment_depth_format = depth_format_override ? depth_format : config.depth_format;
		bool depth_active = depth_test && attachment_depth_format != VK_FORMAT_UNDEFINED;
		VkPipelineDepthStencilStateCreateInfo depth_stencil{};
		depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil.depthTestEnable = depth_active ? VK_TRUE : VK_FALSE;
		depth_stencil.depthWriteEnable = (depth_active && depth_write) ? VK_TRUE : VK_FALSE;
		depth_stencil.depthCompareOp = depth_lequal ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
		depth_stencil.depthBoundsTestEnable = VK_FALSE;
		depth_stencil.stencilTestEnable = VK_FALSE;

		/*
		颜色混合（按混合档生成；写全通道）
		Opaque  ：关闭混合，直接写全通道（行为与本轮之前一致）；
		Alpha   ：源 alpha 混合（src.rgb * src.a + dst.rgb * (1 - src.a)），颜色与 alpha 通道同源同参。
		Additive：加法混合（src.rgb + dst.rgb），alpha 通道同源同参（第二百二十七轮）。
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
		//若为加法混合档
		else if (blend_mode == Blend_Mode::Additive)
		{
			color_attachment.blendEnable = VK_TRUE;
			color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		//不透明档：关闭混合
		else
			color_attachment.blendEnable = VK_FALSE;

		//颜色混合状态（第二附件存在时按两项下发，第二项同混合口径）
		VkPipelineColorBlendAttachmentState color_attachments[2] = { color_attachment, color_attachment };
		const bool tone_id_active = tone_id_format != VK_FORMAT_UNDEFINED;
		VkPipelineColorBlendStateCreateInfo color_blend{};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.logicOpEnable = VK_FALSE;
		color_blend.attachmentCount = tone_id_active ? 2u : 1u;
		color_blend.pAttachments = color_attachments;

		//动态状态（视口与裁剪）
		VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic_state{};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = 2;
		dynamic_state.pDynamicStates = dynamic_states;

		//动态渲染信息（颜色 / 深度附件格式，经 pNext 链入；颜色格式可被条目覆盖，见 Pipeline_Entry::color_format）
		VkFormat attachment_color_format = color_format == VK_FORMAT_UNDEFINED ? config.color_format : color_format;
		VkFormat attachment_formats[2] = { attachment_color_format, tone_id_format };
		VkPipelineRenderingCreateInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_info.colorAttachmentCount = tone_id_active ? 2u : 1u;
		rendering_info.pColorAttachmentFormats = attachment_formats;
		rendering_info.depthAttachmentFormat = attachment_depth_format;

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
		if (!vk_result_check(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &pipeline_info, nullptr, &pipeline_out), "vkCreateGraphicsPipelines"))
		{
			pipeline_out = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}
}