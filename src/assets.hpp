#pragma once

#include <cstdint>

#include "mesh.hpp"
#include "render_backend.hpp"

/// @brief Read a binary shader file.
/// @param path Path to the shader file.
/// @param shaderCode Shader code vector.
/// @return true on success, false otherwise.
bool readShaderFile(char const* path, std::vector<uint32_t>& shaderCode);

/// @brief Load an OBJ file from disk.
/// @param pDeviceContext Render device context to use for mesh loading.
/// @param path 
/// @param mesh 
/// @return A boolean indicating success.
bool loadOBJ(RenderDeviceContext* pDeviceContext, char const* path, Mesh& mesh);

/// @brief Load a texture from disk, mipmaps are generated automatically on load.
/// @param pDeviceContext Device context to use for texture loading.
/// @param path 
/// @param texture 
/// @return A boolean indicating success.
bool loadTexture(RenderDeviceContext* pDeviceContext, char const* path, Texture& texture);

/// @brief Upload data to a texture. Mipmaps are generated for the texture automatically. The size of the data buffer must be the same
/// as the 0th mip level extent * channels.
/// @param pDeviceContext 
/// @param texture 
/// @param pData 
/// @param size 
/// @return A boolean indicating success.
bool uploadToTexture(RenderDeviceContext* pDeviceContext, Texture& texture, void* pData, size_t size);
