#define _CRT_SECURE_NO_WARNINGS //< Used to silence C file IO function warnings

#include "assets.hpp"

#include <cstdio>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <volk.h>

#include "mesh.hpp"
#include "render_backend.hpp"
#include "render_backend/buffer.hpp"
#include "render_backend/texture.hpp"
#include "render_backend/utils.hpp"
#include "scene.hpp"
#include "transform.hpp"

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

std::shared_ptr<Mesh> createMesh(RenderDeviceContext* pDeviceContext, Vertex const* vertices, uint32_t vertexCount, uint32_t const* indices, uint32_t indexCount)
{
    assert(vertices != nullptr);
    assert(indices != nullptr);
    assert(vertexCount > 0);
    assert(indexCount > 0);

    uint32_t const vertexBufferSize = sizeof(Vertex) * vertexCount;
    uint32_t const indexBufferSize = sizeof(uint32_t) * indexCount;

    std::shared_ptr<Buffer> vertexUploadBuffer = pDeviceContext->createBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    std::shared_ptr<Buffer> indexUploadBuffer = pDeviceContext->createBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    if (vertexUploadBuffer == nullptr || indexUploadBuffer == nullptr) {
        return nullptr;
    }

    std::shared_ptr<Buffer> vertexBuffer = pDeviceContext->createBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    std::shared_ptr<Buffer> indexBuffer = pDeviceContext->createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vertexBuffer == nullptr || indexBuffer == nullptr) {
        return nullptr;
    }

    assert(vertexUploadBuffer->mapped() && indexUploadBuffer->mapped());
    memcpy(vertexUploadBuffer->data(), vertices, vertexBufferSize);
    memcpy(indexUploadBuffer->data(), indices, indexBufferSize);

    // Schedule upload using transient upload buffer
    {
        CommandContext uploadCommandContext{};
        if (!pDeviceContext->createCommandContext(CommandQueueType::Copy, uploadCommandContext)) {
            return nullptr;
        }

        VkCommandBufferBeginInfo uploadBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        uploadBeginInfo.pInheritanceInfo = nullptr;

        if (VK_FAILED(vkBeginCommandBuffer(uploadCommandContext.handle, &uploadBeginInfo)))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return nullptr;
        }

        VkBufferCopy vertexCopy{};
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = 0;
        vertexCopy.size = vertexBufferSize;

        VkBufferCopy indexCopy{};
        indexCopy.srcOffset = 0;
        indexCopy.dstOffset = 0;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(uploadCommandContext.handle, vertexUploadBuffer->handle(), vertexBuffer->handle(), 1, &vertexCopy);
        vkCmdCopyBuffer(uploadCommandContext.handle, indexUploadBuffer->handle(), indexBuffer->handle(), 1, &indexCopy);

        if (VK_FAILED(vkEndCommandBuffer(uploadCommandContext.handle)))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return nullptr;
        }

        VkFence uploadFence = VK_NULL_HANDLE;
        if (!pDeviceContext->createFence(&uploadFence, false))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return nullptr;
        }

        VkSubmitInfo uploadSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        uploadSubmit.waitSemaphoreCount = 0;
        uploadSubmit.pWaitSemaphores = nullptr;
        uploadSubmit.pWaitDstStageMask = nullptr;
        uploadSubmit.commandBufferCount = 1;
        uploadSubmit.pCommandBuffers = &uploadCommandContext.handle;
        uploadSubmit.signalSemaphoreCount = 0;
        uploadSubmit.pSignalSemaphores = nullptr;

        if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &uploadSubmit, uploadFence)))
        {
            pDeviceContext->destroyFence(uploadFence);
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return nullptr;
        }

        vkWaitForFences(pDeviceContext->device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
        pDeviceContext->destroyFence(uploadFence);
        pDeviceContext->destroyCommandContext(uploadCommandContext);
    }

    return std::make_shared<Mesh>(vertexCount, indexCount, vertexBuffer, indexBuffer);
}

