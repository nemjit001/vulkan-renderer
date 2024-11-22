#pragma once

#include <cstdint>
#include <vector>

#include "math.hpp"
#include "render_backend.hpp"

class Scene;

/// @brief Renderer interface, manages render passes, pipelines, render command recording, etc. internally.
class IRenderer
{
public:
	IRenderer(RenderDeviceContext* pDeviceContext);
	virtual ~IRenderer() = default;

	virtual void awaitFrame() = 0;

	virtual bool onResize(uint32_t width, uint32_t height) = 0;

	virtual void update(Scene const& scene) = 0;

	virtual void render(Scene const& scene) = 0;

protected:
	RenderDeviceContext* m_pDeviceContext = nullptr;
};

/// @brief Forward renderer. Implements forward shading pipeline for opaque and transparent objects.
class ForwardRenderer
	:
	public IRenderer
{
public:
	ForwardRenderer(RenderDeviceContext* pDeviceContext, uint32_t framebufferWidth, uint32_t framebufferHeight);
	virtual ~ForwardRenderer();

	ForwardRenderer(ForwardRenderer const&) = delete;
	ForwardRenderer& operator=(ForwardRenderer const&) = delete;

	virtual void awaitFrame() override;

	virtual bool onResize(uint32_t width, uint32_t height) override;

	virtual void update(Scene const& scene) override;

	virtual void render(Scene const& scene) override;

protected:
	/// @brief Uniform camera parameters, aligned for use on the GPU.
	struct UniformCameraData
	{
		alignas(16) glm::vec3 position;
		alignas(16) glm::mat3 matrix;
	};

	/// @brief Uniform material parameters, aligned for use on the GPU.
	/// Expects textures to be bound in sampler arrays.
	struct UniformMaterialData
	{
		alignas(16) glm::vec3 albedo;
		alignas(16) glm::vec3 specular;
		alignas(4)  uint32_t albedoMapIndex;
		alignas(4)  uint32_t specularMapIndex;
		alignas(4)  uint32_t normalMapIndex;
	};

	/// @brief Uniform object data, aligned for use on the GPU.
	struct UniformObjectData
	{
		alignas(16) glm::mat4 model;
		alignas(16) glm::mat4 normal;
	};

	//-- State tracking --//
	uint32_t m_framebufferWidth = 0;
	uint32_t m_framebufferHeight = 0;

	//-- Command recording --//
	VkFence m_frameCommandsFinished = VK_NULL_HANDLE;
	CommandContext m_frameCommands{};

	//-- Shadow mapping members --//
	VkRenderPass m_shadowmappingRenderPass = VK_NULL_HANDLE;
	std::vector<Texture> m_shadowmaps{};
	std::vector<VkFramebuffer> m_shadowmappingFramebuffers{};

	VkDescriptorSetLayout m_shadowmappingSceneDataSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_shadowmappingObjectDataSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_shadowmappingPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_shadowmappingPipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_shadowmappingDescriptorPool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_shadowmappingSceneDataSets{};
	std::vector<VkDescriptorSet> m_shadowmappingObjectDataSets{};

	//-- Forward rendering members --//
	VkRenderPass m_forwardRenderPass = VK_NULL_HANDLE;
	Texture m_depthStencilTexture{};
	std::vector<VkFramebuffer> m_forwardFramebuffers{};

	VkSampler m_shadowmapSampler = VK_NULL_HANDLE;
	VkSampler m_textureSampler = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_forwardSceneDataSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_forwardMaterialDataSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_forwardObjectDataSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_forwardPipelineLayout = VK_NULL_HANDLE;

	VkPipeline m_forwardOpaquePipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_forwardDescriptorPool = VK_NULL_HANDLE;
};
