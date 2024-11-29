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