std::shared_ptr<Mesh> loadOBJ(RenderDeviceContext* pDeviceContext, char const* path)
{
    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig config;

    config.triangulate = true;
    config.triangulation_method = "earcut";
    config.vertex_color = true;

    if (!reader.ParseFromFile(path))
    {
        printf("TinyOBJ OBJ load failed [%s]\n", path);
        return nullptr;
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

    return createMesh(pDeviceContext, vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
}

std::shared_ptr<Texture> loadTexture(RenderDeviceContext* pDeviceContext, char const* path)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pImageData = stbi_load(path, &width, &height, &channels, 4);
    if (pImageData == nullptr)
    {
        printf("STBI image load failed [%s]\n", path);
        return false;
    }

    // Load all images as 4 channel color targets
    printf("Loaded texture [%s] (%d x %d x %d, %d mips)\n", path, width, height, channels, Texture::calculateMipLevels(width, height));
    std::shared_ptr<Texture> texture = pDeviceContext->createTexture(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, Texture::calculateMipLevels(width, height)
    );

    if (texture == nullptr || !uploadToTexture(pDeviceContext, texture, pImageData, width * height * 4)) {
        stbi_image_free(pImageData);
        return nullptr;
    }

    stbi_image_free(pImageData);
    if (!generateMipMaps(pDeviceContext, texture)) {
        return nullptr;
    }

    return texture;
}

std::shared_ptr<Texture> loadTextureFromMemory(RenderDeviceContext* pDeviceContext, void* pData, size_t size)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pImageData = stbi_load_from_memory(reinterpret_cast<stbi_uc*>(pData), static_cast<int>(size), &width, &height, &channels, 4);
    if (pImageData == nullptr)
    {
        printf("STBI image load from memory failed\n");
        return false;
    }

    printf("Loaded texture from memory (%d x %d x %d, %d mips)\n", width, height, channels, Texture::calculateMipLevels(width, height));
    std::shared_ptr<Texture> texture = pDeviceContext->createTexture(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, Texture::calculateMipLevels(width, height)
    );

    if (texture == nullptr || !uploadToTexture(pDeviceContext, texture, pImageData, width * height * 4))
    {
        stbi_image_free(pImageData);
        return nullptr;
    }

    stbi_image_free(pImageData);
    if (!generateMipMaps(pDeviceContext, texture)) {
        return nullptr;
    }

    return texture;
}

std::shared_ptr<Texture> loadCubeMap(RenderDeviceContext* pDeviceContext, std::array<std::string, 6> const& faces)
{
    int texWidth = 0, texHeight = 0;
    std::array<int, 6> widths;
    std::array<int, 6> heights;
    std::array<stbi_uc*, 6> images;
    for (size_t i = 0; i < 6; i++)
    {
        int width = 0, height = 0, channels = 0;
        images[i] = stbi_load(faces[i].c_str(), &width, &height, &channels, 4);
        widths[i] = width;
        heights[i] = height;

        texWidth = std::max(texWidth, width);
        texHeight = std::max(texHeight, height);
        printf("Loaded cubemap face [%s]\n", faces[i].c_str());
    }

    printf("Loaded cubemap (%d x %d)\n", texWidth, texHeight);
    std::shared_ptr<Texture> texture = pDeviceContext->createTexture(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texWidth, texHeight, 1, 1, 6
    );

    if (texture == nullptr) {
        return nullptr;
    }

    uint32_t layerIdx = 0;
    for (size_t i = 0; i < 6; i++) {
        uploadToTexture(pDeviceContext, texture, images[i], widths[i] * heights[i] * 4, layerIdx++);
        stbi_image_free(images[i]);
    }

    return texture;
}

