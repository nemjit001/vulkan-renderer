#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Vertex;
class Mesh;
class RenderDeviceContext;
class Scene;
class Texture;

/// @brief Read a binary shader file.
/// @param path Path to the shader file.
/// @param shaderCode Shader code vector.
/// @return true on success, false otherwise.
bool readShaderFile(char const* path, std::vector<uint32_t>& shaderCode);

/// @brief Create a mesh object, uploads vertex data to GPU memory.
/// @param pDeviceContext Render device context to use for mesh creation.
/// @param vertices 
/// @param vertexCount 
/// @param indices 
/// @param indexCount 
/// @return A mesh or NULL on error.
std::shared_ptr<Mesh> createMesh(RenderDeviceContext* pDeviceContext, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount);

/// @brief Load an OBJ file from disk.
/// @param pDeviceContext Render device context to use for mesh loading.
/// @param path 
/// @return A boolean indicating success.
std::shared_ptr<Mesh> loadOBJ(RenderDeviceContext* pDeviceContext, char const* path);

/// @brief Load a texture from disk, mipmaps are generated automatically on load.
/// @param pDeviceContext Device context to use for texture loading.
/// @param path 
/// @return A texture or NULL on error.
std::shared_ptr<Texture> loadTexture(RenderDeviceContext* pDeviceContext, char const* path);

/// @brief Load a texture from memory, mipmaps are generated automatically on load.
/// @param pDeviceContext Device context to use for texture loading.
/// @param pData 
/// @param size
/// @return A texture or NULL on error.
std::shared_ptr<Texture> loadTextureFromMemory(RenderDeviceContext* pDeviceContext, void* pData, size_t size);

/// @brief Load cubemap textures from disk.
/// @param pDeviceContext 
/// @param faces 
/// @return 
std::shared_ptr<Texture> loadCubeMap(RenderDeviceContext* pDeviceContext, std::array<std::string, 6> const& faces);

/// @brief Upload data to a texture. Mipmaps are generated for the texture automatically. The size of the data buffer must be the same
/// as the 0th mip level extent * channels.
/// @param pDeviceContext 
/// @param texture 
/// @param pData 
/// @param size 
/// @param layer Texture layer to upload to.
/// @return A boolean indicating success.
bool uploadToTexture(RenderDeviceContext* pDeviceContext, std::shared_ptr<Texture> texture, void* pData, size_t size, uint32_t layer = 0);

/// @brief Generate mipmaps for a texture.
/// @param pDeviceContext 
/// @param texture 
/// @param layer Layer to generate mipmaps for.
/// @return 
bool generateMipMaps(RenderDeviceContext* pDeviceContext, std::shared_ptr<Texture> texture, uint32_t layer = 0);

/// @brief Load a scene file from disk.
/// @param pDeviceContext 
/// @param path 
/// @param scene The scene to populate w/ scene data, it will NOT be cleared before loading.
/// @return 
bool loadScene(RenderDeviceContext* pDeviceContext, char const* path, Scene& scene);
