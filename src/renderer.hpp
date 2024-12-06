#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "math.hpp"
#include "render_backend.hpp"

class Buffer;
class Mesh;
class Scene;
class Texture;

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
	static constexpr uint32_t SunShadowMapResolutionX = 4096;
	static constexpr uint32_t SunShadowMapResolutionY = 4096;
	static constexpr glm::vec3 SunShadowExtent = glm::vec3(5'000.0F, 5'000.0F, 5'000.0F);

	/// @brief Uniform shadow map camera parameters.
	struct UniformShadowMapCameraData
	{
		alignas(16) glm::mat4 matrix;
	};

	/// @brief Uniform shadow map object parameters.
	struct alignas(64) UniformShadowMapObjectData
	{
		alignas(16) glm::mat4 model;
	};

	/// @brief Uniform camera parameters, aligned for use on the GPU.
	struct UniformCameraData
	{
		alignas(16) glm::vec3 position;
		alignas(16) glm::mat4 matrix;
	};

	/// @brief Uniform sunlight parameters, aligned for use on the GPU.
	struct UniformSunLightData
	{
		alignas(16) glm::vec3 direction;
		alignas(16) glm::vec3 color;
		alignas(16) glm::vec3 ambient;
		alignas(16) glm::mat4 lightSpaceTransform;
	};

	/// @brief Shader storage light buffer entry.
	struct SSBOLightEntry
	{
		alignas(4) uint32_t type;
		alignas(16) glm::vec3 color;
		alignas(16) glm::vec3 positionOrDirection;
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

	/// @brief Uniform skybox data, expects position to be removed from view matrix.
	struct UniformSkyboxCameraData
	{
		alignas(16) glm::mat4 matrix;
	};

	/// @brief Shadow map mesh draw in renderer, uses no materials.
	struct ShadowMapDraw
	{
		uint32_t mesh;
		uint32_t objectIndex;
	};

	/// @brief Mesh draw in renderer, associates a mesh with an object index and material
	struct MeshDraw
	{
		uint32_t material;
		uint32_t mesh;
		uint32_t objectIndex;
	};

	//-- State tracking --//
	uint32_t m_framebufferWidth = 0;
	uint32_t m_framebufferHeight = 0;
	std::vector<glm::mat4> m_objectTransforms{}; //< contains cached world space transforms for objects in scene

	//-- Command recording --//
	VkFence m_frameCommandsFinished = VK_NULL_HANDLE;
	CommandContext m_frameCommands{};

	//-- Shadow mapping pass --//
		//-- Shadow map render pass data --//
		VkRenderPass m_shadowMapRenderPass = VK_NULL_HANDLE;
		std::shared_ptr<Texture> m_sunShadowMap = nullptr;
		VkFramebuffer m_sunShadowMapFramebuffer = VK_NULL_HANDLE;

		//-- Descriptor layouts --//
		VkDescriptorSetLayout m_shadowMapCameraDataSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_shadowMapObjectDataSetLayout = VK_NULL_HANDLE;

		//-- Shadow map pipeline --//
		VkPipelineLayout m_shadowMapPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_shadowMapPipeline = VK_NULL_HANDLE;

		//-- Shadow map shader buffers --//
		std::shared_ptr<Buffer> m_sunCameraDataBuffer = nullptr;
		std::shared_ptr<Buffer> m_shadowMapObjectDataBuffer = nullptr;

		//-- Descriptor set management --//
		uint32_t m_maxShadowMapDescriptorSets = 0;
		VkDescriptorPool m_shadowMapDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_shadowMapCameraSet = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_shadowMapObjectSets{};

		//-- Optimized draw data for shadow mapping pass --//
		std::vector<ShadowMapDraw> m_shadowMapDrawData{};

	//-- Forward rendering pass --//
		//-- Forward render pass data --//
		VkRenderPass m_forwardRenderPass = VK_NULL_HANDLE;
		std::shared_ptr<Texture> m_depthStencilTexture = nullptr;
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

		//-- Forward shader buffers --//
		std::shared_ptr<Buffer> m_cameraDataBuffer = nullptr; //< contains camera data.
		std::shared_ptr<Buffer> m_sunLightDataBuffer = nullptr; //< contains sun light data.
		std::shared_ptr<Buffer> m_lightBuffer = nullptr; //< contains all lights in the scene.
		std::shared_ptr<Buffer> m_materialDataBuffer = nullptr; //< contains all materials in the scene.
		std::shared_ptr<Buffer> m_objectDataBuffer = nullptr; //< contains a node's world transforms (model + normal matrix).

		//-- GUI state management --//
		VkDescriptorPool m_guiDescriptorPool = VK_NULL_HANDLE;

		//-- Descriptor set management --//
		uint32_t m_maxDescriptorSets = 0;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_sceneSet = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_materialSets{};
		std::vector<VkDescriptorSet> m_objectSets{};

		//-- Optimized draw data for forward pass --//
		std::unordered_map<uint32_t, std::vector<MeshDraw>> m_forwardDrawData;

	//-- Skybox pass (forward subpass: reuses render pass, framebuffers, depth stencil) --//
		//-- Mesh and sampler --//
		std::shared_ptr<Mesh> m_skyboxMesh = nullptr;
		VkSampler m_skyboxSampler = VK_NULL_HANDLE;

		//-- Descriptor layouts --//
		VkDescriptorSetLayout m_skyboxSetLayout = VK_NULL_HANDLE;

		//-- Skybox pipeline --//
		VkPipelineLayout m_skyboxPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_skyboxPipeline = VK_NULL_HANDLE;

		//-- Skybox shader buffers --//
		std::shared_ptr<Buffer> m_skyboxCameraDataBuffer = nullptr;

		//-- Descriptor set management --//
		VkDescriptorPool m_skyboxDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_skyboxSet = VK_NULL_HANDLE;
};
