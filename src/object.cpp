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
    if (!pDeviceContext->createBuffer(objectDataBuffer, sizeof(UniformObjectData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true))
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
    UniformObjectData objectData{};
    objectData.model = transform.matrix();
    objectData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(objectData.model))));
    objectData.specularity = specularity;

    assert(objectDataBuffer.mapped);
    memcpy(objectDataBuffer.pData, &objectData, sizeof(UniformObjectData));
}
