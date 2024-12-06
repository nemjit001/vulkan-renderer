#include "renderer.hpp"

#include <cassert>
#include <stdexcept>

#include <imgui_impl_vulkan.h>
#include <volk.h>

#include "assets.hpp"
#include "camera.hpp"
#include "light.hpp"
#include "mesh.hpp"
#include "render_backend/buffer.hpp"
#include "render_backend/texture.hpp"
#include "render_backend/utils.hpp"
#include "scene.hpp"

namespace SceneHelpers
{
	void calcWorldSpaceTransforms(Scene const& scene, glm::mat4 const& parentTransform, std::vector<glm::mat4>& transforms, SceneRef node)
	{
		assert(node < scene.nodes.count);
		assert(node < transforms.size());

		transforms[node] = parentTransform * scene.nodes.transform[node].matrix();
		for (auto const& childRef : scene.nodes.children[node]) {
			calcWorldSpaceTransforms(scene, transforms[node], transforms, childRef);
		}
	};
}

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

	// Create shadow map rendering members
	{
		// Create shadow map render pass
		{
			VkAttachmentDescription depthStencilAttachment{};
			depthStencilAttachment.flags = 0;
			depthStencilAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
			depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference shadowMapDepthAttachment = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
			VkSubpassDescription shadowMapPass{};
			shadowMapPass.flags = 0;
			shadowMapPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			shadowMapPass.inputAttachmentCount = 0;
			shadowMapPass.pInputAttachments = nullptr;
			shadowMapPass.colorAttachmentCount = 0;
			shadowMapPass.pColorAttachments = nullptr;
			shadowMapPass.pResolveAttachments = nullptr;
			shadowMapPass.pDepthStencilAttachment = &shadowMapDepthAttachment;
			shadowMapPass.preserveAttachmentCount = 0;
			shadowMapPass.pPreserveAttachments = nullptr;

			VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
			renderPassCreateInfo.flags = 0;
			renderPassCreateInfo.attachmentCount = 1;
			renderPassCreateInfo.pAttachments = &depthStencilAttachment;
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &shadowMapPass;
			renderPassCreateInfo.dependencyCount = 0;
			renderPassCreateInfo.pDependencies = nullptr;

			if (VK_FAILED(vkCreateRenderPass(m_pDeviceContext->device, &renderPassCreateInfo, nullptr, &m_shadowMapRenderPass))) {
				throw std::runtime_error("Forward Renderer shadow map render pass create failed");
			}
		}

		// Create sun shadow map & forward framebuffers
		{
			m_sunShadowMap = m_pDeviceContext->createTexture(
				VK_IMAGE_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, SunShadowMapResolutionX, SunShadowMapResolutionY, 1
			);

			if (m_sunShadowMap == nullptr || !m_sunShadowMap->initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT)) {
				throw std::runtime_error("Forward Renderer sun shadow map create failed");
			}

			VkFramebufferCreateInfo sunShadowMapFramebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			sunShadowMapFramebufferCreateInfo.flags = 0;
			sunShadowMapFramebufferCreateInfo.renderPass = m_shadowMapRenderPass;
			sunShadowMapFramebufferCreateInfo.attachmentCount = 1;
			sunShadowMapFramebufferCreateInfo.pAttachments = &m_sunShadowMap->view;
			sunShadowMapFramebufferCreateInfo.width = SunShadowMapResolutionX;
			sunShadowMapFramebufferCreateInfo.height = SunShadowMapResolutionY;
			sunShadowMapFramebufferCreateInfo.layers = 1;

			vkCreateFramebuffer(m_pDeviceContext->device, &sunShadowMapFramebufferCreateInfo, nullptr, &m_sunShadowMapFramebuffer);
			assert(m_sunShadowMapFramebuffer != VK_NULL_HANDLE);
		}

		// Create shadow map pipeline descriptor set layouts
		{
			VkDescriptorSetLayoutBinding shadowMapCameraDataBinding{};
			shadowMapCameraDataBinding.binding = 0;
			shadowMapCameraDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			shadowMapCameraDataBinding.descriptorCount = 1;
			shadowMapCameraDataBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			shadowMapCameraDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo shadowMapCameraDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			shadowMapCameraDataSetLayout.flags = 0;
			shadowMapCameraDataSetLayout.bindingCount = 1;
			shadowMapCameraDataSetLayout.pBindings = &shadowMapCameraDataBinding;

			VkDescriptorSetLayoutBinding shadowMapObjectDataBinding{};
			shadowMapObjectDataBinding.binding = 0;
			shadowMapObjectDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			shadowMapObjectDataBinding.descriptorCount = 1;
			shadowMapObjectDataBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			shadowMapObjectDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo shadowMapObjectDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			shadowMapObjectDataSetLayout.flags = 0;
			shadowMapObjectDataSetLayout.bindingCount = 1;
			shadowMapObjectDataSetLayout.pBindings = &shadowMapObjectDataBinding;

			if (VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &shadowMapCameraDataSetLayout, nullptr, &m_shadowMapCameraDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &shadowMapObjectDataSetLayout, nullptr, &m_shadowMapObjectDataSetLayout))) {
				throw std::runtime_error("Forward Renderer shadow map descriptor set layout create failed");
			}
		}

		// Create shadow map pipeline layout
		{
			VkDescriptorSetLayout setLayouts[] = { m_shadowMapCameraDataSetLayout, m_shadowMapObjectDataSetLayout, };
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			pipelineLayoutCreateInfo.flags = 0;
			pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(std::size(setLayouts));
			pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
			pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
			pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

			if (VK_FAILED(vkCreatePipelineLayout(m_pDeviceContext->device, &pipelineLayoutCreateInfo, nullptr, &m_shadowMapPipelineLayout))) {
				throw std::runtime_error("Foward Renderer shadow map pipeline layout create failed");
			}
		}

		// Create shadow map graphics pipeline
		{
			std::vector<uint32_t> vertShaderCode{};
			if (!readShaderFile("shadow_map.vert.spv", vertShaderCode)) {
				throw std::runtime_error("Forward Renderer shader read failed (shadow map)");
			}

			VkShaderModuleCreateInfo shadowmapVertModuleCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };;
			shadowmapVertModuleCreateInfo.flags = 0;
			shadowmapVertModuleCreateInfo.codeSize = static_cast<uint32_t>(4 * vertShaderCode.size());
			shadowmapVertModuleCreateInfo.pCode = vertShaderCode.data();

			VkShaderModule shadowmapVertModule = VK_NULL_HANDLE;
			if (VK_FAILED(vkCreateShaderModule(m_pDeviceContext->device, &shadowmapVertModuleCreateInfo, nullptr, &shadowmapVertModule))) {
				throw std::runtime_error("Forward Renderer shader module create failed (shadow mapping)");
			}

			VkPipelineShaderStageCreateInfo shadowmapVertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			shadowmapVertStage.flags = 0;
			shadowmapVertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			shadowmapVertStage.module = shadowmapVertModule;
			shadowmapVertStage.pName = "main";
			shadowmapVertStage.pSpecializationInfo = nullptr;

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
			vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(std::size(bindingDescriptions));
			vertexInputState.pVertexBindingDescriptions = bindingDescriptions;
			vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributeDescriptions));
			vertexInputState.pVertexAttributeDescriptions = attributeDescriptions;

			VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			inputAssemblyState.flags = 0;
			inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssemblyState.primitiveRestartEnable = VK_FALSE;

			float viewportWidth = static_cast<float>(SunShadowMapResolutionX);
			float viewportHeight = static_cast<float>(SunShadowMapResolutionY);
			VkViewport viewport = VkViewport{ 0.0F, 0.0F, viewportWidth, viewportHeight, 0.0F, 1.0F };
			VkRect2D scissor = VkRect2D{ { 0, 0 }, { SunShadowMapResolutionX, SunShadowMapResolutionY } };

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
			rasterizationState.cullMode = VK_CULL_MODE_NONE;
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
			colorBlendState.attachmentCount = static_cast<uint32_t>(std::size(colorBlendAttachments));
			colorBlendState.pAttachments = colorBlendAttachments;
			colorBlendState.blendConstants[0] = 0.0F;
			colorBlendState.blendConstants[1] = 0.0F;
			colorBlendState.blendConstants[2] = 0.0F;
			colorBlendState.blendConstants[3] = 0.0F;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, };
			VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamicState.flags = 0;
			dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
			dynamicState.pDynamicStates = dynamicStates;

			VkGraphicsPipelineCreateInfo shadowMapPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			shadowMapPipelineCreateInfo.flags = 0;
			shadowMapPipelineCreateInfo.stageCount = 1;
			shadowMapPipelineCreateInfo.pStages = &shadowmapVertStage;
			shadowMapPipelineCreateInfo.pVertexInputState = &vertexInputState;
			shadowMapPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
			shadowMapPipelineCreateInfo.pTessellationState = nullptr;
			shadowMapPipelineCreateInfo.pViewportState = &viewportState;
			shadowMapPipelineCreateInfo.pRasterizationState = &rasterizationState;
			shadowMapPipelineCreateInfo.pMultisampleState = &multisampleState;
			shadowMapPipelineCreateInfo.pDepthStencilState = &depthStencilState;
			shadowMapPipelineCreateInfo.pColorBlendState = &colorBlendState;
			shadowMapPipelineCreateInfo.pDynamicState = &dynamicState;
			shadowMapPipelineCreateInfo.layout = m_shadowMapPipelineLayout;
			shadowMapPipelineCreateInfo.renderPass = m_shadowMapRenderPass;
			shadowMapPipelineCreateInfo.subpass = 0;
			shadowMapPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
			shadowMapPipelineCreateInfo.basePipelineIndex = 0;

			if (VK_FAILED(vkCreateGraphicsPipelines(m_pDeviceContext->device, VK_NULL_HANDLE, 1, &shadowMapPipelineCreateInfo, nullptr, &m_shadowMapPipeline))) {
				throw std::runtime_error("Forward renderer shadow map pipeline create failed");
			}

			vkDestroyShaderModule(m_pDeviceContext->device, shadowmapVertModule, nullptr);
		}

		// Create shader buffers
		{
			m_sunCameraDataBuffer = m_pDeviceContext->createBuffer(sizeof(UniformShadowMapCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			m_shadowMapObjectDataBuffer = m_pDeviceContext->createBuffer(sizeof(UniformShadowMapObjectData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			if (m_sunCameraDataBuffer == nullptr || m_shadowMapObjectDataBuffer == nullptr) {
				throw std::runtime_error("Forward Renderer shadow map shader buffer create failed");
			}
		}
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
			depthStencilAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
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

			VkAttachmentReference skyboxColorAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference skyboxDepthAttachment = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
			VkSubpassDescription skyboxPass{};
			skyboxPass.flags = 0;
			skyboxPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			skyboxPass.inputAttachmentCount = 0;
			skyboxPass.pInputAttachments = nullptr;
			skyboxPass.colorAttachmentCount = 1;
			skyboxPass.pColorAttachments = &skyboxColorAttachment;
			skyboxPass.pResolveAttachments = nullptr;
			skyboxPass.pDepthStencilAttachment = &skyboxDepthAttachment;
			skyboxPass.preserveAttachmentCount = 0;
			skyboxPass.pPreserveAttachments = nullptr;

			VkAttachmentReference GUIColorAttachment = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkSubpassDescription GUIPass{};
			GUIPass.flags = 0;
			GUIPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			GUIPass.inputAttachmentCount = 0;
			GUIPass.pInputAttachments = nullptr;
			GUIPass.colorAttachmentCount = 1;
			GUIPass.pColorAttachments = &skyboxColorAttachment;
			GUIPass.pResolveAttachments = nullptr;
			GUIPass.pDepthStencilAttachment = nullptr;
			GUIPass.preserveAttachmentCount = 0;
			GUIPass.pPreserveAttachments = nullptr;

			VkSubpassDependency prevFrameDependency{};
			prevFrameDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			prevFrameDependency.dstSubpass = 0;
			prevFrameDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			prevFrameDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			prevFrameDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			prevFrameDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkSubpassDependency skyboxDependency{};
			skyboxDependency.srcSubpass = 0;
			skyboxDependency.dstSubpass = 1;
			skyboxDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			skyboxDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			skyboxDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			skyboxDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

			VkSubpassDependency GUIDependency{};
			GUIDependency.srcSubpass = 1;
			GUIDependency.dstSubpass = 2;
			GUIDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			GUIDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			GUIDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			GUIDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[] = { colorAttachment, depthStencilAttachment, };
			VkSubpassDescription subpasses[] = { opaqueForwardPass, skyboxPass, GUIPass, };
			VkSubpassDependency dependencies[] = { prevFrameDependency, skyboxDependency, GUIDependency, };

			VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
			renderPassCreateInfo.flags = 0;
			renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(std::size(attachments));
			renderPassCreateInfo.pAttachments = attachments;
			renderPassCreateInfo.subpassCount = static_cast<uint32_t>(std::size(subpasses));
			renderPassCreateInfo.pSubpasses = subpasses;
			renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(std::size(dependencies));
			renderPassCreateInfo.pDependencies = dependencies;

			if (VK_FAILED(vkCreateRenderPass(m_pDeviceContext->device, &renderPassCreateInfo, nullptr, &m_forwardRenderPass))) {
				throw std::runtime_error("Forward Renderer forward render pass create failed");
			}
		}

		// Create depth stencil target & forward framebuffers
		{
			m_depthStencilTexture = m_pDeviceContext->createTexture(
				VK_IMAGE_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_framebufferWidth, m_framebufferHeight, 1
			);

			if (m_depthStencilTexture == nullptr || !m_depthStencilTexture->initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT)) {
				throw std::runtime_error("Forward Renderer depth stencil texture create failed");
			}

			auto backbuffers = m_pDeviceContext->getBackbuffers();
			for (auto& backbuffer : backbuffers)
			{
				VkImageView attachments[] = { backbuffer.view, m_depthStencilTexture->view, };

				VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
				framebufferCreateInfo.flags = 0;
				framebufferCreateInfo.renderPass = m_forwardRenderPass;
				framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(std::size(attachments));
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
				throw std::runtime_error("Forward Renderer immutable sampler create failed");
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

			VkDescriptorSetLayoutBinding sunLightDataBinding{};
			sunLightDataBinding.binding = 1;
			sunLightDataBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			sunLightDataBinding.descriptorCount = 1;
			sunLightDataBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			sunLightDataBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutBinding lightBufferBinding{};
			lightBufferBinding.binding = 2;
			lightBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			lightBufferBinding.descriptorCount = 1;
			lightBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			lightBufferBinding.pImmutableSamplers = nullptr;

			std::vector<VkSampler> textureSamplers(Scene::MaxTextures, m_textureSampler);
			VkDescriptorSetLayoutBinding textureArrayBinding{};
			textureArrayBinding.binding = 3;
			textureArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureArrayBinding.descriptorCount = Scene::MaxTextures;
			textureArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			textureArrayBinding.pImmutableSamplers = textureSamplers.data();

			VkDescriptorSetLayoutBinding sunShadowMapBinding{};
			sunShadowMapBinding.binding = 4;
			sunShadowMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			sunShadowMapBinding.descriptorCount = 1;
			sunShadowMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			sunShadowMapBinding.pImmutableSamplers = &m_shadowmapSampler;

			VkDescriptorSetLayoutBinding sceneDataBindings[] = {
				cameraDataBinding,
				sunLightDataBinding,
				lightBufferBinding,
				textureArrayBinding,
				sunShadowMapBinding,
			};
			VkDescriptorSetLayoutCreateInfo sceneDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			sceneDataSetLayout.flags = 0;
			sceneDataSetLayout.bindingCount = static_cast<uint32_t>(std::size(sceneDataBindings));
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
			materialDataSetLayout.bindingCount = static_cast<uint32_t>(std::size(materialDataBindings));
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
			objectDataSetLayout.bindingCount = static_cast<uint32_t>(std::size(objectDataBindings));
			objectDataSetLayout.pBindings = objectDataBindings;

			if (VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &sceneDataSetLayout, nullptr, &m_sceneDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &materialDataSetLayout, nullptr, &m_materialDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &objectDataSetLayout, nullptr, &m_objectDataSetLayout))) {
				throw std::runtime_error("Forward Renderer forward descriptor set layout create failed");
			}
		}

		// Create forward pipeline layout
		{
			VkDescriptorSetLayout setLayouts[] = { m_sceneDataSetLayout, m_materialDataSetLayout, m_objectDataSetLayout, };
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			pipelineLayoutCreateInfo.flags = 0;
			pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(std::size(setLayouts));
			pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
			pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
			pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

			if (VK_FAILED(vkCreatePipelineLayout(m_pDeviceContext->device, &pipelineLayoutCreateInfo, nullptr, &m_forwardPipelineLayout))) {
				throw std::runtime_error("Forward Renderer forward pipeline layout create failed");
			}
		}

		// Create forward graphics pipeline
		{
			std::vector<uint32_t> forwardVertCode{};
			std::vector<uint32_t> forwardFragCode{};
			if (!readShaderFile("forward.vert.spv", forwardVertCode)
				|| !readShaderFile("forward.frag.spv", forwardFragCode)) {
				throw std::runtime_error("Forward Renderer shader file read failed (forward opaque)");
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
				throw std::runtime_error("Forward Renderer shader module create failed (forward opaque)");
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
			vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(std::size(bindingDescriptions));
			vertexInputState.pVertexBindingDescriptions = bindingDescriptions;
			vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributeDescriptions));
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
			colorBlendState.attachmentCount = static_cast<uint32_t>(std::size(colorBlendAttachments));
			colorBlendState.pAttachments = colorBlendAttachments;
			colorBlendState.blendConstants[0] = 0.0F;
			colorBlendState.blendConstants[1] = 0.0F;
			colorBlendState.blendConstants[2] = 0.0F;
			colorBlendState.blendConstants[3] = 0.0F;

			VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, };
			VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamicState.flags = 0;
			dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
			dynamicState.pDynamicStates = dynamicStates;

			VkGraphicsPipelineCreateInfo forwardOpaqueCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			forwardOpaqueCreateInfo.flags = 0;
			forwardOpaqueCreateInfo.stageCount = static_cast<uint32_t>(std::size(forwardOpaqueStages));
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
				throw std::runtime_error("Forward Renderer forward pipeline create failed");
			}

			vkDestroyShaderModule(m_pDeviceContext->device, forwardFragModule, nullptr);
			vkDestroyShaderModule(m_pDeviceContext->device, forwardVertModule, nullptr);
		}
	}

	// Create uniform buffers
	{
		size_t cameraDataBufferSize = sizeof(UniformCameraData);
		size_t sunLightDataBufferSize = sizeof(UniformSunLightData);
		size_t lightDataBufferSize = sizeof(SSBOLightEntry);
		size_t materialBufferSize = sizeof(UniformMaterialData);
		size_t objectBufferSize = sizeof(UniformObjectData);

		m_cameraDataBuffer = m_pDeviceContext->createBuffer(cameraDataBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		m_sunLightDataBuffer = m_pDeviceContext->createBuffer(sunLightDataBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		m_lightBuffer = m_pDeviceContext->createBuffer(lightDataBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		m_materialDataBuffer = m_pDeviceContext->createBuffer(materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		m_objectDataBuffer = m_pDeviceContext->createBuffer(objectBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (m_cameraDataBuffer == nullptr || m_lightBuffer == nullptr || m_materialDataBuffer == nullptr || m_objectDataBuffer == nullptr) {
			throw std::runtime_error("Forward Renderer forward shader buffer create failed");
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
		guiPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(std::size(guiPoolSizes));
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
		initInfo.Subpass = 2;

		auto loaderFunc = [](char const* pName, void* pUserData) {
			(void)(pUserData);
			return vkGetInstanceProcAddr(RenderBackend::getInstance(), pName);
		};

		if (!ImGui_ImplVulkan_LoadFunctions(loaderFunc)
			|| !ImGui_ImplVulkan_Init(&initInfo)) {
			throw std::runtime_error("Forward Renderer ImGUI init failed");
		}
	}

	// Create skybox rendering members
	{
		// TODO(nemjit001): create skybox members
	}
}

ForwardRenderer::~ForwardRenderer()
{
	ImGui_ImplVulkan_Shutdown();

	// Destroy skybox data
	{
		m_skyboxSet = VK_NULL_HANDLE;
		vkDestroyDescriptorPool(m_pDeviceContext->device, m_skyboxDescriptorPool, nullptr);

		vkDestroyPipeline(m_pDeviceContext->device, m_skyboxPipeline, nullptr);
		vkDestroyPipelineLayout(m_pDeviceContext->device, m_skyboxPipelineLayout, nullptr);

		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_skyboxSetLayout, nullptr);

		vkDestroySampler(m_pDeviceContext->device, m_skyboxSampler, nullptr);
	}

	// Destroy forward pass data
	{
		m_objectSets.clear();
		m_materialSets.clear();
		m_sceneSet = VK_NULL_HANDLE;
		vkDestroyDescriptorPool(m_pDeviceContext->device, m_descriptorPool, nullptr);

		vkDestroyDescriptorPool(m_pDeviceContext->device, m_guiDescriptorPool, nullptr);

		// Destroy pipeline
		vkDestroyPipeline(m_pDeviceContext->device, m_forwardOpaquePipeline, nullptr);
		vkDestroyPipelineLayout(m_pDeviceContext->device, m_forwardPipelineLayout, nullptr);

		// Destroy descriptor layouts
		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_objectDataSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_materialDataSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_sceneDataSetLayout, nullptr);

		// Destroy samplers
		vkDestroySampler(m_pDeviceContext->device, m_textureSampler, nullptr);
		vkDestroySampler(m_pDeviceContext->device, m_shadowmapSampler, nullptr);

		// Destroy framebuffers & attachments
		for (auto& framebuffer : m_forwardFramebuffers) {
			vkDestroyFramebuffer(m_pDeviceContext->device, framebuffer, nullptr);
		}
		vkDestroyRenderPass(m_pDeviceContext->device, m_forwardRenderPass, nullptr);
	}

	// Destroy shadow map pass data
	{
		m_shadowMapObjectSets.clear();
		m_shadowMapCameraSet = VK_NULL_HANDLE;
		vkDestroyDescriptorPool(m_pDeviceContext->device, m_shadowMapDescriptorPool, nullptr);

		// Destroy pipeline
		vkDestroyPipeline(m_pDeviceContext->device, m_shadowMapPipeline, nullptr);
		vkDestroyPipelineLayout(m_pDeviceContext->device, m_shadowMapPipelineLayout, nullptr);

		// Destroy descriptor layouts
		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_shadowMapObjectDataSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_shadowMapCameraDataSetLayout, nullptr);

		// Destroy framebuffers & attachments
		vkDestroyFramebuffer(m_pDeviceContext->device, m_sunShadowMapFramebuffer, nullptr);
		vkDestroyRenderPass(m_pDeviceContext->device, m_shadowMapRenderPass, nullptr);
	}

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

	// Create swap dependent resources
	m_depthStencilTexture = m_pDeviceContext->createTexture(
		VK_IMAGE_TYPE_2D, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_framebufferWidth, m_framebufferHeight, 1
	);

	if (m_depthStencilTexture == nullptr || !m_depthStencilTexture->initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT)) {
		return false;
	}

	auto backbuffers = m_pDeviceContext->getBackbuffers();
	for (auto& backbuffer : backbuffers)
	{
		VkImageView attachments[] = { backbuffer.view, m_depthStencilTexture->view, };

		VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		framebufferCreateInfo.flags = 0;
		framebufferCreateInfo.renderPass = m_forwardRenderPass;
		framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(std::size(attachments));
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
	// Calculate world space transforms for all nodes in scene
	m_objectTransforms.resize(scene.nodes.count);
	for (auto const& root : scene.rootNodes) {
		SceneHelpers::calcWorldSpaceTransforms(scene, glm::identity<glm::mat4>(), m_objectTransforms, root);
	}

	// Set sun transform
	// TODO(nemjit001): Adjust to be more robust, eventually add cascades
	SceneRef const& camParent = scene.nodes.parentRef[scene.activeCamera];
	glm::mat4 const& camParentTransform = (camParent == RefUnused) ? glm::identity<glm::mat4>() : m_objectTransforms[camParent];

	glm::vec3 sunPosition = scene.nodes.transform[scene.activeCamera].position;
	sunPosition += -scene.sun.direction() * 0.5F * SunShadowExtent.z; // Offset by half Z extent

	glm::mat4 const sunProject = Camera::createOrtho(SunShadowExtent.x, SunShadowExtent.y, 1.0F, SunShadowExtent.z).matrix();
	glm::mat4 const sunView = camParentTransform * glm::lookAt(sunPosition, sunPosition + scene.sun.direction(), UP); // TODO(nemjit001): Figure out right transform matrix for cam-following sun light

	// Update shadow map pipeline state
	{
		// Gather object & draw data
		std::vector<UniformShadowMapObjectData> objectBuffer;
		objectBuffer.reserve(scene.nodes.count);
		m_shadowMapDrawData.clear();
		for (uint32_t i = 0; i < scene.nodes.count; i++)
		{
			if (scene.nodes.materialRef[i] != RefUnused && scene.nodes.meshRef[i] != RefUnused)
			{
				UniformShadowMapObjectData const objectData{
					m_objectTransforms[i],
				};

				ShadowMapDraw const draw{
					scene.nodes.meshRef[i], //< mesh index
					static_cast<uint32_t>(objectBuffer.size()), //< object index
				};

				objectBuffer.push_back(objectData);
				m_shadowMapDrawData.push_back(draw);
			}
		}

		// Resize shader buffers if needed
		size_t const objectBufferSize = objectBuffer.size() * sizeof(UniformShadowMapObjectData);
		if (objectBufferSize > m_shadowMapObjectDataBuffer->size()) {
			m_shadowMapObjectDataBuffer = m_pDeviceContext->createBuffer(objectBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (m_shadowMapObjectDataBuffer == nullptr) {
				throw std::runtime_error("Forward Renderer update uniform shadow map object buffer resize failed\n");
			}

			printf("Forward Renderer shadow map object uniform buffer resized: %zu bytes\n", m_shadowMapObjectDataBuffer->size());
		}

		// Upload shader data
		UniformShadowMapCameraData const sunCameraData {
			sunProject * sunView,
		};

		m_sunCameraDataBuffer->map();
		memcpy(m_sunCameraDataBuffer->data(), &sunCameraData, m_sunCameraDataBuffer->size());
		m_sunCameraDataBuffer->unmap();

		if (objectBuffer.size() > 0)
		{
			m_shadowMapObjectDataBuffer->map();
			memcpy(m_shadowMapObjectDataBuffer->data(), objectBuffer.data(), m_shadowMapObjectDataBuffer->size());
			m_shadowMapObjectDataBuffer->unmap();
		}

		// Reset descriptor pool & reallocate descriptor sets
		m_shadowMapObjectSets.resize(objectBuffer.size());
		uint32_t requiredDescriptorSets = 1 + m_shadowMapObjectSets.size();
		if (requiredDescriptorSets > m_maxShadowMapDescriptorSets)
		{
			m_maxShadowMapDescriptorSets = requiredDescriptorSets;

			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * m_maxShadowMapDescriptorSets },
			};

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			descriptorPoolCreateInfo.flags = 0;
			descriptorPoolCreateInfo.maxSets = m_maxShadowMapDescriptorSets;
			descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
			descriptorPoolCreateInfo.pPoolSizes = poolSizes;

			vkDestroyDescriptorPool(m_pDeviceContext->device, m_shadowMapDescriptorPool, nullptr);
			if (VK_FAILED(vkCreateDescriptorPool(m_pDeviceContext->device, &descriptorPoolCreateInfo, nullptr, &m_shadowMapDescriptorPool))) {
				throw std::runtime_error("Forward Renderer update descriptor pool realloc failed");
			}

			printf("Forward Renderer update reallocated shadow map descriptor pool (%u sets)\n", m_maxShadowMapDescriptorSets);
		}

		vkResetDescriptorPool(m_pDeviceContext->device, m_shadowMapDescriptorPool, 0 /* no flags*/);
		std::vector<VkDescriptorSetLayout> objectDescriptorSetLayouts(m_shadowMapObjectSets.size(), m_shadowMapObjectDataSetLayout);

		VkDescriptorSetAllocateInfo smCameraSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		smCameraSetAllocateInfo.descriptorPool = m_shadowMapDescriptorPool;
		smCameraSetAllocateInfo.descriptorSetCount = 1;
		smCameraSetAllocateInfo.pSetLayouts = &m_shadowMapCameraDataSetLayout;

		if (VK_FAILED(vkAllocateDescriptorSets(m_pDeviceContext->device, &smCameraSetAllocateInfo, &m_shadowMapCameraSet))) {
			throw std::runtime_error("Forward Renderer update scene data set alloc failed\n");
		}

		if (m_shadowMapObjectSets.size() > 0)
		{
			VkDescriptorSetAllocateInfo smObjectSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			smObjectSetAllocateInfo.descriptorPool = m_shadowMapDescriptorPool;
			smObjectSetAllocateInfo.descriptorSetCount = static_cast<uint32_t>(m_shadowMapObjectSets.size());
			smObjectSetAllocateInfo.pSetLayouts = objectDescriptorSetLayouts.data();

			if (VK_FAILED(vkAllocateDescriptorSets(m_pDeviceContext->device, &smObjectSetAllocateInfo, m_shadowMapObjectSets.data()))) {
				throw std::runtime_error("Forward Renderer update scene data set alloc failed\n");
			}
		}

		// Update all descriptor sets
		VkDescriptorBufferInfo cameraBufferInfo{};
		cameraBufferInfo.buffer = m_sunCameraDataBuffer->handle();
		cameraBufferInfo.offset = 0;
		cameraBufferInfo.range = m_sunCameraDataBuffer->size();

		VkWriteDescriptorSet cameraDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		cameraDataWrite.dstSet = m_shadowMapCameraSet;
		cameraDataWrite.dstBinding = 0;
		cameraDataWrite.dstArrayElement = 0;
		cameraDataWrite.descriptorCount = 1;
		cameraDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		cameraDataWrite.pBufferInfo = &cameraBufferInfo;
		vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &cameraDataWrite, 0, nullptr);

		for (size_t objectIdx = 0; objectIdx < objectBuffer.size(); objectIdx++)
		{
			VkDescriptorBufferInfo objectBufferInfo{};
			objectBufferInfo.buffer = m_shadowMapObjectDataBuffer->handle();
			objectBufferInfo.offset = objectIdx * sizeof(UniformShadowMapObjectData);
			objectBufferInfo.range = sizeof(UniformShadowMapObjectData);

			VkWriteDescriptorSet objectDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			objectDataWrite.dstSet = m_shadowMapObjectSets[objectIdx];
			objectDataWrite.dstBinding = 0;
			objectDataWrite.dstArrayElement = 0;
			objectDataWrite.descriptorCount = 1;
			objectDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			objectDataWrite.pBufferInfo = &objectBufferInfo;
			vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &objectDataWrite, 0, nullptr);
		}
	}

	// Update forward pipeline state
	{
		// Gather scene data buffer entries
		std::vector<SSBOLightEntry> lightBuffer;
		std::vector<UniformMaterialData> materialBuffer;
		std::vector<UniformObjectData> objectBuffer;
		std::vector<MeshDraw> draws;
		lightBuffer.reserve(scene.nodes.count);
		materialBuffer.reserve(scene.materials.size());
		objectBuffer.reserve(scene.nodes.count);
		draws.reserve(scene.nodes.count);

		// Gather material data
		for (auto const& material : scene.materials)
		{
			UniformMaterialData const materialData{
				material.defaultAlbedo,
				material.defaultSpecular,
				material.albedoTexture,
				material.specularTexture,
				material.normalTexture
			};

			materialBuffer.push_back(materialData);
		}

		// Gather lights, objects, draws
		for (uint32_t i = 0; i < scene.nodes.count; i++)
		{
			if (scene.nodes.lightRef[i] != RefUnused)
			{
				Light const& light = scene.lights[scene.nodes.lightRef[i]];
				SSBOLightEntry lightEntry{};
				lightEntry.type = static_cast<uint32_t>(light.type);
				lightEntry.color = light.color;
				lightEntry.positionOrDirection = glm::vec3(0);

				switch (light.type)
				{
				case LightType::Undefined:
					break;
				case LightType::Directional:
					lightEntry.positionOrDirection = glm::normalize(glm::vec3(glm::inverse(m_objectTransforms[i])[2]));
					break;
				case LightType::Point:
					lightEntry.positionOrDirection = scene.nodes.transform[i].position;
					break;
				default:
					break;
				}

				lightBuffer.push_back(lightEntry);
			}

			if (scene.nodes.materialRef[i] != RefUnused	&& scene.nodes.meshRef[i] != RefUnused)
			{
				glm::mat4 const model = m_objectTransforms[i];
				glm::mat4 const normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(model))));

				UniformObjectData const objectData{
					model,
					normal,
				};

				MeshDraw const draw{
					scene.nodes.materialRef[i],
					scene.nodes.meshRef[i],
					static_cast<uint32_t>(objectBuffer.size()),
				};

				objectBuffer.push_back(objectData);
				draws.push_back(draw);
			}
		}

		// Check uniform buffer sizes & recreate buffers if needed
		size_t const lightBufferSize = lightBuffer.size() * sizeof(SSBOLightEntry);
		size_t const materialBufferSize = materialBuffer.size() * sizeof(UniformMaterialData);
		size_t const objectBufferSize = objectBuffer.size() * sizeof(UniformObjectData);

		if (lightBufferSize > m_lightBuffer->size())
		{
			m_lightBuffer = m_pDeviceContext->createBuffer(lightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (m_lightBuffer == nullptr) {
				throw std::runtime_error("Forward Renderer update uniform light buffer resize failed\n");
			}

			printf("Forward Renderer light storage buffer resized: %zu bytes\n", m_lightBuffer->size());
		}

		if (materialBufferSize > m_materialDataBuffer->size())
		{
			m_materialDataBuffer = m_pDeviceContext->createBuffer(materialBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (m_materialDataBuffer == nullptr) {
				throw std::runtime_error("Forward Renderer update uniform material buffer resize failed\n");
			}

			printf("Forward Renderer material uniform buffer resized: %zu bytes\n", m_materialDataBuffer->size());
		}

		if (objectBufferSize > m_objectDataBuffer->size())
		{
			m_objectDataBuffer = m_pDeviceContext->createBuffer(objectBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (m_objectDataBuffer == nullptr) {
				throw std::runtime_error("Forward Renderer update uniform object buffer resize failed\n");
			}

			printf("Forward Renderer object uniform buffer resized: %zu bytes\n", m_objectDataBuffer->size());
		}

		// Upload shader data
		glm::mat4 const& camTransform = m_objectTransforms[scene.activeCamera];
		glm::mat4 const& camViewMatrix = glm::lookAt(Transform::getPosition(camTransform) + Transform::getForward(camTransform), Transform::getPosition(camTransform), UP);

		Camera const& camera = scene.cameras[scene.nodes.cameraRef[scene.activeCamera]];
		UniformCameraData const cameraData{
			Transform::getPosition(camTransform),
			camera.matrix() * camViewMatrix,
		};

		m_cameraDataBuffer->map();
		memcpy(m_cameraDataBuffer->data(), &cameraData, sizeof(UniformCameraData));
		m_cameraDataBuffer->unmap();

		UniformSunLightData const sunLightData{
			scene.sun.direction(),
			scene.sun.color,
			scene.sun.ambient,
			sunProject * sunView,
		};

		m_sunLightDataBuffer->map();
		memcpy(m_sunLightDataBuffer->data(), &sunLightData, sizeof(UniformSunLightData));
		m_sunLightDataBuffer->unmap();

		if (!lightBuffer.empty())
		{
			m_lightBuffer->map();
			memcpy(m_lightBuffer->data(), lightBuffer.data(), m_lightBuffer->size());
			m_lightBuffer->unmap();
		}

		if (!materialBuffer.empty())
		{
			m_materialDataBuffer->map();
			memcpy(m_materialDataBuffer->data(), materialBuffer.data(), m_materialDataBuffer->size());
			m_materialDataBuffer->unmap();
		}

		if (!objectBuffer.empty())
		{
			m_objectDataBuffer->map();
			memcpy(m_objectDataBuffer->data(), objectBuffer.data(), m_objectDataBuffer->size());
			m_objectDataBuffer->unmap();
		}

		// Size descriptor set arrays for this frame & reset descriptor pool
		m_materialSets.resize(materialBuffer.size());
		m_objectSets.resize(objectBuffer.size());

		// required = scene + materials[] + objects[]
		uint32_t requiredDescriptorSets = static_cast<uint32_t>(1 + m_materialSets.size() + m_objectSets.size());
		if (requiredDescriptorSets > m_maxDescriptorSets) {
			m_maxDescriptorSets = requiredDescriptorSets;

			VkDescriptorPoolSize poolSizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * m_maxDescriptorSets },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 * m_maxDescriptorSets },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * m_maxDescriptorSets },
			};

			VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			descriptorPoolCreateInfo.flags = 0;
			descriptorPoolCreateInfo.maxSets = m_maxDescriptorSets;
			descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
			descriptorPoolCreateInfo.pPoolSizes = poolSizes;

			vkDestroyDescriptorPool(m_pDeviceContext->device, m_descriptorPool, nullptr);
			if (VK_FAILED(vkCreateDescriptorPool(m_pDeviceContext->device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool))) {
				throw std::runtime_error("Forward Renderer update descriptor pool realloc failed");
			}

			printf("Forward Renderer update reallocated descriptor pool (%u sets)\n", m_maxDescriptorSets);
		}

		// Reallocate descriptor sets
		// FIXME(nemjit001): It takes a lot of time to reallocate descriptor sets each frame, smarter reuse scheme might be useful here
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
		VkDescriptorBufferInfo cameraDataBufferInfo{};
		cameraDataBufferInfo.buffer = m_cameraDataBuffer->handle();
		cameraDataBufferInfo.offset = 0;
		cameraDataBufferInfo.range = m_cameraDataBuffer->size();

		VkWriteDescriptorSet cameraDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		cameraDataWrite.dstSet = m_sceneSet;
		cameraDataWrite.dstBinding = 0;
		cameraDataWrite.dstArrayElement = 0;
		cameraDataWrite.descriptorCount = 1;
		cameraDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		cameraDataWrite.pBufferInfo = &cameraDataBufferInfo;
		vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &cameraDataWrite, 0, nullptr);

		VkDescriptorBufferInfo sunLightDataBufferInfo{};
		sunLightDataBufferInfo.buffer = m_sunLightDataBuffer->handle();
		sunLightDataBufferInfo.offset = 0;
		sunLightDataBufferInfo.range = m_sunLightDataBuffer->size();

		VkWriteDescriptorSet sunLightDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		sunLightDataWrite.dstSet = m_sceneSet;
		sunLightDataWrite.dstBinding = 1;
		sunLightDataWrite.dstArrayElement = 0;
		sunLightDataWrite.descriptorCount = 1;
		sunLightDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sunLightDataWrite.pBufferInfo = &sunLightDataBufferInfo;
		vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &sunLightDataWrite, 0, nullptr);

		VkDescriptorBufferInfo lightBufferInfo{};
		lightBufferInfo.buffer = m_lightBuffer->handle();
		lightBufferInfo.offset = 0;
		lightBufferInfo.range = m_lightBuffer->size();

		VkWriteDescriptorSet lightBufferWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		lightBufferWrite.dstSet = m_sceneSet;
		lightBufferWrite.dstBinding = 2;
		lightBufferWrite.dstArrayElement = 0;
		lightBufferWrite.descriptorCount = 1;
		lightBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		lightBufferWrite.pBufferInfo = &lightBufferInfo;
		vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &lightBufferWrite, 0, nullptr);

		for (size_t textureIdx = 0; textureIdx < scene.textures.size(); textureIdx++)
		{
			VkDescriptorImageInfo textureImageInfo{};
			textureImageInfo.sampler = VK_NULL_HANDLE; //< Uses immutable sampler
			textureImageInfo.imageView = scene.textures[textureIdx]->view;
			textureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet textureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			textureWrite.dstSet = m_sceneSet;
			textureWrite.dstBinding = 3;
			textureWrite.dstArrayElement = static_cast<uint32_t>(textureIdx);
			textureWrite.descriptorCount = 1;
			textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureWrite.pImageInfo = &textureImageInfo;
			vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &textureWrite, 0, nullptr);
		}

		VkDescriptorImageInfo sunShadowMapImageInfo{};
		sunShadowMapImageInfo.sampler = VK_NULL_HANDLE; //< Uses immutable sampler
		sunShadowMapImageInfo.imageView = m_sunShadowMap->view;
		sunShadowMapImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet sunShadowMapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		sunShadowMapWrite.dstSet = m_sceneSet;
		sunShadowMapWrite.dstBinding = 4;
		sunShadowMapWrite.dstArrayElement = 0;
		sunShadowMapWrite.descriptorCount = 1;
		sunShadowMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sunShadowMapWrite.pImageInfo = &sunShadowMapImageInfo;
		vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &sunShadowMapWrite, 0, nullptr);

		for (size_t materialIdx = 0; materialIdx < materialBuffer.size(); materialIdx++)
		{
			VkDescriptorBufferInfo materialDataBufferInfo{};
			materialDataBufferInfo.buffer = m_materialDataBuffer->handle();
			materialDataBufferInfo.offset = materialIdx * sizeof(UniformMaterialData);
			materialDataBufferInfo.range = sizeof(UniformMaterialData);

			VkWriteDescriptorSet materialDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			materialDataWrite.dstSet = m_materialSets[materialIdx];
			materialDataWrite.dstBinding = 0;
			materialDataWrite.dstArrayElement = 0;
			materialDataWrite.descriptorCount = 1;
			materialDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			materialDataWrite.pBufferInfo = &materialDataBufferInfo;
			vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &materialDataWrite, 0, nullptr);
		}

		for (size_t objectIdx = 0; objectIdx < objectBuffer.size(); objectIdx++)
		{
			VkDescriptorBufferInfo objectDataBufferInfo{};
			objectDataBufferInfo.buffer = m_objectDataBuffer->handle();
			objectDataBufferInfo.offset = objectIdx * sizeof(UniformObjectData);
			objectDataBufferInfo.range = sizeof(UniformObjectData);

			VkWriteDescriptorSet objectDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			objectDataWrite.dstSet = m_objectSets[objectIdx];
			objectDataWrite.dstBinding = 0;
			objectDataWrite.dstArrayElement = 0;
			objectDataWrite.descriptorCount = 1;
			objectDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			objectDataWrite.pBufferInfo = &objectDataBufferInfo;
			vkUpdateDescriptorSets(m_pDeviceContext->device, 1, &objectDataWrite, 0, nullptr);
		}

		// Sort mesh draws by material to reduce state changes in draw loop
		m_forwardDrawData.clear();
		for (auto const& draw : draws) {
			m_forwardDrawData[draw.material].emplace_back(draw);
		}
	}

	// Update skybox pipeline state
	{
		// TODO(nemjit001): Reallocate descriptor set -> only populate or render if skybox texture is set
	}
}