bool uploadToTexture(RenderDeviceContext* pDeviceContext, std::shared_ptr<Texture> texture, void* pData, size_t size, uint32_t layer)
{
    std::shared_ptr<Buffer> uploadBuffer = pDeviceContext->createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    if (uploadBuffer == nullptr) {
        return false;
    }

    assert(uploadBuffer->mapped() && uploadBuffer->data() != nullptr);
    memcpy(uploadBuffer->data(), pData, uploadBuffer->size());

    // Schedule upload using transient upload buffer
    {
        CommandContext uploadCommandContext{};
        if (!pDeviceContext->createCommandContext(CommandQueueType::Copy, uploadCommandContext))
        {
            return false;
        }

        VkCommandBufferBeginInfo uploadBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        uploadBeginInfo.pInheritanceInfo = nullptr;

        if (VK_FAILED(vkBeginCommandBuffer(uploadCommandContext.handle, &uploadBeginInfo)))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return false;
        }

        VkImageMemoryBarrier transferBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        transferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        transferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        transferBarrier.image = texture->handle;
        transferBarrier.subresourceRange.baseMipLevel = 0;
        transferBarrier.subresourceRange.levelCount = texture->levels;
        transferBarrier.subresourceRange.baseArrayLayer = layer;
        transferBarrier.subresourceRange.layerCount = 1;
        transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdPipelineBarrier(uploadCommandContext.handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &transferBarrier
        );

        VkBufferImageCopy imageCopy{};
        imageCopy.bufferOffset = 0;
        imageCopy.bufferRowLength = texture->size.width;
        imageCopy.bufferImageHeight = texture->size.height;
        imageCopy.imageSubresource.mipLevel = 0;
        imageCopy.imageSubresource.baseArrayLayer = layer;
        imageCopy.imageSubresource.layerCount = 1;
        imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.imageOffset = VkOffset3D{ 0, 0, 0 };
        imageCopy.imageExtent = VkExtent3D{ static_cast<uint32_t>(texture->size.width), static_cast<uint32_t>(texture->size.height), 1 };

        vkCmdCopyBufferToImage(
            uploadCommandContext.handle,
            uploadBuffer->handle(),
            texture->handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &imageCopy
        );

        VkImageMemoryBarrier shaderBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.image = texture->handle;
        shaderBarrier.subresourceRange.baseMipLevel = 0;
        shaderBarrier.subresourceRange.levelCount = texture->levels;
        shaderBarrier.subresourceRange.baseArrayLayer = layer;
        shaderBarrier.subresourceRange.layerCount = 1;
        shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdPipelineBarrier(uploadCommandContext.handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &shaderBarrier
        );

        if (VK_FAILED(vkEndCommandBuffer(uploadCommandContext.handle)))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return false;
        }

        VkFence uploadFence = VK_NULL_HANDLE;
        if (!pDeviceContext->createFence(&uploadFence, false))
        {
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return false;
        }

        VkSubmitInfo uploadSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        uploadSubmit.waitSemaphoreCount = 0;
        uploadSubmit.pWaitSemaphores = nullptr;
        uploadSubmit.pWaitDstStageMask = nullptr;
        uploadSubmit.commandBufferCount = 1;
        uploadSubmit.pCommandBuffers = &uploadCommandContext.handle;
        uploadSubmit.signalSemaphoreCount = 0;
        uploadSubmit.pSignalSemaphores = nullptr;

        if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &uploadSubmit, uploadFence)))
        {
            pDeviceContext->destroyFence(uploadFence);
            pDeviceContext->destroyCommandContext(uploadCommandContext);
            return false;
        }

        vkWaitForFences(pDeviceContext->device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
        pDeviceContext->destroyFence(uploadFence);
        pDeviceContext->destroyCommandContext(uploadCommandContext);
    }

    return true;
}

