#pragma once

#include "assets.hpp"
#include "mesh.hpp"
#include "renderer.hpp"

/// @brief Simple TRS transform.
struct Transform
{
    /// @brief Calculate the transformation matrix for this transform.
    /// @return 
    glm::mat4 matrix() const;

    glm::vec3 position = glm::vec3(0.0F);
    glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    glm::vec3 scale = glm::vec3(1.0F);
};

/// @brief Virtual camera.
struct Camera
{
    /// @brief Calculate the view and projection matrix for this camera.
    /// @return 
    glm::mat4 matrix() const;

    // Camera transform
    glm::vec3 position = glm::vec3(0.0F);
    glm::vec3 forward = glm::vec3(0.0F, 0.0F, 1.0F);
    glm::vec3 up = glm::vec3(0.0F, 1.0F, 0.0F);

    // Perspective camera data
    float FOVy = 60.0F;
    float aspectRatio = 1.0F;
    float zNear = 0.1F;
    float zFar = 100.0F;
};

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

    Scene(RenderDeviceContext* pDeviceContext, VkDescriptorSet sceneDataSet);

    void destroy();

    void update();

public:
    static constexpr uint32_t MaxSceneObjects = 1024;

    RenderDeviceContext* pDeviceContext;
    VkDescriptorSet sceneDataSet;

    VkDescriptorPool objectDescriptorPool = VK_NULL_HANDLE;
    Buffer sceneDataBuffer{};

    std::vector<Mesh> meshes{};
    std::vector<Texture> textures{};
    std::vector<VkImageView> textureViews{};

    float sunAzimuth = 0.0F;
    float sunZenith = 0.0F;
    glm::vec3 sunColor = glm::vec3(1.0F);
    glm::vec3 ambientLight = glm::vec3(0.1F);
    Camera camera{};

    std::vector<Object> objects{};
};
