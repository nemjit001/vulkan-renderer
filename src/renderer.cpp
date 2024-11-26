#include "renderer.hpp"

#include <cassert>
#include <stdexcept>

#include <imgui_impl_vulkan.h>
#include <volk.h>

#include "assets.hpp"
#include "mesh.hpp"
#include "scene.hpp"

IRenderer::IRenderer(RenderDeviceContext* pDeviceContext)
	:
	m_pDeviceContext(pDeviceContext)
{
	assert(m_pDeviceContext != nullptr);
}

ForwardRenderer::ForwardRenderer(RenderDeviceContext* pDeviceContext, uint32_t framebufferWidth, uint32_t framebufferHeight)
	:
	IRenderer(pDeviceContext),
	m_framebufferWidth(framebufferWidth),
	m_framebufferHeight(framebufferHeight)
{
	// Create command data
	if (!pDeviceContext->createFence(&m_frameCommandsFinished, true)
		|| !pDeviceContext->createCommandContext(CommandQueueType::Direct, m_frameCommands)) {
		throw std::runtime_error("Forward Renderer command data create failed\n");
	}

	// Create forward rendering members
	{
		// Create forward render pass
		{
			VkAttachmentDescription colorAttachment{};
			colorAttachment.flags = 0;
			colorAttachment.format = m_pDeviceContext->getSwapFormat();
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentDescription depthStencilAttachment{};
			depthStencilAttachment.flags = 0;
			depthStencilAttachment.format = VK_FORMAT_D32_SFLOAT;
			depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference opaqueForwardColorAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference opaqueForwardDepthAttachment = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
			VkSubpassDescription opaqueForwardPass{};
			opaqueForwardPass.flags = 0;
			opaqueForwardPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			opaqueForwardPass.inputAttachmentCount = 0;
			opaqueForwardPass.pInputAttachments = nullptr;
			opaqueForwardPass.colorAttachmentCount = 1;
			opaqueForwardPass.pColorAttachments = &opaqueForwardColorAttachment;
			opaqueForwardPass.pResolveAttachments = nullptr;
			opaqueForwardPass.pDepthStencilAttachment = &opaqueForwardDepthAttachment;
			opaqueForwardPass.preserveAttachmentCount = 0;
			opaqueForwardPass.pPreserveAttachments = nullptr;

			VkSubpassDependency prevFrameDependency{};
			prevFrameDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			prevFrameDependency.dstSubpass = 0;
			prevFrameDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			prevFrameDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			prevFrameDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			prevFrameDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[] = { colorAttachment, depthStencilAttachment, };
			VkSubpassDescription subpasses[] = { opaqueForwardPass, };
			VkSubpassDependency dependencies[] = { prevFrameDependency };

			VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
			renderPassCreateInfo.flags = 0;
			renderPassCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
			renderPassCreateInfo.pAttachments = attachments;
			renderPassCreateInfo.subpassCount = SIZEOF_ARRAY(subpasses);
			renderPassCreateInfo.pSubpasses = subpasses;
			renderPassCreateInfo.dependencyCount = SIZEOF_ARRAY(dependencies);
			renderPassCreateInfo.pDependencies = dependencies;

			if (VK_FAILED(vkCreateRenderPass(m_pDeviceContext->device, &renderPassCreateInfo, nullptr, &m_forwardRenderPass))) {
				throw std::runtime_error("Forward Renderer forward render pass create failed\n");
			}
		}

		// Create depth stencil target & forward framebuffers
		{
			if (!m_pDeviceContext->createTexture(
				m_depthStencilTexture,
				VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_framebufferWidth, m_framebufferHeight, 1)
				|| !m_depthStencilTexture.initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT))
			{
				throw std::runtime_error("Forward Renderer depth stencil texture create failed\n");
			}

			auto backbuffers = m_pDeviceContext->getBackbuffers();
			for (auto& backbuffer : backbuffers)
			{
				VkImageView attachments[] = { backbuffer.view, m_depthStencilTexture.view, };

				VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
				framebufferCreateInfo.flags = 0;
				framebufferCreateInfo.renderPass = m_forwardRenderPass;
				framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
				framebufferCreateInfo.pAttachments = attachments;
				framebufferCreateInfo.width = m_framebufferWidth;
				framebufferCreateInfo.height = m_framebufferHeight;
				framebufferCreateInfo.layers = 1;

				VkFramebuffer framebuffer = VK_NULL_HANDLE;
				vkCreateFramebuffer(m_pDeviceContext->device, &framebufferCreateInfo, nullptr, &framebuffer);
				assert(framebuffer != VK_NULL_HANDLE);
				m_forwardFramebuffers.push_back(framebuffer);
			}
		}

		// Create forward pipeline immutable samplers
		{
			VkSamplerCreateInfo shadowmapSamplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			shadowmapSamplerCreateInfo.flags = 0;
			shadowmapSamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			shadowmapSamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			shadowmapSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			shadowmapSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			shadowmapSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			shadowmapSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			shadowmapSamplerCreateInfo.mipLodBias = 0.0F;
			shadowmapSamplerCreateInfo.anisotropyEnable = VK_FALSE;
			shadowmapSamplerCreateInfo.maxAnisotropy = 0.0F;
			shadowmapSamplerCreateInfo.compareEnable = VK_FALSE;
			shadowmapSamplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
			shadowmapSamplerCreateInfo.minLod = 0.0F;
			shadowmapSamplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
			shadowmapSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			shadowmapSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

			VkSamplerCreateInfo textureSamplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			textureSamplerCreateInfo.flags = 0;
			textureSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
			textureSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
			textureSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			textureSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			textureSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			textureSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			textureSamplerCreateInfo.mipLodBias = 0.0F;
			textureSamplerCreateInfo.anisotropyEnable = VK_TRUE;
			textureSamplerCreateInfo.maxAnisotropy = 16.0F;
			textureSamplerCreateInfo.compareEnable = VK_FALSE;
			textureSamplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
			textureSamplerCreateInfo.minLod = 0.0F;
			textureSamplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
			textureSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			textureSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

			if (VK_FAILED(vkCreateSampler(m_pDeviceContext->device, &shadowmapSamplerCreateInfo, nullptr, &m_shadowmapSampler))
				|| VK_FAILED(vkCreateSampler(m_pDeviceContext->device, &textureSamplerCreateInfo, nullptr, &m_textureSampler)))
			{
				throw std::runtime_error("Forward Renderer immutable sampler create failed\n");
			}
		}

		// Create forward pipeline descriptor set layouts
		{
			VkDescriptorSetLayoutBinding cameraDataBinding{};
			cameraDataBinding.binding = 0;
			cameraDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			cameraDataBinding.descriptorCount = 1;
			cameraDataBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			cameraDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutBinding textureArrayBinding{};
			textureArrayBinding.binding = 1;
			textureArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureArrayBinding.descriptorCount = Scene::MaxTextures;
			textureArrayBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			textureArrayBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutBinding sceneDataBindings[] = { cameraDataBinding, textureArrayBinding, };
			VkDescriptorSetLayoutCreateInfo sceneDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			sceneDataSetLayout.flags = 0;
			sceneDataSetLayout.bindingCount = SIZEOF_ARRAY(sceneDataBindings);
			sceneDataSetLayout.pBindings = sceneDataBindings;

			VkDescriptorSetLayoutBinding materialDataBinding{};
			materialDataBinding.binding = 0;
			materialDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			materialDataBinding.descriptorCount = 1;
			materialDataBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			materialDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutBinding materialDataBindings[] = { materialDataBinding, };
			VkDescriptorSetLayoutCreateInfo materialDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			materialDataSetLayout.flags = 0;
			materialDataSetLayout.bindingCount = SIZEOF_ARRAY(materialDataBindings);
			materialDataSetLayout.pBindings = materialDataBindings;

			VkDescriptorSetLayoutBinding objectDataBinding{};
			objectDataBinding.binding = 0;
			objectDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			objectDataBinding.descriptorCount = 1;
			objectDataBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			objectDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutBinding objectDataBindings[] = { objectDataBinding, };
			VkDescriptorSetLayoutCreateInfo objectDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			objectDataSetLayout.flags = 0;
			objectDataSetLayout.bindingCount = SIZEOF_ARRAY(objectDataBindings);
			objectDataSetLayout.pBindings = objectDataBindings;

			if (VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &sceneDataSetLayout, nullptr, &m_sceneDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &materialDataSetLayout, nullptr, &m_materialDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &objectDataSetLayout, nullptr, &m_objectDataSetLayout))) {
				throw std::runtime_error("Forward Renderer forward descriptor set layout create failed\n");
			}
		}

		// Create forward pipeline layout
		{
			VkDescriptorSetLayout setLayouts[] = { m_sceneDataSetLayout, m_materialDataSetLayout, m_objectDataSetLayout, };
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			pipelineLayoutCreateInfo.flags = 0;
			pipelineLayoutCreateInfo.setLayoutCount = SIZEOF_ARRAY(setLayouts);
			pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
			pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
			pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

			if (VK_FAILED(vkCreatePipelineLayout(m_pDeviceContext->device, &pipelineLayoutCreateInfo, nullptr, &m_forwardPipelineLayout))) {
				throw std::runtime_error("Forward Renderer forward pipeline layout create failed\n");
			}
		}

		// Create forward graphics pipeline
		{
			std::vector<uint32_t> forwardVertCode{};
			std::vector<uint32_t> forwardFragCode{};
			if (!readShaderFile("forward.vert.spv", forwardVertCode)
				|| !readShaderFile("forward.frag.spv", forwardFragCode)) {
				throw std::runtime_error("Forward Renderer shader file read failed (forward opaque)\n");
			}

			VkShaderModuleCreateInfo forwardVertCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			forwardVertCreateInfo.flags = 0;
			forwardVertCreateInfo.codeSize = static_cast<uint32_t>(forwardVertCode.size() * 4);
			forwardVertCreateInfo.pCode = forwardVertCode.data();

			VkShaderModuleCreateInfo forwardFragCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			forwardFragCreateInfo.flags = 0;
			forwardFragCreateInfo.codeSize = static_cast<uint32_t>(forwardFragCode.size() * 4);
			forwardFragCreateInfo.pCode = forwardFragCode.data();

			VkShaderModule forwardVertModule = VK_NULL_HANDLE;
			VkShaderModule forwardFragModule = VK_NULL_HANDLE;
			if (VK_FAILED(vkCreateShaderModule(m_pDeviceContext->device, &forwardVertCreateInfo, nullptr, &forwardVertModule))
				|| VK_FAILED(vkCreateShaderModule(m_pDeviceContext->device, &forwardFragCreateInfo, nullptr, &forwardFragModule))) {
				throw std::runtime_error("Forward Renderer shader module create failed (forward opaque)\n");
			}

			VkPipelineShaderStageCreateInfo forwardVertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			forwardVertStage.flags = 0;
			forwardVertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			forwardVertStage.module = forwardVertModule;
			forwardVertStage.pName = "main";
			forwardVertStage.pSpecializationInfo = nullptr;

			VkPipelineShaderStageCreateInfo forwardFragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			forwardFragStage.flags = 0;
			forwardFragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			forwardFragStage.module = forwardFragModule;
			forwardFragStage.pName = "main";
			forwardFragStage.pSpecializationInfo = nullptr;

			VkPipelineShaderStageCreateInfo forwardOpaqueStages[] = { forwardVertStage, forwardFragStage, };

			VkVertexInputBindingDescription bindingDescriptions[] = {
				{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX, }
			};

			VkVertexInputAttributeDescription attributeDescriptions[] = {
				{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), },
				{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), },
				{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), },
				{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent), },
				{ 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord), },
			};

			VkPipelineVertexInputStateCreateInfo vertexInputState{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
			vertexInputState.flags = 0;
			vertexInputState.vertexBindingDescriptionCount = SIZEOF_ARRAY(bindingDescriptions);
			vertexInputState.pVertexBindingDescriptions = bindingDescriptions;
			vertexInputState.vertexAttributeDescriptionCount = SIZEOF_ARRAY(attributeDescriptions);
			vertexInputState.pVertexAttributeDescriptions = attributeDescriptions;

			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			inputAssemblyState.flags = 0;
			inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;

			float viewportWidth = static_cast<float>(m_framebufferWidth);
			float viewportHeight = static_cast<float>(m_framebufferHeight);
			VkViewport viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -viewportHeight, 0.0F, 1.0F };
			VkRect2D scissor = VkRect2D{ { 0, 0 }, { m_framebufferWidth, m_framebufferHeight } };

			VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			viewportState.flags = 0;
			viewportState.viewportCount = 1;
			viewportState.pViewports = &viewport;
			viewportState.scissorCount = 1;
			viewportState.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo rasterizationState{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
			rasterizationState.flags = 0;
			rasterizationState.depthClampEnable = VK_FALSE;
			rasterizationState.rasterizerDiscardEnable = VK_FALSE;
			rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizationState.depthBiasEnable = VK_FALSE;
			rasterizationState.depthBiasConstantFactor = 0.0F;
			rasterizationState.depthBiasClamp = 0.0F;
			rasterizationState.depthBiasSlopeFactor = 0.0F;
			rasterizationState.lineWidth = 1.0F;

			VkPipelineMultisampleStateCreateInfo multisampleState{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			multisampleState.flags = 0;
			multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			multisampleState.sampleShadingEnable = VK_FALSE;
			multisampleState.minSampleShading = 0.0F;
			multisampleState.pSampleMask = nullptr;
			multisampleState.alphaToCoverageEnable = VK_FALSE;
			multisampleState.alphaToOneEnable = VK_FALSE;

			VkPipelineDepthStencilStateCreateInfo depthStencilState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
			depthStencilState.flags = 0;
			depthStencilState.depthTestEnable = VK_TRUE;
			depthStencilState.depthWriteEnable = VK_TRUE;
			depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencilState.depthBoundsTestEnable = VK_TRUE;
			depthStencilState.stencilTestEnable = VK_FALSE;
			depthStencilState.front = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, UINT32_MAX, UINT32_MAX, UINT32_MAX };
			depthStencilState.back = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, UINT32_MAX, UINT32_MAX, UINT32_MAX };
			depthStencilState.minDepthBounds = 0.0F;
			depthStencilState.maxDepthBounds = 1.0F;

			VkPipelineColorBlendAttachmentState colorBlendTarget{};
			colorBlendTarget.blendEnable = VK_FALSE;
			colorBlendTarget.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendTarget.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendTarget.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendTarget.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendTarget.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendTarget.alphaBlendOp = VK_BLEND_OP_ADD;
			colorBlendTarget.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
				| VK_COLOR_COMPONENT_G_BIT
				| VK_COLOR_COMPONENT_B_BIT
				| VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendAttachmentState colorBlendAttachments[] = { colorBlendTarget, };
			VkPipelineColorBlendStateCreateInfo colorBlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
			colorBlendState.flags = 0;
			colorBlendState.logicOpEnable = VK_FALSE;
			colorBlendState.logicOp = VK_LOGIC_OP_CLEAR;
			colorBlendState.attachmentCount = SIZEOF_ARRAY(colorBlendAttachments);
			colorBlendState.pAttachments = colorBlendAttachments;
			colorBlendState.blendConstants[0] = 0.0F;
			colorBlendState.blendConstants[1] = 0.0F;
			colorBlendState.blendConstants[2] = 0.0F;
			colorBlendState.blendConstants[3] = 0.0F;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, };
			VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamicState.flags = 0;
			dynamicState.dynamicStateCount = SIZEOF_ARRAY(dynamicStates);
			dynamicState.pDynamicStates = dynamicStates;

			VkGraphicsPipelineCreateInfo forwardOpaqueCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			forwardOpaqueCreateInfo.flags = 0;
			forwardOpaqueCreateInfo.stageCount = SIZEOF_ARRAY(forwardOpaqueStages);
			forwardOpaqueCreateInfo.pStages = forwardOpaqueStages;
			forwardOpaqueCreateInfo.pVertexInputState = &vertexInputState;
			forwardOpaqueCreateInfo.pInputAssemblyState = &inputAssemblyState;
			forwardOpaqueCreateInfo.pTessellationState = nullptr;
			forwardOpaqueCreateInfo.pViewportState = &viewportState;
			forwardOpaqueCreateInfo.pRasterizationState = &rasterizationState;
			forwardOpaqueCreateInfo.pMultisampleState = &multisampleState;
			forwardOpaqueCreateInfo.pDepthStencilState = &depthStencilState;
			forwardOpaqueCreateInfo.pColorBlendState = &colorBlendState;
			forwardOpaqueCreateInfo.pDynamicState = &dynamicState;
			forwardOpaqueCreateInfo.layout = m_forwardPipelineLayout;
			forwardOpaqueCreateInfo.renderPass = m_forwardRenderPass;
			forwardOpaqueCreateInfo.subpass = 0;
			forwardOpaqueCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			forwardOpaqueCreateInfo.basePipelineIndex = 0;

			if (VK_FAILED(vkCreateGraphicsPipelines(m_pDeviceContext->device, VK_NULL_HANDLE, 1, &forwardOpaqueCreateInfo, nullptr, &m_forwardOpaquePipeline))) {
				throw std::runtime_error("Forward Renderer forward pipeline create failed\n");
			}

			vkDestroyShaderModule(m_pDeviceContext->device, forwardFragModule, nullptr);
			vkDestroyShaderModule(m_pDeviceContext->device, forwardVertModule, nullptr);
		}
	}

	// Create uniform buffers
	{
		size_t sceneDataBufferSize = sizeof(UniformCameraData);
		size_t materialBufferSize = sizeof(UniformMaterialData);
		size_t objectBufferSize = sizeof(UniformObjectData);

		if (!m_pDeviceContext->createBuffer(m_sceneDataBuffer, sceneDataBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			|| !m_pDeviceContext->createBuffer(m_materialDataBuffer, materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			|| !m_pDeviceContext->createBuffer(m_objectDataBuffer, objectBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			throw std::runtime_error("Forward Renderer uniform buffer create failed");
		}
	}

	// Create GUI descriptor pool & init ImGui
	{
		VkDescriptorPoolSize guiPoolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		};

		VkDescriptorPoolCreateInfo guiPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		guiPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		guiPoolCreateInfo.maxSets = 1;
		guiPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(guiPoolSizes);
		guiPoolCreateInfo.pPoolSizes = guiPoolSizes;

		if (VK_FAILED(vkCreateDescriptorPool(m_pDeviceContext->device, &guiPoolCreateInfo, nullptr, &m_guiDescriptorPool))) {
			throw std::runtime_error("Forward Renderer ImGUI descriptor pool create failed");
		}

		ImGui_ImplVulkan_InitInfo initInfo{};
		initInfo.Instance = RenderBackend::getInstance();
		initInfo.PhysicalDevice = m_pDeviceContext->getAdapter();
		initInfo.Device = m_pDeviceContext->device;
		initInfo.QueueFamily = m_pDeviceContext->getQueueFamily(CommandQueueType::Direct);
		initInfo.Queue = m_pDeviceContext->directQueue;
		initInfo.DescriptorPool = m_guiDescriptorPool;
		initInfo.RenderPass = m_forwardRenderPass;
		initInfo.MinImageCount = m_pDeviceContext->backbufferCount();
		initInfo.ImageCount = m_pDeviceContext->backbufferCount();
		initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.PipelineCache = VK_NULL_HANDLE;
		initInfo.Subpass = 0;

		auto loaderFunc = [](char const* pName, void* pUserData) {
			(void)(pUserData);
			return vkGetInstanceProcAddr(RenderBackend::getInstance(), pName);
		};

		if (!ImGui_ImplVulkan_LoadFunctions(loaderFunc)
			|| !ImGui_ImplVulkan_Init(&initInfo)) {
			throw std::runtime_error("Forward Renderer ImGUI init failed");
		}
	}
}

ForwardRenderer::~ForwardRenderer()
{
	ImGui_ImplVulkan_Shutdown();

	m_objectSets.clear();
	m_materialSets.clear();
	m_sceneSet = VK_NULL_HANDLE;
	vkDestroyDescriptorPool(m_pDeviceContext->device, m_descriptorPool, nullptr);

	vkDestroyDescriptorPool(m_pDeviceContext->device, m_guiDescriptorPool, nullptr);

	m_objectDataBuffer.destroy();
	m_materialDataBuffer.destroy();
	m_sceneDataBuffer.destroy();

	// Destroy forward rendering pipeline
	vkDestroyPipeline(m_pDeviceContext->device, m_forwardOpaquePipeline, nullptr);
	vkDestroyPipelineLayout(m_pDeviceContext->device, m_forwardPipelineLayout, nullptr);

	// Destroy layouts
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_objectDataSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_materialDataSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_sceneDataSetLayout, nullptr);

	// Destroy samplers
	vkDestroySampler(m_pDeviceContext->device, m_textureSampler, nullptr);
	vkDestroySampler(m_pDeviceContext->device, m_shadowmapSampler, nullptr);

	// Destroy forward pass data
	for (auto& framebuffer : m_forwardFramebuffers) {
		vkDestroyFramebuffer(m_pDeviceContext->device, framebuffer, nullptr);
	}
	m_depthStencilTexture.destroy();
	vkDestroyRenderPass(m_pDeviceContext->device, m_forwardRenderPass, nullptr);

	// Destroy command data
	m_pDeviceContext->destroyCommandContext(m_frameCommands);
	m_pDeviceContext->destroyFence(m_frameCommandsFinished);
}