bool generateMipMaps(RenderDeviceContext* pDeviceContext, std::shared_ptr<Texture> texture, uint32_t layer)
{
    assert(layer < texture->size.depthOrLayers);

    CommandContext mipmapCommandContext{};
    if (!pDeviceContext->createCommandContext(CommandQueueType::Copy, mipmapCommandContext))
    {
        return false;
    }

    VkCommandBufferBeginInfo mipmapBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    mipmapBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    mipmapBeginInfo.pInheritanceInfo = nullptr;

    if (VK_FAILED(vkBeginCommandBuffer(mipmapCommandContext.handle, &mipmapBeginInfo)))
    {
        pDeviceContext->destroyCommandContext(mipmapCommandContext);
        return false;
    }

    // XXX: This mip calculation only works for textures that are powers of 2
    int32_t srcWidth = static_cast<int32_t>(texture->size.width);
    int32_t srcHeight = static_cast<int32_t>(texture->size.height);
    for (uint32_t level = 0; level < texture->levels - 1; level++)
    {
        int32_t dstWidth = srcWidth / 2;
        int32_t dstHeight = srcHeight / 2;

        VkImageMemoryBarrier srcMipBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        srcMipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcMipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcMipBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcMipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcMipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcMipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcMipBarrier.image = texture->handle;
        srcMipBarrier.subresourceRange.baseMipLevel = level;
        srcMipBarrier.subresourceRange.levelCount = 1;
        srcMipBarrier.subresourceRange.baseArrayLayer = layer;
        srcMipBarrier.subresourceRange.layerCount = 1;
        srcMipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageMemoryBarrier dstMipBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        dstMipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dstMipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstMipBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dstMipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstMipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstMipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstMipBarrier.image = texture->handle;
        dstMipBarrier.subresourceRange.baseMipLevel = level + 1;
        dstMipBarrier.subresourceRange.levelCount = 1;
        dstMipBarrier.subresourceRange.baseArrayLayer = layer;
        dstMipBarrier.subresourceRange.layerCount = 1;
        dstMipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageMemoryBarrier mipBarriers[] = { srcMipBarrier, dstMipBarrier, };
        vkCmdPipelineBarrier(mipmapCommandContext.handle,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            static_cast<uint32_t>(std::size(mipBarriers)), mipBarriers
        );

        VkImageBlit blitRegion{};
        blitRegion.srcOffsets[0] = VkOffset3D{ 0, 0, 0, };
        blitRegion.srcOffsets[1] = VkOffset3D{ srcWidth, srcHeight, 1, };
        blitRegion.srcSubresource.baseArrayLayer = layer;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcSubresource.mipLevel = level;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstOffsets[0] = VkOffset3D{ 0, 0, 0, };
        blitRegion.dstOffsets[1] = VkOffset3D{ dstWidth, dstHeight, 1, };
        blitRegion.dstSubresource.baseArrayLayer = layer;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstSubresource.mipLevel = level + 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdBlitImage(
            mipmapCommandContext.handle,
            texture->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
    shaderBarrier.image = texture->handle;
    shaderBarrier.subresourceRange.baseMipLevel = 0;
    shaderBarrier.subresourceRange.levelCount = texture->levels;
    shaderBarrier.subresourceRange.baseArrayLayer = 0;
    shaderBarrier.subresourceRange.layerCount = texture->size.depthOrLayers;
    shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    vkCmdPipelineBarrier(mipmapCommandContext.handle,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &shaderBarrier
    );

    if (VK_FAILED(vkEndCommandBuffer(mipmapCommandContext.handle)))
    {
        pDeviceContext->destroyCommandContext(mipmapCommandContext);
        return false;
    }

    VkFence mipmapFence = VK_NULL_HANDLE;
    if (!pDeviceContext->createFence(&mipmapFence, false))
    {
        pDeviceContext->destroyCommandContext(mipmapCommandContext);
        return false;
    }

    VkSubmitInfo mipmapSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    mipmapSubmit.waitSemaphoreCount = 0;
    mipmapSubmit.pWaitSemaphores = nullptr;
    mipmapSubmit.pWaitDstStageMask = nullptr;
    mipmapSubmit.commandBufferCount = 1;
    mipmapSubmit.pCommandBuffers = &mipmapCommandContext.handle;
    mipmapSubmit.signalSemaphoreCount = 0;
    mipmapSubmit.pSignalSemaphores = nullptr;

    if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &mipmapSubmit, mipmapFence)))
    {
        pDeviceContext->destroyFence(mipmapFence);
        pDeviceContext->destroyCommandContext(mipmapCommandContext);
        return false;
    }

    vkWaitForFences(pDeviceContext->device, 1, &mipmapFence, VK_TRUE, UINT64_MAX);
    pDeviceContext->destroyFence(mipmapFence);
    pDeviceContext->destroyCommandContext(mipmapCommandContext);
    return true;
}

