#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "math.hpp"
#include "render_backend.hpp"
#include "render_backend/texture.hpp"

class Buffer;
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
		alignas(16) glm::mat4 matrix;
	};

	/// @brief Uniform material parameters, aligned for use on the GPU.
	/// Expects textures to be bound in sampler arrays.
	struct alignas(64) UniformMaterialData
	{
		alignas(16) glm::vec3 albedo;
		alignas(16) glm::vec3 specular;
		alignas(4)  uint32_t albedoMapIndex;
		alignas(4)  uint32_t specularMapIndex;
		alignas(4)  uint32_t normalMapIndex;
	};

	/// @brief Uniform object data, aligned for use on the GPU.
	struct alignas(64) UniformObjectData
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

	//-- Forward render pass data --//
	VkRenderPass m_forwardRenderPass = VK_NULL_HANDLE;
	Texture m_depthStencilTexture{};
	std::vector<VkFramebuffer> m_forwardFramebuffers{};

	//-- Samplers --//
	VkSampler m_shadowmapSampler = VK_NULL_HANDLE;
	VkSampler m_textureSampler = VK_NULL_HANDLE;

	//-- Descriptor layouts --//
	VkDescriptorSetLayout m_sceneDataSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_materialDataSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_objectDataSetLayout = VK_NULL_HANDLE;

	//-- Forward pipeline --//
	VkPipelineLayout m_forwardPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_forwardOpaquePipeline = VK_NULL_HANDLE;

	//-- Uniform buffers --//
	std::shared_ptr<Buffer> m_sceneDataBuffer = nullptr; //< contains camera data.
	std::shared_ptr<Buffer> m_materialDataBuffer = nullptr; //< contains all materials in the scene.
	std::shared_ptr<Buffer> m_objectDataBuffer = nullptr; //< contains a node's world transforms (model + normal matrix).
	std::vector<glm::mat4> m_objectTransforms{}; //< contains cached world space transforms

	//-- GUI state management --//
	VkDescriptorPool m_guiDescriptorPool = VK_NULL_HANDLE;

	//-- Descriptor set management --//
	uint32_t m_maxDescriptorSets = 0;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_sceneSet = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_materialSets{};
	std::vector<VkDescriptorSet> m_objectSets{};

	//-- Optimized draw data --//
	std::unordered_map<uint32_t, std::vector<uint32_t>> m_drawData; //< Material:Node[] mapping
};
