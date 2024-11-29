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
class Mesh
{
public:
    Mesh(uint32_t vertexCount, uint32_t indexCount, std::shared_ptr<Buffer> vertexBuffer, std::shared_ptr<Buffer> indexBuffer);
    ~Mesh() = default;

    Mesh(Mesh const&) = default;
    Mesh& operator=(Mesh const&) = default;

    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = default;

public:
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    std::shared_ptr<Buffer> vertexBuffer = nullptr;
    std::shared_ptr<Buffer> indexBuffer = nullptr;
};

/// @brief Create a mesh object, uploads vertex data to GPU memory.
/// @param pDeviceContext Render device context to use for mesh creation.
/// @param vertices 
/// @param vertexCount 
/// @param indices 
/// @param indexCount 
/// @return A mesh or NULL on error.
std::shared_ptr<Mesh> createMesh(RenderDeviceContext* pDeviceContext, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount);