// Helper function for recursive node tree walking
void sceneTraverseChildren(
    Scene& scene,
    aiScene const* pImportedScene,
    SceneRef const& parent,
    aiNode const* pNode,
    SceneRef const& baseMeshRef,
    SceneRef const& baseMaterialRef
)
{
    assert(pImportedScene != nullptr);
    assert(pNode != nullptr);

    // Get assimp transform in usable format
    aiVector3D nodePosition, nodeScale, nodeRotation;
    pNode->mTransformation.Decompose(nodeScale, nodeRotation, nodePosition);

    // Get assimp mesh/material data
    uint32_t nodeMesh = pNode->mNumMeshes > 0 ? pNode->mMeshes[0] : UINT32_MAX;
    uint32_t nodeMaterial = UINT32_MAX;
    if (nodeMesh != UINT32_MAX) {
        nodeMaterial = pImportedScene->mMeshes[nodeMesh]->mMaterialIndex;
    }

    // Get "our" transform representation
    glm::vec3 const position(nodePosition.x, nodePosition.y, nodePosition.z);
    glm::vec3 const euler(nodeRotation.x, nodeRotation.y, nodeRotation.z);
    glm::vec3 const scale(nodeScale.x, nodeScale.y, nodeScale.z);

    // Fill out node data
    char const* pNodeName = pNode->mName.C_Str();
    Transform const transform{ position, glm::quat(euler), scale };
    SceneRef const meshRef = nodeMesh != UINT32_MAX ? static_cast<SceneRef>(nodeMesh) : RefUnused;
    SceneRef const materialRef = nodeMaterial != UINT32_MAX ? static_cast<SceneRef>(nodeMaterial) : RefUnused;

    SceneRef const node = (pNode == pImportedScene->mRootNode) ? scene.createRootNode(pNodeName, transform) : scene.createChildNode(parent, pNodeName, transform);
    scene.nodes.meshRef[node] = meshRef != RefUnused ? baseMeshRef + meshRef : RefUnused;
    scene.nodes.materialRef[node] = materialRef != RefUnused ? baseMaterialRef + materialRef : RefUnused;

    for (uint32_t i = 0; i < pNode->mNumChildren; i++) {
        sceneTraverseChildren(scene, pImportedScene, node, pNode->mChildren[i], baseMeshRef, baseMaterialRef);
    }
}

