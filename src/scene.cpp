#include "scene.hpp"

#include <stdexcept>

#include <volk.h>

/// @brief Uniform scene data.
struct UniformSceneData
{
    __declspec(align(16)) glm::vec3 cameraPosition;
    __declspec(align(16)) glm::mat4 viewproject;
};

/// @brief Uniform light data
struct UniformLightData
{
    __declspec(align(16)) glm::vec3 lightDirection;
    __declspec(align(16)) glm::vec3 lightColor;
    __declspec(align(16)) glm::vec3 ambient;
    __declspec(align(16)) glm::mat4 lightSpaceTransform;
};

/// @brief Uniform per-object data.
struct UniformObjectData
{
    __declspec(align(16)) glm::mat4 model;
    __declspec(align(16)) glm::mat4 normal;
    __declspec(align(4))  float specularity;
};

glm::mat4 Transform::matrix() const
{
    return glm::translate(glm::identity<glm::mat4>(), position)
        * glm::mat4_cast(rotation)
        * glm::scale(glm::identity<glm::mat4>(), scale);
}

glm::mat4 PerspectiveCamera::matrix() const
{
    return glm::perspective(glm::radians(FOVy), aspectRatio, zNear, zFar);
}

glm::mat4 OrthographicCamera::matrix() const
{
    return glm::ortho(
        -0.5F * xMag, 0.5F * xMag,
        -0.5F * yMag, 0.5F * yMag,
        zNear, zFar
    );
}

glm::mat4 Camera::matrix() const
{
    switch (type)
    {
    case CameraType::Perspective:
        return perspective.matrix();
    case CameraType::Orthographic:
        return ortho.matrix();
    default:
        break;
    }

    return glm::identity<glm::mat4>();
}

Object::Object(
    RenderDeviceContext* pDeviceContext,
    VkDescriptorSet objectDataSet,
    Mesh mesh,
    VkImageView colorTextureView,
    VkImageView normalTextureView
)
    :
    pDeviceContext(pDeviceContext),
    objectDataSet(objectDataSet),
    mesh(mesh),
    colorTextureView(colorTextureView),
    normalTextureView(normalTextureView)
{
    if (!pDeviceContext->createBuffer(objectDataBuffer, sizeof(UniformObjectData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        throw std::runtime_error("VK Renderer object data buffer create failed");
    }

    VkDescriptorBufferInfo objectDataBufferInfo{};
    objectDataBufferInfo.buffer = objectDataBuffer.handle;
    objectDataBufferInfo.offset = 0;
    objectDataBufferInfo.range = objectDataBuffer.size;

    VkDescriptorImageInfo colorTextureInfo{};
    colorTextureInfo.sampler = VK_NULL_HANDLE;
    colorTextureInfo.imageView = colorTextureView;
    colorTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo normalTextureInfo{};
    normalTextureInfo.sampler = VK_NULL_HANDLE;
    normalTextureInfo.imageView = normalTextureView;
    normalTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet objectDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    objectDataWrite.dstSet = objectDataSet;
    objectDataWrite.dstBinding = 0;
    objectDataWrite.dstArrayElement = 0;
    objectDataWrite.descriptorCount = 1;
    objectDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    objectDataWrite.pBufferInfo = &objectDataBufferInfo;

    VkWriteDescriptorSet colorTextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    colorTextureWrite.dstSet = objectDataSet;
    colorTextureWrite.dstBinding = 1;
    colorTextureWrite.dstArrayElement = 0;
    colorTextureWrite.descriptorCount = 1;
    colorTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorTextureWrite.pImageInfo = &colorTextureInfo;

    VkWriteDescriptorSet normalTextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    normalTextureWrite.dstSet = objectDataSet;
    normalTextureWrite.dstBinding = 2;
    normalTextureWrite.dstArrayElement = 0;
    normalTextureWrite.descriptorCount = 1;
    normalTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalTextureWrite.pImageInfo = &normalTextureInfo;

    VkWriteDescriptorSet objectDescriptorWrites[] = { objectDataWrite, colorTextureWrite, normalTextureWrite, };
    vkUpdateDescriptorSets(pDeviceContext->device, SIZEOF_ARRAY(objectDescriptorWrites), objectDescriptorWrites, 0, nullptr);
}

void Object::destroy()
{
    objectDataBuffer.destroy();
}

void Object::update()
{
    objectDataBuffer.map();

    UniformObjectData objectData{};
    objectData.model = transform.matrix();
    objectData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(objectData.model))));
    objectData.specularity = specularity;

    assert(objectDataBuffer.mapped);
    memcpy(objectDataBuffer.pData, &objectData, sizeof(UniformObjectData));
    objectDataBuffer.unmap();
}

