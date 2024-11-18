#include "object.hpp"

#include <stdexcept>

#include <volk.h>

glm::mat4 Transform::matrix() const
{
    return glm::translate(glm::identity<glm::mat4>(), position)
        * glm::mat4_cast(rotation)
        * glm::scale(glm::identity<glm::mat4>(), scale);
}

Object::Object(
    RenderDeviceContext* pDeviceContext,
    VkDescriptorSet objectDataSet,
    Mesh mesh,
    Texture colorTexture,
    Texture normalTexture
)
    :
    pDeviceContext(pDeviceContext),
    objectDataSet(objectDataSet),
    mesh(mesh),
    colorTexture(colorTexture),
    normalTexture(normalTexture)
{
    if (!pDeviceContext->createBuffer(objectDataBuffer, sizeof(UniformObjectData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true))
    {
        throw std::runtime_error("VK Renderer object data buffer create failed");
    }

    VkImageViewCreateInfo colorTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    colorTextureViewCreateInfo.flags = 0;
    colorTextureViewCreateInfo.image = colorTexture.handle;
    colorTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorTextureViewCreateInfo.format = colorTexture.format;
    colorTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    colorTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    colorTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    colorTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    colorTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
    colorTextureViewCreateInfo.subresourceRange.levelCount = colorTexture.levels;
    colorTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    colorTextureViewCreateInfo.subresourceRange.layerCount = colorTexture.depthOrLayers;
    colorTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &colorTextureViewCreateInfo, nullptr, &colorTextureView)))
    {
        throw std::runtime_error("VK Renderer color texture view create failed");
    }

    VkImageViewCreateInfo normalTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    normalTextureViewCreateInfo.flags = 0;
    normalTextureViewCreateInfo.image = normalTexture.handle;
    normalTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    normalTextureViewCreateInfo.format = normalTexture.format;
    normalTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    normalTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    normalTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    normalTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    normalTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
    normalTextureViewCreateInfo.subresourceRange.levelCount = normalTexture.levels;
    normalTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    normalTextureViewCreateInfo.subresourceRange.layerCount = normalTexture.depthOrLayers;
    normalTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &normalTextureViewCreateInfo, nullptr, &normalTextureView)))
    {
        throw std::runtime_error("VK Renderer normal texture view create failed");
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
    vkDestroyImageView(pDeviceContext->device, normalTextureView, nullptr);
    vkDestroyImageView(pDeviceContext->device, colorTextureView, nullptr);
    objectDataBuffer.destroy();
}

void Object::update()
{
    UniformObjectData objectData{};
    objectData.model = transform.matrix();
    objectData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(objectData.model))));
    objectData.specularity = specularity;

    assert(objectDataBuffer.mapped);
    memcpy(objectDataBuffer.pData, &objectData, sizeof(UniformObjectData));
}
