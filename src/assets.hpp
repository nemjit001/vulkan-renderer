#pragma once

#include <cstdint>

#include "mesh.hpp"
#include "renderer.hpp"

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

/// @brief Load a texture from disk.
/// @param pDeviceContext Device context to use for texture loading.
/// @param path 
/// @param texture 
/// @return A boolean indicating success.
bool loadTexture(RenderDeviceContext* pDeviceContext, char const* path, Texture& texture);