void ForwardRenderer::awaitFrame()
{
	vkWaitForFences(m_pDeviceContext->device, 1, &m_frameCommandsFinished, VK_TRUE, UINT64_MAX);
}

bool ForwardRenderer::onResize(uint32_t width, uint32_t height)
{
	vkWaitForFences(m_pDeviceContext->device, 1, &m_frameCommandsFinished, VK_TRUE, UINT64_MAX);
	m_framebufferWidth = width;
	m_framebufferHeight = height;

	// Release swap dependent resources
	for (auto& framebuffer : m_forwardFramebuffers) {
		vkDestroyFramebuffer(m_pDeviceContext->device, framebuffer, nullptr);
	}
	m_forwardFramebuffers.clear();
	m_depthStencilTexture.destroy();

	// Create swap dependent resources
	if (!m_pDeviceContext->createTexture(
			m_depthStencilTexture,
			VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_framebufferWidth, m_framebufferHeight, 1)
		|| !m_depthStencilTexture.initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT))
	{
		return false;
	}

	auto backbuffers = m_pDeviceContext->getBackbuffers();
	for (auto& backbuffer : backbuffers)
	{
		VkImageView attachments[] = { backbuffer.view, m_depthStencilTexture.view, };

		VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		framebufferCreateInfo.flags = 0;
		framebufferCreateInfo.renderPass = m_forwardRenderPass;
		framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
		framebufferCreateInfo.pAttachments = attachments;
		framebufferCreateInfo.width = m_framebufferWidth;
		framebufferCreateInfo.height = m_framebufferHeight;
		framebufferCreateInfo.layers = 1;

		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		vkCreateFramebuffer(m_pDeviceContext->device, &framebufferCreateInfo, nullptr, &framebuffer);
		assert(framebuffer != VK_NULL_HANDLE);
		m_forwardFramebuffers.push_back(framebuffer);
	}

	return true;
}

