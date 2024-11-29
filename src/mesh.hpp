#pragma once

#include <memory>

#include "math.hpp"

class Buffer;
class RenderDeviceContext;

/// @brief Vertex struct with interleaved per-vertex data.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 texCoord;
};

/// @brief Mesh representation with indexed vertices.
struct Mesh
{
    /// @brief Destroy this mesh.
    void destroy();

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    std::shared_ptr<Buffer> vertexBuffer = nullptr;
    std::shared_ptr<Buffer> indexBuffer = nullptr;
};

/// @brief Create a mesh object, uploads vertex data to GPU memory.
/// @param pDeviceContext Render device context to use for mesh creation.
/// @param mesh Mesh to initialize.
/// @param vertices 
/// @param vertexCount 
/// @param indices 
/// @param indexCount 
/// @return A boolean indicating success
bool createMesh(RenderDeviceContext* pDeviceContext, Mesh& mesh, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount);