bool loadScene(RenderDeviceContext* pDeviceContext, char const* path, Scene& scene)
{
    uint32_t const importflags = aiProcess_Triangulate
        | aiProcess_GenSmoothNormals
        | aiProcess_CalcTangentSpace
        | aiProcess_GenUVCoords
        | aiProcess_FlipUVs
        | aiProcess_EmbedTextures;

    Assimp::Importer importer;
    aiScene const* pImportedScene = importer.ReadFile(path, importflags);
    if (pImportedScene == nullptr)
    {
        printf("Scene import failed: %s\n", importer.GetErrorString());
        return false;
    }

    SceneRef const baseMeshRef = static_cast<SceneRef>(scene.meshes.size());
    SceneRef const baseTextureRef = static_cast<SceneRef>(scene.textures.size());
    SceneRef const baseMaterialRef = static_cast<SceneRef>(scene.materials.size());

    printf("Scene [%s]:\n", pImportedScene->GetShortFilename(path));
    printf("- Animations: %u\n", pImportedScene->mNumAnimations); //< NYI in scene
    printf("- Cameras:    %u\n", pImportedScene->mNumCameras); //< TODO
    printf("- Lights:     %u\n", pImportedScene->mNumLights); //< NYI in scene
    printf("- Materials:  %u\n", pImportedScene->mNumMaterials);
    printf("- Meshes:     %u\n", pImportedScene->mNumMeshes);
    printf("- Skeletons:  %u\n", pImportedScene->mNumSkeletons); //< NYI in scene
    printf("- Textures:   %u\n", pImportedScene->mNumTextures); //< TODO

    // TODO(nemjit001): Load all cameras in scene

    // Load all materials in scene
    for (uint32_t i = 0; i < pImportedScene->mNumMaterials; i++)
    {
        aiMaterial const* pImportedMaterial = pImportedScene->mMaterials[i];

        // Load material values
        aiColor3D albedo;
        aiColor3D specular;
        if (pImportedMaterial->Get("$clr.diffuse", aiPTI_Float, 0, albedo) != aiReturn_SUCCESS) {
            albedo = aiColor3D(1.0F, 0.0F, 0.0F);
        }

        if (pImportedMaterial->Get("$clr.specular", aiPTI_Float, 0, specular) != aiReturn_SUCCESS) {
            specular = aiColor3D(0.5F, 0.5F, 0.5F);
        }

        // Load texture indices (if they exist)
        SceneRef albedoRef = RefUnused;
        SceneRef specularRef = RefUnused;
        SceneRef normalRef = RefUnused;

        aiString diffuseTextureFile;
        if (pImportedMaterial->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), diffuseTextureFile) == aiReturn_SUCCESS) {
            auto texAndIdx = pImportedScene->GetEmbeddedTextureAndIndex(diffuseTextureFile.C_Str());
            albedoRef = static_cast<SceneRef>(texAndIdx.second);
        }

        aiString specularTextureFile;
        if (pImportedMaterial->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), specularTextureFile) == aiReturn_SUCCESS) {
            auto texAndIdx = pImportedScene->GetEmbeddedTextureAndIndex(specularTextureFile.C_Str());
            specularRef = static_cast<SceneRef>(texAndIdx.second);
        }

        aiString normalTextureFile;
        if (pImportedMaterial->Get(AI_MATKEY_TEXTURE(aiTextureType_DISPLACEMENT, 0), normalTextureFile) == aiReturn_SUCCESS) {
            auto texAndIdx = pImportedScene->GetEmbeddedTextureAndIndex(normalTextureFile.C_Str());
            normalRef = static_cast<SceneRef>(texAndIdx.second);
        }

        Material material{};
        material.defaultAlbedo = glm::vec3(albedo.r, albedo.g, albedo.b);
        material.defaultSpecular = glm::vec3(specular.r, specular.g, specular.b);
        material.albedoTexture = albedoRef != RefUnused ? baseTextureRef + albedoRef : RefUnused;
        material.specularTexture = specularRef != RefUnused ? baseTextureRef + specularRef : RefUnused;
        material.normalTexture = normalRef != RefUnused ? baseTextureRef + normalRef : RefUnused;

        scene.addMaterial(material);
    }

    // Load all meshes in scene
    for (uint32_t i = 0; i < pImportedScene->mNumMeshes; i++)
    {
        aiMesh const* pImportedMesh = pImportedScene->mMeshes[i];
        assert(pImportedMesh->mVertices != nullptr);
        assert(pImportedMesh->mNormals != nullptr);
        assert(pImportedMesh->mTangents != nullptr);
        assert(pImportedMesh->mTextureCoords[0] != nullptr);

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(pImportedMesh->mNumVertices);
        indices.reserve(pImportedMesh->mNumFaces * 3);

        // Load vertex data
        for (uint32_t vertIdx = 0; vertIdx < pImportedMesh->mNumVertices; vertIdx++)
        {
            aiVector3D const& position = pImportedMesh->mVertices[vertIdx];
            aiColor4D const& color = pImportedMesh->mColors[0] != nullptr ? pImportedMesh->mColors[0][vertIdx] : aiColor4D(1.0F, 1.0F, 1.0F, 1.0F);
            aiVector3D const& normal = pImportedMesh->mNormals[vertIdx];
            aiVector3D const& tangent = pImportedMesh->mTangents[vertIdx];
            aiVector3D const& texcoord = pImportedMesh->mTextureCoords[0][vertIdx];

            Vertex const vertex{
                { position.x, position.y, position.z },
                { color.r, color.g, color.b, },
                { normal.x, normal.y, normal.z },
                { tangent.x, tangent.y, tangent.z },
                { texcoord.x, texcoord.y },
            };

            vertices.push_back(vertex);
        }

        // Load index data
        for (uint32_t faceIdx = 0; faceIdx < pImportedMesh->mNumFaces; faceIdx++)
        {
            aiFace const& face = pImportedMesh->mFaces[faceIdx];
            for (uint32_t idx = 0; idx < face.mNumIndices; idx++) {
                indices.push_back(face.mIndices[idx]);
            }
        }

        std::shared_ptr<Mesh> mesh = createMesh(pDeviceContext, vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
        if (mesh == nullptr) {
            return false;
        }

        scene.addMesh(mesh);
    }

    //  Load all textures in scene
    for (uint32_t i = 0; i < pImportedScene->mNumTextures; i++)
    {
        aiTexture const* pImportedTexture = pImportedScene->mTextures[i];
        size_t const dataSize = pImportedTexture->mHeight == 0 ?
            pImportedTexture->mWidth : pImportedTexture->mWidth * pImportedTexture->mHeight * sizeof(aiTexel);
        
        std::shared_ptr<Texture> texture = loadTextureFromMemory(pDeviceContext, reinterpret_cast<void*>(pImportedTexture->pcData), dataSize);
        if (texture == nullptr || !texture->initDefaultView(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
            return false;
        }

        scene.addTexture(texture);
    }

    sceneTraverseChildren(
        scene,
        pImportedScene,
        RefUnused,
        pImportedScene->mRootNode,
        baseMeshRef,
        baseMaterialRef
    );
    return true;
}
