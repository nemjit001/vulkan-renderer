#pragma once

#include "assets.hpp"
#include "camera.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "transform.hpp"

constexpr glm::vec3 FORWARD = glm::vec3(0.0F, 0.0F, 1.0F);
constexpr glm::vec3 UP      = glm::vec3(0.0F, 1.0F, 0.0F);
constexpr glm::vec3 RIGHT   = glm::vec3(1.0F, 0.0F, 0.0F);

/// @brief Renderable object.
class Object
{
public:
    Object() = default;

    /// @brief Create a new object.
    /// @param pDeviceContext Device context pointer.
    /// @param objectDataSet Object data descriptor set, preallocated.
    /// @param mesh Mesh struct, externally managed.
    /// @param colorTextureView Color texture view, externally managed.
    /// @param normalTextureView Normal texture view, externally managed.
    Object(
        RenderDeviceContext* pDeviceContext,
        VkDescriptorSet objectDataSet,
        Mesh mesh,
        VkImageView colorTextureView,
        VkImageView normalTextureView
    );

    /// @brief Destroy the object.
    void destroy();

    /// @brief Update the object state, uploading object-specific data to the GPU.
    void update();

public:
    RenderDeviceContext* pDeviceContext;
    VkDescriptorSet objectDataSet;
    Mesh mesh;
    VkImageView colorTextureView;
    VkImageView normalTextureView;

    Buffer objectDataBuffer{};

    Transform transform{};
    float specularity = 0.5F;
};

class Scene
{
public:
    Scene() = default;

    Scene(
        RenderDeviceContext* pDeviceContext,
        VkPipelineLayout forwardPipelineLayout,
        VkPipeline depthPrepassPipeline,
        VkPipeline forwardPipeline,
        VkDescriptorSet sceneDataSet,
        VkDescriptorSet lightDataSet
    );

    void destroy();

    void update();

    void render(
        VkCommandBuffer commandBuffer,
        VkRenderPass renderPass,
        VkFramebuffer framebuffer,
        uint32_t framebufferWidth,
        uint32_t framebufferHeight
    );

public:
    static constexpr uint32_t MaxSceneObjects = 1024;

    RenderDeviceContext* pDeviceContext = nullptr;
    VkPipelineLayout forwardPipelineLayout = VK_NULL_HANDLE;
    VkPipeline depthPrepassPipeline = VK_NULL_HANDLE;
    VkPipeline forwardPipeline = VK_NULL_HANDLE;
    VkDescriptorSet sceneDataSet = VK_NULL_HANDLE;
    VkDescriptorSet lightDataSet = VK_NULL_HANDLE;

    VkDescriptorPool objectDescriptorPool = VK_NULL_HANDLE;
    Buffer sceneDataBuffer{};
    Buffer lightDataBuffer{};

    std::vector<Mesh> meshes{};
    std::vector<Texture> textures{};
    std::vector<VkImageView> textureViews{};

    Transform cameraTransform{};
    Camera camera{};

    float shadowmapXMag = 10.0F;
    float shadowmapYMag = 10.0F;
    float shadowmapDepth = 10.0F;
    float sunAzimuth = 0.0F;
    float sunZenith = 0.0F;
    glm::vec3 sunColor = glm::vec3(1.0F);
    glm::vec3 ambientLight = glm::vec3(0.1F);

    std::vector<Object> objects{};
};
