#define _CRT_SECURE_NO_WARNINGS //< Used to silence C file IO function warnings

#include "assets.hpp"

#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <volk.h>

bool readShaderFile(char const* path, std::vector<uint32_t>& shaderCode)
{
    FILE* pFile = fopen(path, "rb");
    if (pFile == nullptr)
    {
        printf("VK Renderer failed to open file [%s]\n", path);
        return false;
    }

    fseek(pFile, 0, SEEK_END);
    size_t codeSize = ftell(pFile);

    assert(codeSize % 4 == 0);
    shaderCode.resize(codeSize / 4, 0);

    fseek(pFile, 0, SEEK_SET);
    fread(shaderCode.data(), sizeof(uint32_t), shaderCode.size(), pFile);

    fclose(pFile);
    return true;
}

bool loadOBJ(RenderDeviceContext* pDeviceContext, char const* path, Mesh& mesh)
{
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;

    config.triangulate = true;
    config.triangulation_method = "earcut";
    config.vertex_color = true;

    if (!reader.ParseFromFile(path))
    {
        printf("TinyOBJ OBJ load failed [%s]\n", path);
        return false;
    }
    printf("Loaded OBJ mesh [%s]\n", path);

    auto const& attrib = reader.GetAttrib();
    auto const& shapes = reader.GetShapes();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (auto const& shape : shapes)
    {
        vertices.reserve(vertices.size() + shape.mesh.indices.size());
        indices.reserve(indices.size() + shape.mesh.indices.size());

        for (auto const& index : shape.mesh.indices)
        {
            size_t vertexIdx = index.vertex_index * 3;
            size_t normalIdx = index.normal_index * 3;
            size_t texIdx = index.texcoord_index * 2;

            vertices.push_back(Vertex{
                { attrib.vertices[vertexIdx + 0], attrib.vertices[vertexIdx + 1], attrib.vertices[vertexIdx + 2] },
                { attrib.colors[vertexIdx + 0], attrib.colors[vertexIdx + 1], attrib.colors[vertexIdx + 2] },
                { attrib.normals[normalIdx + 0], attrib.normals[normalIdx + 1], attrib.normals[normalIdx + 2] },
                { 0.0F, 0.0F, 0.0F }, //< tangents are calculated after loading
                { attrib.texcoords[texIdx + 0], attrib.texcoords[texIdx + 1] },
                });
            indices.push_back(static_cast<uint32_t>(indices.size())); //< works because mesh is triangulated
        }
    }

    // calculate tangents based on position & texture coords
    assert(indices.size() % 3 == 0); //< Need multiple of 3 for triangle indices
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        Vertex& v0 = vertices[indices[i + 0]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        glm::vec3 const e1 = v1.position - v0.position;
        glm::vec3 const e2 = v2.position - v0.position;
        glm::vec2 const dUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 const dUV2 = v2.texCoord - v0.texCoord;

        float const f = 1.0F / (dUV1.x * dUV2.y - dUV1.y * dUV2.x);
        glm::vec3 const tangent = f * (dUV2.y * e1 - dUV1.y * e2);

        v0.tangent = tangent;
        v1.tangent = tangent;
        v2.tangent = tangent;
    }

    return createMesh(pDeviceContext, mesh, vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
}

bool loadTexture(RenderDeviceContext* pDeviceContext, char const* path, Texture& texture)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pImageData = stbi_load(path, &width, &height, &channels, 4);
    if (pImageData == nullptr)
    {
        printf("STBI image load failed [%s]\n", path);
        return false;
    }
    printf("Loaded texture [%s] (%d x %d x %d)\n", path, width, height, channels);

    // Load all images as 4 channel color targets
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t mipLevels = static_cast<uint32_t>(std::log2(std::max(width, height))) + 1;
    channels = 4;

    printf("Texture has %d mip levels\n", mipLevels);

    Buffer uploadBuffer{};
    if (!pDeviceContext->createBuffer(
        uploadBuffer,
        width * height * channels,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true
    )) {
        stbi_image_free(pImageData);
        return false;
    }

    assert(uploadBuffer.mapped && uploadBuffer.pData != nullptr);
    memcpy(uploadBuffer.pData, pImageData, uploadBuffer.size);

    if (!pDeviceContext->createTexture(
        texture,
        VK_IMAGE_TYPE_2D,
        imageFormat,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1,
        mipLevels
    )) {
        uploadBuffer.destroy();
        stbi_image_free(pImageData);
        return false;
    }

    // Schedule upload using transient upload buffer
    {
        VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
        if (!pDeviceContext->createCommandBuffer(CommandQueueType::Copy, &uploadCommandBuffer))
        {
            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return false;
        }

        VkCommandBufferBeginInfo uploadBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        uploadBeginInfo.pInheritanceInfo = nullptr;

        if (VK_FAILED(vkBeginCommandBuffer(uploadCommandBuffer, &uploadBeginInfo)))
        {
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return false;
        }

        VkImageMemoryBarrier transferBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        transferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        transferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferBarrier.image = texture.handle;
        transferBarrier.subresourceRange.baseMipLevel = 0;
        transferBarrier.subresourceRange.levelCount = texture.levels;
        transferBarrier.subresourceRange.baseArrayLayer = 0;
        transferBarrier.subresourceRange.layerCount = texture.depthOrLayers;
        transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdPipelineBarrier(uploadCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &transferBarrier
        );

        VkBufferImageCopy imageCopy{};
        imageCopy.bufferOffset = 0;
        imageCopy.bufferRowLength = width;
        imageCopy.bufferImageHeight = height;
        imageCopy.imageSubresource.mipLevel = 0;
        imageCopy.imageSubresource.baseArrayLayer = 0;
        imageCopy.imageSubresource.layerCount = 1;
        imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.imageOffset = VkOffset3D{ 0, 0, 0 };
        imageCopy.imageExtent = VkExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

        vkCmdCopyBufferToImage(
            uploadCommandBuffer,
            uploadBuffer.handle,
            texture.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &imageCopy
        );

        // XXX: This mip calulcation only works for textures that are powers of 2
        int32_t srcWidth = static_cast<int32_t>(texture.width);
        int32_t srcHeight = static_cast<int32_t>(texture.height);
        for (uint32_t level = 0; level < texture.levels - 1; level++)
        {
            int32_t dstWidth = srcWidth / 2;
            int32_t dstHeight = srcHeight / 2;

            VkImageMemoryBarrier srcMipBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            srcMipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcMipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcMipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            srcMipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcMipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcMipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcMipBarrier.image = texture.handle;
            srcMipBarrier.subresourceRange.baseMipLevel = level;
            srcMipBarrier.subresourceRange.levelCount = 1;
            srcMipBarrier.subresourceRange.baseArrayLayer = 0;
            srcMipBarrier.subresourceRange.layerCount = texture.depthOrLayers;
            srcMipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            VkImageMemoryBarrier dstMipBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            dstMipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstMipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstMipBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dstMipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstMipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstMipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstMipBarrier.image = texture.handle;
            dstMipBarrier.subresourceRange.baseMipLevel = level + 1;
            dstMipBarrier.subresourceRange.levelCount = 1;
            dstMipBarrier.subresourceRange.baseArrayLayer = 0;
            dstMipBarrier.subresourceRange.layerCount = texture.depthOrLayers;
            dstMipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            VkImageMemoryBarrier mipBarriers[] = { srcMipBarrier, dstMipBarrier, };
            vkCmdPipelineBarrier(uploadCommandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                SIZEOF_ARRAY(mipBarriers), mipBarriers
            );

            VkImageBlit blitRegion{};
            blitRegion.srcOffsets[0] = VkOffset3D{ 0, 0, 0, };
            blitRegion.srcOffsets[1] = VkOffset3D{ srcWidth, srcHeight, 1, };
            blitRegion.srcSubresource.baseArrayLayer = 0;
            blitRegion.srcSubresource.layerCount = 1;
            blitRegion.srcSubresource.mipLevel = level;
            blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.dstOffsets[0] = VkOffset3D{ 0, 0, 0, };
            blitRegion.dstOffsets[1] = VkOffset3D{ dstWidth, dstHeight, 1, };
            blitRegion.dstSubresource.baseArrayLayer = 0;
            blitRegion.dstSubresource.layerCount = 1;
            blitRegion.dstSubresource.mipLevel = level + 1;
            blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkCmdBlitImage(
                uploadCommandBuffer,
                texture.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blitRegion,
                VK_FILTER_LINEAR
            );

            srcWidth = dstWidth;
            srcHeight = dstHeight;
        }

        VkImageMemoryBarrier shaderBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.image = texture.handle;
        shaderBarrier.subresourceRange.baseMipLevel = 0;
        shaderBarrier.subresourceRange.levelCount = texture.levels;
        shaderBarrier.subresourceRange.baseArrayLayer = 0;
        shaderBarrier.subresourceRange.layerCount = texture.depthOrLayers;
        shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdPipelineBarrier(uploadCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &shaderBarrier
        );

        if (VK_FAILED(vkEndCommandBuffer(uploadCommandBuffer)))
        {
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return false;
        }

        VkFence uploadFence = VK_NULL_HANDLE;
        if (!pDeviceContext->createFence(&uploadFence, false))
        {
            pDeviceContext->destroyFence(uploadFence);
            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return false;
        }

        VkSubmitInfo uploadSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        uploadSubmit.waitSemaphoreCount = 0;
        uploadSubmit.pWaitSemaphores = nullptr;
        uploadSubmit.pWaitDstStageMask = nullptr;
        uploadSubmit.commandBufferCount = 1;
        uploadSubmit.pCommandBuffers = &uploadCommandBuffer;
        uploadSubmit.signalSemaphoreCount = 0;
        uploadSubmit.pSignalSemaphores = nullptr;

        if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &uploadSubmit, uploadFence)))
        {
            vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return false;
        }

        vkWaitForFences(pDeviceContext->device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
        pDeviceContext->destroyFence(uploadFence);
        pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
    }

    uploadBuffer.destroy();
    stbi_image_free(pImageData);
    return true;
}
