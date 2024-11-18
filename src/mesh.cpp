#include "mesh.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

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
