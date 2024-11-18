#include "mesh.hpp"

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

    uint32_t const vertexBufferSize = sizeof(Vertex) * vertexCount;
    uint32_t const indexBufferSize = sizeof(uint32_t) * indexCount;

    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;

    if (!pDeviceContext->createBuffer(mesh.vertexBuffer, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        return false;
    }

    if (!pDeviceContext->createBuffer(mesh.indexBuffer, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        return false;
    }

    mesh.vertexBuffer.map();
    memcpy(mesh.vertexBuffer.pData, vertices, vertexBufferSize);
    mesh.vertexBuffer.unmap();

    mesh.indexBuffer.map();
    memcpy(mesh.indexBuffer.pData, indices, indexBufferSize);
    mesh.indexBuffer.unmap();

    return true;
}
