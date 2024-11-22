#include "renderer.hpp"

#include <cassert>
#include <stdexcept>

#include <volk.h>

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
			VkDescriptorSetLayoutCreateInfo sceneDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			sceneDataSetLayout.flags = 0;
			sceneDataSetLayout.bindingCount = 0;
			sceneDataSetLayout.pBindings = nullptr;

			VkDescriptorSetLayoutCreateInfo materialDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			materialDataSetLayout.flags = 0;
			materialDataSetLayout.bindingCount = 0;
			materialDataSetLayout.pBindings = nullptr;

			VkDescriptorSetLayoutCreateInfo objectDataSetLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			objectDataSetLayout.flags = 0;
			objectDataSetLayout.bindingCount = 0;
			objectDataSetLayout.pBindings = nullptr;

			if (VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &sceneDataSetLayout, nullptr, &m_forwardSceneDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &materialDataSetLayout, nullptr, &m_forwardMaterialDataSetLayout))
				|| VK_FAILED(vkCreateDescriptorSetLayout(m_pDeviceContext->device, &objectDataSetLayout, nullptr, &m_forwardObjectDataSetLayout))) {
				throw std::runtime_error("Forward Renderer forward descriptor set layout create failed\n");
			}
		}

		// Create forward pipeline layout
		{
			VkDescriptorSetLayout setLayouts[] = { m_forwardSceneDataSetLayout, m_forwardMaterialDataSetLayout, m_forwardObjectDataSetLayout, };
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

		// Create forward graphics pipelines
		{

		}
	}
}

ForwardRenderer::~ForwardRenderer()
{
	// Await previous frame to be finished
	vkWaitForFences(m_pDeviceContext->device, 1, &m_frameCommandsFinished, VK_TRUE, UINT64_MAX);

	// Destroy forward rendering members
	vkDestroyDescriptorPool(m_pDeviceContext->device, m_forwardDescriptorPool, nullptr);

	vkDestroyPipeline(m_pDeviceContext->device, m_shadowmappingPipeline, nullptr);

	vkDestroyPipelineLayout(m_pDeviceContext->device, m_forwardPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_forwardObjectDataSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_forwardMaterialDataSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_pDeviceContext->device, m_forwardSceneDataSetLayout, nullptr);

	vkDestroySampler(m_pDeviceContext->device, m_textureSampler, nullptr);
	vkDestroySampler(m_pDeviceContext->device, m_shadowmapSampler, nullptr);

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
	// TODO(nemjit001): preprocess scene data, create descriptor sets, upload to uniform buffers, etc.
}

void ForwardRenderer::render(Scene const& scene)
{
	awaitFrame();

	VkCommandBufferBeginInfo frameBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	frameBeginInfo.flags = 0;
	frameBeginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(m_frameCommands.handle, &frameBeginInfo);

	// Render shadowmapping pass (per light that might shadow map, record scene)
	// TODO(nemjit001): render shadow mapping pass

	// Render forward pass
	{
		uint32_t backbufferIndex = m_pDeviceContext->getCurrentBackbufferIndex();

		VkClearValue clearValues[] = {
			{{ 0.3F, 0.6F, 0.9F, 1.0F }},
			{{ 1.0F, 0x00 }},
		};

		VkRenderPassBeginInfo forwardPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		forwardPassBeginInfo.renderPass = m_forwardRenderPass;
		forwardPassBeginInfo.framebuffer = m_forwardFramebuffers[backbufferIndex];
		forwardPassBeginInfo.renderArea = VkRect2D{ { 0, 0 }, { m_framebufferWidth, m_framebufferHeight } };
		forwardPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
		forwardPassBeginInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(m_frameCommands.handle, &forwardPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		// TODO(nemjit001): render scene data using forward pipeline here

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