void ForwardRenderer::update(Scene const& scene)
{
	// Check uniform buffer sizes & recreate buffers if needed
	size_t const materialBufferSize = scene.materials.size() * sizeof(UniformMaterialData);
	size_t const objectBufferSize = scene.nodes.count * sizeof(UniformObjectData);
	
	if (materialBufferSize > m_materialDataBuffer.size)
	{
		m_materialDataBuffer.destroy();
		if (!m_pDeviceContext->createBuffer(m_materialDataBuffer, materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			throw std::runtime_error("Forward Renderer update uniform material buffer resize failed\n");
		}

		printf("Forward Renderer material uniform resized: %zu bytes\n", m_materialDataBuffer.size);
	}

	if (objectBufferSize > m_objectDataBuffer.size)
	{
		m_objectDataBuffer.destroy();
		if (!m_pDeviceContext->createBuffer(m_objectDataBuffer, objectBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			throw std::runtime_error("Forward Renderer update uniform object buffer resize failed\n");
		}

		printf("Forward Renderer object uniform resized: %zu bytes\n", m_objectDataBuffer.size);
	}

	// Upload uniform camera data
	Transform const& camTransform = scene.nodes.transform[scene.activeCamera];
	glm::mat4 const& camTransformMat = camTransform.matrix();
	glm::vec3 const camForward = glm::vec3(camTransformMat * glm::vec4(FORWARD, 0.0));
	glm::vec3 const camUp = glm::vec3(camTransformMat * glm::vec4(UP, 0.0));
	glm::mat4 const camView = glm::lookAt(camTransform.position, camTransform.position + camForward, camUp);

	UniformCameraData const cameraData{
		camTransform.position,
		scene.cameras[scene.nodes.cameraRef[scene.activeCamera]].matrix()* camView,
	};

	m_sceneDataBuffer.map();
	memcpy(m_sceneDataBuffer.pData, &cameraData, sizeof(UniformCameraData));
	m_sceneDataBuffer.unmap();

	// Upload uniform material data if it exists
	if (scene.materials.size() > 0)
	{
		std::vector<UniformMaterialData> materialData{};
		materialData.reserve(scene.materials.size());
		for (size_t i = 0; i < scene.materials.size(); i++)
		{
			Material const& mat = scene.materials[i];
			UniformMaterialData const uniformData{
				mat.defaultAlbedo,
				mat.defaultSpecular,
				mat.albedoTexture,
				mat.specularTexture,
				mat.normalTexture
			};

			materialData.emplace_back(uniformData);
		}

		m_materialDataBuffer.map();
		memcpy(m_materialDataBuffer.pData, materialData.data(), m_materialDataBuffer.size);
		m_materialDataBuffer.unmap();
	}

	// Upload uniform object data if it exists
	if (scene.nodes.count > 0)
	{
		std::vector<UniformObjectData> objectData{};
		objectData.reserve(scene.nodes.count);
		for (size_t i = 0; i < scene.nodes.count; i++)
		{
			glm::mat4 const model = scene.nodes.transform[i].matrix();
			glm::mat4 const normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(model))));
			UniformObjectData const uniformData{
				model,
				normal
			};

			objectData.emplace_back(uniformData);
		}

		m_objectDataBuffer.map();
		memcpy(m_objectDataBuffer.pData, objectData.data(), m_objectDataBuffer.size);
		m_objectDataBuffer.unmap();
	}

	// Size descriptor set arrays for this frame & recreate descriptor pool
	m_materialSets.resize(scene.materials.size());
	m_objectSets.resize(scene.nodes.count);

	uint32_t requiredDescriptorSets = static_cast<uint32_t>(1 + m_materialSets.size() + m_objectSets.size());
	if (requiredDescriptorSets > m_maxDescriptorSets) {
		m_maxDescriptorSets = requiredDescriptorSets;

		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * m_maxDescriptorSets },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * m_maxDescriptorSets },
		};

		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		descriptorPoolCreateInfo.flags = 0;
		descriptorPoolCreateInfo.maxSets = m_maxDescriptorSets;
		descriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(poolSizes);
		descriptorPoolCreateInfo.pPoolSizes = poolSizes;

		vkDestroyDescriptorPool(m_pDeviceContext->device, m_descriptorPool, nullptr);
		if (VK_FAILED(vkCreateDescriptorPool(m_pDeviceContext->device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool))) {
			throw std::runtime_error("Forward Renderer update descriptor pool realloc failed");
		}

		printf("Forward Renderer update reallocated descriptor pool (%u sets)\n", m_maxDescriptorSets);
	}

	// Reallocate descriptor sets
	vkResetDescriptorPool(m_pDeviceContext->device, m_descriptorPool, /* no flags */ 0);
	std::vector<VkDescriptorSetLayout> const materialSetLayouts(m_materialSets.size(), m_materialDataSetLayout);
	std::vector<VkDescriptorSetLayout> const objectSetLayouts(m_objectSets.size(), m_objectDataSetLayout);

	VkDescriptorSetAllocateInfo sceneSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	sceneSetAllocInfo.descriptorPool = m_descriptorPool;
	sceneSetAllocInfo.descriptorSetCount = 1;
	sceneSetAllocInfo.pSetLayouts = &m_sceneDataSetLayout;

	if (VK_FAILED(vkAllocateDescriptorSets(m_pDeviceContext->device, &sceneSetAllocInfo, &m_sceneSet))) {
		throw std::runtime_error("Forward Renderer update scene data set alloc failed\n");
	}

	if (m_materialSets.size() > 0)
	{
		VkDescriptorSetAllocateInfo materialSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		materialSetAllocInfo.descriptorPool = m_descriptorPool;
		materialSetAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_materialSets.size());
		materialSetAllocInfo.pSetLayouts = materialSetLayouts.data();

		if (VK_FAILED(vkAllocateDescriptorSets(m_pDeviceContext->device, &materialSetAllocInfo, m_materialSets.data()))) {
			throw std::runtime_error("Forward Renderer update material descriptor set alloc failed");
		}
	}

	if (m_objectSets.size() > 0)
	{
		VkDescriptorSetAllocateInfo objectSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		objectSetAllocInfo.descriptorPool = m_descriptorPool;
		objectSetAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_objectSets.size());
		objectSetAllocInfo.pSetLayouts = objectSetLayouts.data();

		if (VK_FAILED(vkAllocateDescriptorSets(m_pDeviceContext->device, &objectSetAllocInfo, m_objectSets.data()))) {
			throw std::runtime_error("Forward Renderer update object descriptor set alloc failed");
		}
	}

	// Update all descriptor sets in frame (scene data, texture array, material data, object data)
	std::vector<VkDescriptorImageInfo> imageInfos{};
	std::vector<VkDescriptorBufferInfo> bufferInfos{};
	std::vector<VkWriteDescriptorSet> descriptorWrites{};
	imageInfos.reserve(scene.textures.size());
	bufferInfos.reserve(2 + scene.materials.size() + scene.nodes.count);
	descriptorWrites.reserve(2 + scene.materials.size() + scene.nodes.count);

	VkDescriptorBufferInfo cameraDataBufferInfo{};
	cameraDataBufferInfo.buffer = m_sceneDataBuffer.handle;
	cameraDataBufferInfo.offset = 0;
	cameraDataBufferInfo.range = sizeof(UniformCameraData);
	bufferInfos.emplace_back(cameraDataBufferInfo);

	VkWriteDescriptorSet cameraDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	cameraDataWrite.dstSet = m_sceneSet;
	cameraDataWrite.dstBinding = 0;
	cameraDataWrite.dstArrayElement = 0;
	cameraDataWrite.descriptorCount = 1;
	cameraDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraDataWrite.pBufferInfo = &bufferInfos.back();
	descriptorWrites.emplace_back(cameraDataWrite);

	for (size_t textureIdx = 0; textureIdx < scene.textures.size(); textureIdx++)
	{
		VkDescriptorImageInfo textureImageInfo{};
		textureImageInfo.sampler = m_textureSampler;
		textureImageInfo.imageView = scene.textures[textureIdx].view;
		textureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos.emplace_back(textureImageInfo);
		
		VkWriteDescriptorSet textureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		textureWrite.dstSet = m_sceneSet;
		textureWrite.dstBinding = 1;
		textureWrite.dstArrayElement = static_cast<uint32_t>(textureIdx);
		textureWrite.descriptorCount = 1;
		textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureWrite.pImageInfo = &imageInfos.back();
		descriptorWrites.emplace_back(textureWrite);
	}

	for (size_t materialIdx = 0; materialIdx < scene.materials.size(); materialIdx++)
	{
		VkDescriptorBufferInfo materialDataBufferInfo{};
		materialDataBufferInfo.buffer = m_materialDataBuffer.handle;
		materialDataBufferInfo.offset = materialIdx * sizeof(UniformMaterialData);
		materialDataBufferInfo.range = sizeof(UniformMaterialData);
		bufferInfos.emplace_back(materialDataBufferInfo);

		VkWriteDescriptorSet materialDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		materialDataWrite.dstSet = m_materialSets[materialIdx];
		materialDataWrite.dstBinding = 0;
		materialDataWrite.dstArrayElement = 0;
		materialDataWrite.descriptorCount = 1;
		materialDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		materialDataWrite.pBufferInfo = &bufferInfos.back();
		descriptorWrites.emplace_back(materialDataWrite);
	}

	for (size_t nodeIdx = 0; nodeIdx < scene.nodes.count; nodeIdx++)
	{
		VkDescriptorBufferInfo objectDataBufferInfo{};
		objectDataBufferInfo.buffer = m_objectDataBuffer.handle;
		objectDataBufferInfo.offset = nodeIdx * sizeof(UniformObjectData);
		objectDataBufferInfo.range = sizeof(UniformObjectData);
		bufferInfos.emplace_back(objectDataBufferInfo);

		VkWriteDescriptorSet objectDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		objectDataWrite.dstSet = m_objectSets[nodeIdx];
		objectDataWrite.dstBinding = 0;
		objectDataWrite.dstArrayElement = 0;
		objectDataWrite.descriptorCount = 1;
		objectDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		objectDataWrite.pBufferInfo = &bufferInfos.back();
		descriptorWrites.emplace_back(objectDataWrite);
	}
	vkUpdateDescriptorSets(m_pDeviceContext->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

	// Sort object draws by material to reduce state changes in draw
	m_drawData.clear();
	for (uint32_t nodeIdx = 0; nodeIdx < scene.nodes.count; nodeIdx++)
	{
		SceneRef const meshRef = scene.nodes.meshRef[nodeIdx];
		SceneRef const materialRef = scene.nodes.materialRef[nodeIdx];
		if (meshRef == RefUnused || materialRef == RefUnused) {
			continue;
		}

		m_drawData[materialRef].emplace_back(nodeIdx);
	}
}

