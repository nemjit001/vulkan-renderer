#include "mesh.hpp"

#include <volk.h>

void Mesh::destroy()
{
    indexBuffer.destroy();
    vertexBuffer.destroy();
}

bool createMesh(RenderDeviceContext* pDeviceContext, Mesh& mesh, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount)
{
    assert(vertices != nullptr);
    assert(indices != nullptr);
    assert(vertexCount > 0);
    assert(indexCount > 0);

    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;

    uint32_t const vertexBufferSize = sizeof(Vertex) * vertexCount;
    uint32_t const indexBufferSize = sizeof(uint32_t) * indexCount;

    Buffer vertexUploadBuffer{};
    Buffer indexUploadBuffer{};
    if (!pDeviceContext->createBuffer(vertexUploadBuffer, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)
        || !pDeviceContext->createBuffer(indexUploadBuffer, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
        return false;
    }

    if (!pDeviceContext->createBuffer(mesh.vertexBuffer, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || !pDeviceContext->createBuffer(mesh.indexBuffer, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return false;
    } 

    assert(vertexUploadBuffer.mapped
        && vertexUploadBuffer.pData != nullptr
        && indexUploadBuffer.mapped
        && indexUploadBuffer.pData != nullptr
    );
    memcpy(vertexUploadBuffer.pData, vertices, vertexBufferSize);
    memcpy(indexUploadBuffer.pData, indices, indexBufferSize);

    // Schedule upload using transient upload buffer
    {
        VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
        if (!pDeviceContext->createCommandBuffer(CommandQueueType::Copy, &uploadCommandBuffer))
        {
            indexUploadBuffer.destroy();
            vertexUploadBuffer.destroy();
            return false;
        }

        VkCommandBufferBeginInfo uploadBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        uploadBeginInfo.pInheritanceInfo = nullptr;

        if (VK_FAILED(vkBeginCommandBuffer(uploadCommandBuffer, &uploadBeginInfo)))
        {
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Direct, uploadCommandBuffer);
            indexUploadBuffer.destroy();
            vertexUploadBuffer.destroy();
            return false;
        }

        VkBufferCopy vertexCopy{};
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = 0;
        vertexCopy.size = vertexBufferSize;

        VkBufferCopy indexCopy{};
        indexCopy.srcOffset = 0;
        indexCopy.dstOffset = 0;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(uploadCommandBuffer, vertexUploadBuffer.handle, mesh.vertexBuffer.handle, 1, &vertexCopy);
        vkCmdCopyBuffer(uploadCommandBuffer, indexUploadBuffer.handle, mesh.indexBuffer.handle, 1, &indexCopy);

        if (VK_FAILED(vkEndCommandBuffer(uploadCommandBuffer)))
        {
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Direct, uploadCommandBuffer);
            indexUploadBuffer.destroy();
            vertexUploadBuffer.destroy();
            return false;
        }

        VkFence uploadFence = VK_NULL_HANDLE;
        if (!pDeviceContext->createFence(&uploadFence, false))
        {
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
            indexUploadBuffer.destroy();
            vertexUploadBuffer.destroy();
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
            pDeviceContext->destroyFence(uploadFence);
            pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
            indexUploadBuffer.destroy();
            vertexUploadBuffer.destroy();
            return false;
        }

        vkWaitForFences(pDeviceContext->device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
        pDeviceContext->destroyFence(uploadFence);
        pDeviceContext->destroyCommandBuffer(CommandQueueType::Copy, uploadCommandBuffer);
    }

    indexUploadBuffer.destroy();
    vertexUploadBuffer.destroy();
    return true;
}
