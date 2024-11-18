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

/// @brief Uniform per-object data.
struct UniformObjectData
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 normal;
    alignas(4)  float specularity;
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
    /// @param colorTexture Color texture, externally managed.
    /// @param normalTexture Normal texture, externally managed.
    Object(
        RenderDeviceContext* pDeviceContext,
        VkDescriptorSet objectDataSet,
        Mesh mesh,
        Texture colorTexture,
        Texture normalTexture
    );

    /// @brief Destroy the object.
    void destroy();

    /// @brief Update the object, uploading data to the GPU.
    void update();

public:
    RenderDeviceContext* pDeviceContext;
    VkDescriptorSet objectDataSet;
    Mesh mesh;
    Texture colorTexture;
    Texture normalTexture;

    Buffer objectDataBuffer{};
    VkImageView colorTextureView;
    VkImageView normalTextureView;

    Transform transform{};
    float specularity = 0.5F;
};