void ForwardRenderer::render(Scene const& scene)
{
	VkCommandBufferBeginInfo frameBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	frameBeginInfo.flags = 0;
	frameBeginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(m_frameCommands.handle, &frameBeginInfo);

	// Render shadow mapping pass
	{
		VkClearValue clearValues[] = {
			VkClearValue{{ 1.0F, 0x00 }},
		};

		VkRenderPassBeginInfo shadowMapPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		shadowMapPassBeginInfo.renderPass = m_shadowMapRenderPass;
		shadowMapPassBeginInfo.framebuffer = m_sunShadowMapFramebuffer;
		shadowMapPassBeginInfo.renderArea = VkRect2D{ { 0, 0 }, { SunShadowMapResolutionX, SunShadowMapResolutionY } };
		shadowMapPassBeginInfo.clearValueCount = static_cast<uint32_t>(std::size(clearValues));
		shadowMapPassBeginInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(m_frameCommands.handle, &shadowMapPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		
		// Set viewport & scissor
		float viewportWidth = static_cast<float>(SunShadowMapResolutionX);
		float viewportHeight = static_cast<float>(SunShadowMapResolutionY);
		VkViewport viewport = { 0.0F, 0.0F, viewportWidth, viewportHeight, 0.0F, 1.0F };
		VkRect2D scissor = { { 0, 0 }, { SunShadowMapResolutionX, SunShadowMapResolutionY } };
		vkCmdSetViewport(m_frameCommands.handle, 0, 1, &viewport);
		vkCmdSetScissor(m_frameCommands.handle, 0, 1, &scissor);

		// Render shadow mapping objects
		vkCmdBindPipeline(m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowMapPipeline);
		vkCmdBindDescriptorSets( // Bind camera data set
			m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowMapPipelineLayout,
			0, 1, &m_shadowMapCameraSet,
			0, nullptr
		);

		for (auto const& draw : m_shadowMapDrawData)
		{
			vkCmdBindDescriptorSets( // Bind object data set
				m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowMapPipelineLayout,
				1, 1, &m_shadowMapObjectSets[draw.objectIndex],
				0, nullptr
			);

			std::shared_ptr<Mesh> const& mesh = scene.meshes[draw.mesh];
			VkBuffer vertexBuffers[] = { mesh->vertexBuffer->handle(), };
			VkDeviceSize const offsets[] = { 0, };
			vkCmdBindVertexBuffers(m_frameCommands.handle, 0, static_cast<uint32_t>(std::size(vertexBuffers)), vertexBuffers, offsets);
			vkCmdBindIndexBuffer(m_frameCommands.handle, mesh->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(m_frameCommands.handle, mesh->indexCount, 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(m_frameCommands.handle);
	}

	// Render forward passes
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
		forwardPassBeginInfo.clearValueCount = static_cast<uint32_t>(std::size(clearValues));
		forwardPassBeginInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(m_frameCommands.handle, &forwardPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		
		// Set viewport & scissor
		float viewportWidth = static_cast<float>(m_framebufferWidth);
		float viewportHeight = static_cast<float>(m_framebufferHeight);
		VkViewport viewport = { 0.0F, viewportHeight, viewportWidth, -viewportHeight, 0.0F, 1.0F };
		VkRect2D scissor = { { 0, 0 }, { m_framebufferWidth, m_framebufferHeight } };
		vkCmdSetViewport(m_frameCommands.handle, 0, 1, &viewport);
		vkCmdSetScissor(m_frameCommands.handle, 0, 1, &scissor);

		// Forward opaque pass
		vkCmdBindPipeline(m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardOpaquePipeline);
		vkCmdBindDescriptorSets( // Bind scene data set
			m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
			0, 1, &m_sceneSet,
			0, nullptr
		);

		for (auto const& kvp : m_forwardDrawData)
		{
			uint32_t materialIdx = kvp.first;
			vkCmdBindDescriptorSets( // Bind material data set
				m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
				1, 1, &m_materialSets[materialIdx],
				0, nullptr
			);

			for (MeshDraw const& draw : kvp.second)
			{
				vkCmdBindDescriptorSets( // Bind object data set
					m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
					2, 1, &m_objectSets[draw.objectIndex],
					0, nullptr
				);

				std::shared_ptr<Mesh> const& mesh = scene.meshes[draw.mesh];
				VkBuffer vertexBuffers[] = { mesh->vertexBuffer->handle(), };
				VkDeviceSize const offsets[] = { 0, };
				vkCmdBindVertexBuffers(m_frameCommands.handle, 0, static_cast<uint32_t>(std::size(vertexBuffers)), vertexBuffers, offsets);
				vkCmdBindIndexBuffer(m_frameCommands.handle, mesh->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(m_frameCommands.handle, mesh->indexCount, 1, 0, 0, 0);
			}
		}

		// Skybox pass
		vkCmdNextSubpass(m_frameCommands.handle, VK_SUBPASS_CONTENTS_INLINE);
		if (scene.skybox != nullptr) // Only render if actually set in scene
		{
			vkCmdBindPipeline(m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
			vkCmdBindDescriptorSets(m_frameCommands.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout,
				0, 1, &m_skyboxSet,
				0, nullptr
			);

			VkBuffer vertexBuffers[] = { m_skyboxMesh->vertexBuffer->handle(), };
			VkDeviceSize const offsets[] = { 0, };
			vkCmdBindVertexBuffers(m_frameCommands.handle, 0, static_cast<uint32_t>(std::size(vertexBuffers)), vertexBuffers, offsets);
			vkCmdBindIndexBuffer(m_frameCommands.handle, m_skyboxMesh->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(m_frameCommands.handle, m_skyboxMesh->indexCount, 1, 0, 0, 0);
		}

		// GUI pass
		vkCmdNextSubpass(m_frameCommands.handle, VK_SUBPASS_CONTENTS_INLINE);
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
