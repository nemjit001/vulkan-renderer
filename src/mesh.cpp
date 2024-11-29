#include "mesh.hpp"

#include <volk.h>

#include "render_backend.hpp"
#include "render_backend/buffer.hpp"
#include "render_backend/utils.hpp"

Mesh::Mesh(uint32_t vertexCount, uint32_t indexCount, std::shared_ptr<Buffer> vertexBuffer, std::shared_ptr<Buffer> indexBuffer)
    :
    vertexCount(vertexCount),
    indexCount(indexCount),
    vertexBuffer(vertexBuffer),
    indexBuffer(indexBuffer)
{
    //
}

std::shared_ptr<Mesh> createMesh(RenderDeviceContext* pDeviceContext, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount)
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