Scene::Scene(RenderDeviceContext* pDeviceContext, VkDescriptorSet sceneDataSet, VkDescriptorSet lightDataSet)
    :
    pDeviceContext(pDeviceContext),
    sceneDataSet(sceneDataSet),
    lightDataSet(lightDataSet)
{
    // Set up object descriptor pool
    {
        VkDescriptorPoolSize objectPoolSizes[] = {
                   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * MaxSceneObjects },
                   VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxSceneObjects },
        };

        VkDescriptorPoolCreateInfo objectDescriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        objectDescriptorPoolCreateInfo.flags = 0;
        objectDescriptorPoolCreateInfo.maxSets = MaxSceneObjects;
        objectDescriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(objectPoolSizes);
        objectDescriptorPoolCreateInfo.pPoolSizes = objectPoolSizes;

        if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &objectDescriptorPoolCreateInfo, nullptr, &objectDescriptorPool)))
        {
            throw std::runtime_error("Vulkan object descriptor pool create failed");
        }
    }

    // Set up scene uniform buffer
    {
        if (!pDeviceContext->createBuffer(sceneDataBuffer, sizeof(UniformSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            throw std::runtime_error("Vulkan scene data buffer create failed");
        }

        VkDescriptorBufferInfo sceneDataBufferInfo{};
        sceneDataBufferInfo.buffer = sceneDataBuffer.handle;
        sceneDataBufferInfo.offset = 0;
        sceneDataBufferInfo.range = sceneDataBuffer.size;

        VkWriteDescriptorSet sceneDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        sceneDataWrite.dstSet = sceneDataSet;
        sceneDataWrite.dstBinding = 0;
        sceneDataWrite.dstArrayElement = 0;
        sceneDataWrite.descriptorCount = 1;
        sceneDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sceneDataWrite.pBufferInfo = &sceneDataBufferInfo;

        vkUpdateDescriptorSets(pDeviceContext->device, 1, &sceneDataWrite, 0, nullptr);
    }

    // Set up light uniform buffer
    {
        if (!pDeviceContext->createBuffer(lightDataBuffer, sizeof(UniformLightData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            throw std::runtime_error("Vulkan light data buffer create failed");
        }

        VkDescriptorBufferInfo lightDataBufferInfo{};
        lightDataBufferInfo.buffer = lightDataBuffer.handle;
        lightDataBufferInfo.offset = 0;
        lightDataBufferInfo.range = lightDataBuffer.size;

        VkWriteDescriptorSet lightDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        lightDataWrite.dstSet = lightDataSet;
        lightDataWrite.dstBinding = 0;
        lightDataWrite.dstArrayElement = 0;
        lightDataWrite.descriptorCount = 1;
        lightDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightDataWrite.pBufferInfo = &lightDataBufferInfo;

        vkUpdateDescriptorSets(pDeviceContext->device, 1, &lightDataWrite, 0, nullptr);
    }
}

void Scene::destroy()
{
    for (auto& object : objects) {
        object.destroy();
    }

    for (auto& view : textureViews) {
        vkDestroyImageView(pDeviceContext->device, view, nullptr);
    }

    for (auto& texture : textures) {
        texture.destroy();
    }

    for (auto& mesh : meshes) {
        mesh.destroy();
    }

    lightDataBuffer.destroy();
    sceneDataBuffer.destroy();
    vkDestroyDescriptorPool(pDeviceContext->device, objectDescriptorPool, nullptr);
}

void Scene::update()
{
    sceneDataBuffer.map();
    glm::vec3 const camForward = glm::normalize(glm::vec3(cameraTransform.matrix() * glm::vec4(FORWARD, 0.0F)));
    glm::vec3 const camUp = glm::normalize(glm::vec3(cameraTransform.matrix() * glm::vec4(UP, 0.0F)));

    UniformSceneData sceneData{};
    sceneData.cameraPosition = cameraTransform.position;
    sceneData.viewproject = camera.matrix() * glm::lookAt(cameraTransform.position, cameraTransform.position + camForward, camUp);

    assert(sceneDataBuffer.mapped);
    memcpy(sceneDataBuffer.pData, &sceneData, sizeof(UniformSceneData));
    sceneDataBuffer.unmap();

    lightDataBuffer.map();
    Camera lightCamera{};
    lightCamera.type = CameraType::Orthographic;
    lightCamera.ortho.xMag = shadowmapXMag;
    lightCamera.ortho.yMag = shadowmapYMag;
    lightCamera.ortho.zNear = 0.01F;
    lightCamera.ortho.zFar = shadowmapDepth;

    UniformLightData lightData{};
    lightData.lightDirection = glm::normalize(glm::vec3{
        glm::cos(glm::radians(sunAzimuth)) * glm::sin(glm::radians(90.0F - sunZenith)),
        glm::cos(glm::radians(90.0F - sunZenith)),
        glm::sin(glm::radians(sunAzimuth)) * glm::sin(glm::radians(90.0F - sunZenith)),
    });
    lightData.lightColor = sunColor;
    lightData.ambient = ambientLight;
    lightData.lightSpaceTransform = lightCamera.matrix() * glm::lookAt(cameraTransform.position, cameraTransform.position - lightData.lightDirection, UP);

    assert(lightDataBuffer.mapped);
    memcpy(lightDataBuffer.pData, &lightData, sizeof(UniformLightData));
    lightDataBuffer.unmap();

    // Update object data in scene
    for (auto& object : objects) {
        object.update();
    }
}