void ForwardRenderer::render(Scene const& scene)
{
	VkCommandBufferBeginInfo frameBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	frameBeginInfo.flags = 0;
	frameBeginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(m_frameCommands.handle, &frameBeginInfo);

	// Render forward pass
	{
		uint32_t const backbufferIndex = m_pDeviceContext->getCurrentBackbufferIndex();

		VkClearValue clearValues[] = {
			VkClearValue{{ 0.1F, 0.1F, 0.1F, 1.0F }},
			VkClearValue{{ 1.0F, 0x00 }},
		};

		VkRenderPassBeginInfo forwardPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		forwardPassBeginInfo.renderPass = m_forwardRenderPass;
		forwardPassBeginInfo.framebuffer = m_forwardFramebuffers[backbufferIndex];
		forwardPassBeginInfo.renderArea = VkRect2D{ { 0, 0 }, { m_framebufferWidth, m_framebufferHeight } };
		forwardPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
		forwardPassBeginInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(m_frameCommands.handle, &forwardPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		
		// Set viewport & scissor
		float viewportWidth = static_cast<float>(m_framebufferWidth);
		float viewportHeight = static_cast<float>(m_framebufferHeight);
		VkViewport viewport = { 0.0F, viewportHeight, viewportWidth, -viewportHeight, 0.0F, 1.0F };
		VkRect2D scissor = { { 0, 0 }, { m_framebufferWidth, m_framebufferHeight } };
		vkCmdSetViewport(m_frameCommands.handle, 0, 1, &viewport);
		vkCmdSetScissor(m_frameCommands.handle, 0, 1, &scissor);

		// Render forward opaque objects
		vkCmdBindPipeline(m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardOpaquePipeline);
		vkCmdBindDescriptorSets( // Bind scene data set
			m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
			0, 1, &m_sceneSet,
			0, nullptr
		);

		for (auto const& kvp : m_drawData)
		{
			uint32_t materialIdx = kvp.first;
			vkCmdBindDescriptorSets( // Bind material data set
				m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
				1, 1, &m_materialSets[materialIdx],
				0, nullptr
			);

			for (uint32_t nodeIdx : kvp.second)
			{
				vkCmdBindDescriptorSets( // Bind object data set
					m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
					2, 1, &m_objectSets[nodeIdx],
					0, nullptr
				);

				SceneRef const meshRef = scene.nodes.meshRef[nodeIdx];
				assert(meshRef != RefUnused);

				Mesh const& mesh = scene.meshes[meshRef];
				VkDeviceSize const offsets[] = { 0, };
				vkCmdBindVertexBuffers(m_frameCommands.handle, 0, 1, &mesh.vertexBuffer.handle, offsets);
				vkCmdBindIndexBuffer(m_frameCommands.handle, mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(m_frameCommands.handle, mesh.indexCount, 1, 0, 0, 0);
			}
		}

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_frameCommands.handle);
		vkCmdEndRenderPass(m_frameCommands.handle);
	}

	vkEndCommandBuffer(m_frameCommands.handle);

	VkSubmitInfo frameSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	frameSubmit.waitSemaphoreCount = 0;
	frameSubmit.pWaitSemaphores = nullptr;
	frameSubmit.pWaitDstStageMask = nullptr;
	frameSubmit.commandBufferCount = 1;
	frameSubmit.pCommandBuffers = &m_frameCommands.handle;
	frameSubmit.signalSemaphoreCount = 0;
	frameSubmit.pSignalSemaphores = nullptr;
	vkResetFences(m_pDeviceContext->device, 1, &m_frameCommandsFinished);
	vkQueueSubmit(m_pDeviceContext->directQueue, 1, &frameSubmit, m_frameCommandsFinished);
}
