#define _CRT_SECURE_NO_WARNINGS //< Used to silence C file IO function warnings

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#define SDL_MAIN_HANDLED
#define STB_IMAGE_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <volk.h>

#include "math.hpp"
#include "renderer.hpp"
#include "timer.hpp"

namespace Engine
{
    namespace
    {
        /// @brief Vertex struct with interleaved per-vertex data.
        struct Vertex
        {
            glm::vec3 position;
            glm::vec3 color;
            glm::vec3 normal;
            glm::vec3 tangent;
            glm::vec2 texCoord;
        };

        /// @brief Simple TRS transform.
        struct Transform
        {
            /// @brief Calculate the transformation matrix for this transform.
            /// @return 
            glm::mat4 matrix() const;

            glm::vec3 position = glm::vec3(0.0F);
            glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            glm::vec3 scale = glm::vec3(1.0F);
        };

        /// @brief Virtual camera.
        struct Camera
        {
            /// @brief Calculate the view and projection matrix for this camera.
            /// @return 
            glm::mat4 matrix() const;

            // Camera transform
            glm::vec3 position = glm::vec3(0.0F);
            glm::vec3 forward = glm::vec3(0.0F, 0.0F, 1.0F);
            glm::vec3 up = glm::vec3(0.0F, 1.0F, 0.0F);

            // Perspective camera data
            float FOVy = 60.0F;
            float aspectRatio = 1.0F;
            float zNear = 0.1F;
            float zFar = 100.0F;
        };

        /// @brief Mesh representation with indexed vertices.
        struct Mesh
        {
            /// @brief Destroy this mesh.
            void destroy();

            uint32_t vertexCount;
            uint32_t indexCount;
            Buffer vertexBuffer{};
            Buffer indexBuffer{};
        };

        /// @brief Uniform scene data structure, matches data uniform available in shaders.
        struct UniformSceneData
        {
            alignas(16) glm::vec3 sunDirection;
            alignas(16) glm::vec3 sunColor;
            alignas(16) glm::vec3 ambientLight;
            alignas(16) glm::vec3 cameraPosition;
            alignas(16) glm::mat4 viewproject;
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 normal;
            alignas(4)  float specularity;
        };
    } // namespace

    constexpr char const* pWindowTitle = "Vulkan Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool isRunning = true;
    SDL_Window* pWindow = nullptr;
    Timer frameTimer{};
    RenderDeviceContext* pDeviceContext = nullptr;

    // GUI data
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE; //< descriptor pool specifically for ImGui usage

    // Per pass data
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkViewport viewport{};
    VkRect2D scissor{};

    // Scene data
    Buffer sceneDataBuffer{};
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // Scene objects
    Camera camera{};
    Transform transform{};
    Mesh mesh{};

    // Material data
    Texture colorTexture{};
    VkImageView colorTextureView;
    Texture normalTexture{};
    VkImageView normalTextureView;

    // CPU side render data
    float sunAzimuth = 0.0F;
    float sunZenith = 0.0F;
    glm::vec3 sunColor = glm::vec3(1.0F);
    glm::vec3 ambientLight = glm::vec3(0.1F);
    float specularity = 0.5F;
    UniformSceneData sceneData{};

    namespace
    {
        /// @brief Read a binary shader file.
        /// @param path Path to the shader file.
        /// @param shaderCode Shader code vector.
        /// @return true on success, false otherwise.
        bool readShaderFile(char const* path, std::vector<uint32_t>& shaderCode)
        {
            FILE* pFile = fopen(path, "rb");
            if (pFile == nullptr)
            {
                printf("VK Renderer failed to open file [%s]\n", path);
                return false;
            }

            fseek(pFile, 0, SEEK_END);
            size_t codeSize = ftell(pFile);

            assert(codeSize % 4 == 0);
            shaderCode.resize(codeSize / 4, 0);

            fseek(pFile, 0, SEEK_SET);
            fread(shaderCode.data(), sizeof(uint32_t), shaderCode.size(), pFile);

            fclose(pFile);
            return true;
        }

        /// @brief Create a mesh object.
        /// @param mesh Mesh to initialize.
        /// @param vertices 
        /// @param vertexCount 
        /// @param indices 
        /// @param indexCount 
        /// @return A boolean indicating success
        bool createMesh(Mesh& mesh, Vertex* vertices, uint32_t vertexCount, uint32_t* indices, uint32_t indexCount)
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

        /// @brief Load an OBJ file from disk.
        /// @param path 
        /// @param mesh 
        /// @return A boolean indicating success.
        bool loadOBJ(char const* path, Mesh& mesh)
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

            return createMesh(mesh, vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
        }

        /// @brief Load a texture from disk.
        /// @param path 
        /// @param texture 
        /// @return A boolean indicating success.
        bool loadTexture(char const* path, Texture& texture)
        {
            int width = 0, height = 0, channels = 0;
            stbi_uc* pImageData = stbi_load(path, &width, &height, &channels, 4);
            if (pImageData == nullptr)
            {
                printf("STBI image load failed [%s]\n", path);
                return false;
            }
            printf("Loaded texture [%s] (%d x %d x %d)\n", path, width, height, channels);

            Buffer uploadBuffer{};
            if (!pDeviceContext->createBuffer(uploadBuffer, width * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
                stbi_image_free(pImageData);
                return false;
            }

            assert(uploadBuffer.mapped && uploadBuffer.pData != nullptr);
            memcpy(uploadBuffer.pData, pImageData, uploadBuffer.size);

            if (!pDeviceContext->createTexture(
                texture,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1
            )) {
                uploadBuffer.destroy();
                stbi_image_free(pImageData);
                return false;
            }

            // Schedule upload using transient upload buffer
            {
                VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                VkFence uploadFence = VK_NULL_HANDLE;
                if (VK_FAILED(vkCreateFence(pDeviceContext->device, &fenceCreateInfo, nullptr, &uploadFence)))
                {
                    vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                    uploadBuffer.destroy();
                    stbi_image_free(pImageData);
                    return false;
                }

                VkCommandBufferAllocateInfo uploadBufAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
                uploadBufAllocInfo.commandPool = pDeviceContext->commandPool;
                uploadBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                uploadBufAllocInfo.commandBufferCount = 1;

                VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
                if (VK_FAILED(vkAllocateCommandBuffers(pDeviceContext->device, &uploadBufAllocInfo, &uploadCommandBuffer)))
                {
                    vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                    uploadBuffer.destroy();
                    stbi_image_free(pImageData);
                    return false;
                }

                VkCommandBufferBeginInfo uploadBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
                uploadBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                uploadBeginInfo.pInheritanceInfo = nullptr;

                if (VK_FAILED(vkBeginCommandBuffer(uploadCommandBuffer, &uploadBeginInfo)))
                {
                    vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                    vkFreeCommandBuffers(pDeviceContext->device, pDeviceContext->commandPool, 1, &uploadCommandBuffer);
                    uploadBuffer.destroy();
                    stbi_image_free(pImageData);
                    return false;
                }

                VkImageMemoryBarrier transferBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                transferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                transferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                transferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                transferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                transferBarrier.image = texture.handle;
                transferBarrier.subresourceRange.baseMipLevel = 0;
                transferBarrier.subresourceRange.levelCount = texture.levels;
                transferBarrier.subresourceRange.baseArrayLayer = 0;
                transferBarrier.subresourceRange.layerCount = texture.depthOrLayers;
                transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

                vkCmdPipelineBarrier(uploadCommandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &transferBarrier
                );

                VkBufferImageCopy imageCopy{};
                imageCopy.bufferOffset = 0;
                imageCopy.bufferRowLength = width;
                imageCopy.bufferImageHeight = height;
                imageCopy.imageSubresource.mipLevel = 0;
                imageCopy.imageSubresource.baseArrayLayer = 0;
                imageCopy.imageSubresource.layerCount = 1;
                imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageCopy.imageOffset = VkOffset3D{ 0, 0, 0 };
                imageCopy.imageExtent = VkExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

                vkCmdCopyBufferToImage(
                    uploadCommandBuffer,
                    uploadBuffer.handle,
                    texture.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &imageCopy
                );

                VkImageMemoryBarrier shaderBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shaderBarrier.image = texture.handle;
                shaderBarrier.subresourceRange.baseMipLevel = 0;
                shaderBarrier.subresourceRange.levelCount = texture.levels;
                shaderBarrier.subresourceRange.baseArrayLayer = 0;
                shaderBarrier.subresourceRange.layerCount = texture.depthOrLayers;
                shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

                vkCmdPipelineBarrier(uploadCommandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &shaderBarrier
                );

                if (VK_FAILED(vkEndCommandBuffer(uploadCommandBuffer)))
                {
                    vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                    vkFreeCommandBuffers(pDeviceContext->device, pDeviceContext->commandPool, 1, &uploadCommandBuffer);
                    uploadBuffer.destroy();
                    stbi_image_free(pImageData);
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
                    vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                    vkFreeCommandBuffers(pDeviceContext->device, pDeviceContext->commandPool, 1, &uploadCommandBuffer);
                    uploadBuffer.destroy();
                    stbi_image_free(pImageData);
                    return false;
                }

                vkWaitForFences(pDeviceContext->device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(pDeviceContext->device, uploadFence, nullptr);
                vkFreeCommandBuffers(pDeviceContext->device, pDeviceContext->commandPool, 1, &uploadCommandBuffer);
            }

            uploadBuffer.destroy();
            stbi_image_free(pImageData);
            return true;
        }

        glm::mat4 Transform::matrix() const
        {
            return glm::translate(glm::identity<glm::mat4>(), position)
                * glm::mat4_cast(rotation)
                * glm::scale(glm::identity<glm::mat4>(), scale);
        }

        glm::mat4 Camera::matrix() const
        {
            return glm::perspective(glm::radians(FOVy), aspectRatio, zNear, zFar) * glm::lookAt(position, position + forward, up);
        }

        void Mesh::destroy()
        {
            mesh.indexBuffer.destroy();
            mesh.vertexBuffer.destroy();
        }
    } // namespace

    bool init()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)(io);
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();

        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            printf("SDL init failed: %s\n", SDL_GetError());
            return false;
        }

        pWindow = SDL_CreateWindow(pWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DefaultWindowWidth, DefaultWindowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
        if (pWindow == nullptr)
        {
            printf("SDL window create failed: %s\n", SDL_GetError());
            return false;
        }

        if (!ImGui_ImplSDL2_InitForVulkan(pWindow))
        {
            printf("ImGui init for SDL2 failed\n");
            return false;
        }

        if (!Renderer::init(pWindow))
        {
            printf("VK Renderer renderer init failed\n");
            return false;
        }
        pDeviceContext = Renderer::pickRenderDevice();

        // Init ImGui for Vulkan
        {
            VkDescriptorPoolSize poolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            };

            VkDescriptorPoolCreateInfo imguiPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            imguiPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            imguiPoolCreateInfo.maxSets = 1;
            imguiPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(poolSizes);
            imguiPoolCreateInfo.pPoolSizes = poolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &imguiPoolCreateInfo, nullptr, &imguiDescriptorPool)))
            {
                printf("Vulkan imgui descriptor pool create failed\n");
                return false;
            }

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = Renderer::getInstance();
            initInfo.PhysicalDevice = pDeviceContext->physicalDevice;
            initInfo.Device = pDeviceContext->device;
            initInfo.QueueFamily = pDeviceContext->directQueueFamily;
            initInfo.Queue = pDeviceContext->directQueue;
            initInfo.DescriptorPool = imguiDescriptorPool;
            initInfo.RenderPass = pDeviceContext->renderPass;
            initInfo.MinImageCount = pDeviceContext->swapchainCreateInfo.minImageCount;
            initInfo.ImageCount = pDeviceContext->swapchainCreateInfo.minImageCount;
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.PipelineCache = VK_NULL_HANDLE;
            initInfo.Subpass = 0;
            initInfo.UseDynamicRendering = false;
            initInfo.Allocator = nullptr;

            auto imguiLoadFunc = [](char const* pName, void* pUserData) { (void)(pUserData); return vkGetInstanceProcAddr(Renderer::getInstance(), pName); };
            if (!ImGui_ImplVulkan_LoadFunctions(imguiLoadFunc, nullptr)
                || !ImGui_ImplVulkan_Init(&initInfo))
            {
                printf("ImGui init for Vulkan failed\n");
                return false;
            }
        }

        // Create samplers, descriptor set layouts, pipeline layout, and graphics pipeline
        {
            VkSamplerCreateInfo samplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerCreateInfo.flags = 0;
            samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias = 0.0F;
            samplerCreateInfo.anisotropyEnable = VK_FALSE;
            samplerCreateInfo.maxAnisotropy = 0.0F;
            samplerCreateInfo.compareEnable = VK_FALSE;
            samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerCreateInfo.minLod = 0.0F;
            samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

            if (VK_FAILED(vkCreateSampler(pDeviceContext->device, &samplerCreateInfo, nullptr, &textureSampler)))
            {
                printf("Vulkan texture sampler create failed\n");
                return false;
            }

            VkDescriptorSetLayoutBinding sceneDataUniformBinding{};
            sceneDataUniformBinding.binding = 0;
            sceneDataUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneDataUniformBinding.descriptorCount = 1;
            sceneDataUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            sceneDataUniformBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding colorTextureBinding{};
            colorTextureBinding.binding = 1;
            colorTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            colorTextureBinding.descriptorCount = 1;
            colorTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            colorTextureBinding.pImmutableSamplers = &textureSampler;

            VkDescriptorSetLayoutBinding normalTextureBinding{};
            normalTextureBinding.binding = 2;
            normalTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            normalTextureBinding.descriptorCount = 1;
            normalTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            normalTextureBinding.pImmutableSamplers = &textureSampler;

            VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = { sceneDataUniformBinding, colorTextureBinding, normalTextureBinding, };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            descriptorSetLayoutCreateInfo.flags = 0;
            descriptorSetLayoutCreateInfo.bindingCount = SIZEOF_ARRAY(descriptorSetLayoutBindings);
            descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;

            if (VK_FAILED(vkCreateDescriptorSetLayout(pDeviceContext->device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout)))
            {
                printf("Vulkan descriptor set layout create failed\n");
                return false;
            }

            VkDescriptorSetLayout descriptorSetLayouts[] = { descriptorSetLayout, };
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipelineLayoutCreateInfo.flags = 0;
            pipelineLayoutCreateInfo.setLayoutCount = SIZEOF_ARRAY(descriptorSetLayouts);
            pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts;
            pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
            pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

            if (VK_FAILED(vkCreatePipelineLayout(pDeviceContext->device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout)))
            {
                printf("Vulkan pipeline layout create failed\n");
                return false;
            }

            std::vector<uint32_t> vertexShaderCode;
            if (!readShaderFile("static.vert.spv", vertexShaderCode))
            {
                printf("VK Renderer vertex shader read failed\n");
                return false;
            }

            VkShaderModuleCreateInfo vertexShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            vertexShaderCreateInfo.flags = 0;
            vertexShaderCreateInfo.codeSize = static_cast<uint32_t>(vertexShaderCode.size() * sizeof(uint32_t));
            vertexShaderCreateInfo.pCode = vertexShaderCode.data();

            VkShaderModule vertexShader = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateShaderModule(pDeviceContext->device, &vertexShaderCreateInfo, nullptr, &vertexShader)))
            {
                printf("Vulkan vertex shader create failed\n");
                return false;
            }

            std::vector<uint32_t> fragmentShaderCode;
            if (!readShaderFile("forward.frag.spv", fragmentShaderCode))
            {
                printf("VK Renderer fragment shader read failed\n");
                return false;
            }

            VkShaderModuleCreateInfo fragmentShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            fragmentShaderCreateInfo.flags = 0;
            fragmentShaderCreateInfo.codeSize = static_cast<uint32_t>(fragmentShaderCode.size() * sizeof(uint32_t));
            fragmentShaderCreateInfo.pCode = fragmentShaderCode.data();

            VkShaderModule fragmentShader = VK_NULL_HANDLE;
            if (VK_FAILED(vkCreateShaderModule(pDeviceContext->device, &fragmentShaderCreateInfo, nullptr, &fragmentShader)))
            {
                printf("Vulkan vertex shader create failed\n");
                return false;
            }

            VkPipelineShaderStageCreateInfo vertexStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            vertexStageInfo.flags = 0;
            vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertexStageInfo.module = vertexShader;
            vertexStageInfo.pName = "main";
            vertexStageInfo.pSpecializationInfo = nullptr;

            VkPipelineShaderStageCreateInfo fragmentStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            fragmentStageInfo.flags = 0;
            fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragmentStageInfo.module = fragmentShader;
            fragmentStageInfo.pName = "main";
            fragmentStageInfo.pSpecializationInfo = nullptr;

            VkVertexInputBindingDescription vertexBindings[] = {
                VkVertexInputBindingDescription{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
            };

            VkVertexInputAttributeDescription vertexAttributes[] = {
                VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) },
                VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) },
                VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) },
                VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent) },
                VkVertexInputAttributeDescription{ 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) },
            };

            VkPipelineVertexInputStateCreateInfo vertexInputState{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertexInputState.flags = 0;
            vertexInputState.vertexBindingDescriptionCount = SIZEOF_ARRAY(vertexBindings);
            vertexInputState.pVertexBindingDescriptions = vertexBindings;
            vertexInputState.vertexAttributeDescriptionCount = SIZEOF_ARRAY(vertexAttributes);
            vertexInputState.pVertexAttributeDescriptions = vertexAttributes;

            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            inputAssemblyState.flags = 0;
            inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssemblyState.primitiveRestartEnable = VK_FALSE;

            float viewportWidth = static_cast<float>(pDeviceContext->swapchainCreateInfo.imageExtent.width);
            float viewportHeight = static_cast<float>(pDeviceContext->swapchainCreateInfo.imageExtent.height);
            viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
            scissor = VkRect2D{ VkOffset2D{ 0, 0 }, pDeviceContext->swapchainCreateInfo.imageExtent };

            VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            viewportState.flags = 0;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizationState{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            rasterizationState.flags = 0;
            rasterizationState.depthClampEnable = VK_FALSE;
            rasterizationState.rasterizerDiscardEnable = VK_FALSE;
            rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizationState.depthBiasEnable = VK_FALSE;
            rasterizationState.depthBiasConstantFactor = 0.0F;
            rasterizationState.depthBiasClamp = 0.0F;
            rasterizationState.depthBiasSlopeFactor = 0.0F;
            rasterizationState.lineWidth = 1.0F;

            VkPipelineMultisampleStateCreateInfo multisampleState{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            multisampleState.flags = 0;
            multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampleState.sampleShadingEnable = VK_FALSE;
            multisampleState.minSampleShading = 0.0F;
            multisampleState.pSampleMask = nullptr;
            multisampleState.alphaToCoverageEnable = VK_FALSE;
            multisampleState.alphaToOneEnable = VK_FALSE;

            VkPipelineDepthStencilStateCreateInfo depthStencilState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            depthStencilState.flags = 0;
            depthStencilState.depthTestEnable = VK_TRUE;
            depthStencilState.depthWriteEnable = VK_TRUE;
            depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencilState.depthBoundsTestEnable = VK_TRUE;
            depthStencilState.stencilTestEnable = VK_FALSE;
            depthStencilState.front =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilState.back =
                VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            depthStencilState.minDepthBounds = 0.0F;
            depthStencilState.maxDepthBounds = 1.0F;

            VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {
                VkPipelineColorBlendAttachmentState{
                    VK_FALSE,
                    VK_BLEND_FACTOR_ZERO,
                    VK_BLEND_FACTOR_ONE,
                    VK_BLEND_OP_ADD,
                    VK_BLEND_FACTOR_ZERO,
                    VK_BLEND_FACTOR_ONE,
                    VK_BLEND_OP_ADD,
                    VK_COLOR_COMPONENT_R_BIT
                    | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT
                },
            };

            VkPipelineColorBlendStateCreateInfo colorBlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            colorBlendState.flags = 0;
            colorBlendState.logicOpEnable = VK_FALSE;
            colorBlendState.logicOp = VK_LOGIC_OP_CLEAR;
            colorBlendState.attachmentCount = SIZEOF_ARRAY(colorBlendAttachments);
            colorBlendState.pAttachments = colorBlendAttachments;
            colorBlendState.blendConstants[0] = 0.0F;
            colorBlendState.blendConstants[1] = 0.0F;
            colorBlendState.blendConstants[2] = 0.0F;
            colorBlendState.blendConstants[3] = 0.0F;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };

            VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            dynamicState.flags = 0;
            dynamicState.dynamicStateCount = SIZEOF_ARRAY(dynamicStates);
            dynamicState.pDynamicStates = dynamicStates;

            VkPipelineShaderStageCreateInfo shaderStages[] = { vertexStageInfo, fragmentStageInfo, };
            VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            graphicsPipelineCreateInfo.flags = 0;
            graphicsPipelineCreateInfo.stageCount = SIZEOF_ARRAY(shaderStages);
            graphicsPipelineCreateInfo.pStages = shaderStages;
            graphicsPipelineCreateInfo.pVertexInputState = &vertexInputState;
            graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
            graphicsPipelineCreateInfo.pTessellationState = nullptr;
            graphicsPipelineCreateInfo.pViewportState = &viewportState;
            graphicsPipelineCreateInfo.pRasterizationState = &rasterizationState;
            graphicsPipelineCreateInfo.pMultisampleState = &multisampleState;
            graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilState;
            graphicsPipelineCreateInfo.pColorBlendState = &colorBlendState;
            graphicsPipelineCreateInfo.pDynamicState = &dynamicState;
            graphicsPipelineCreateInfo.layout = pipelineLayout;
            graphicsPipelineCreateInfo.renderPass = pDeviceContext->renderPass;
            graphicsPipelineCreateInfo.subpass = 0;
            graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            graphicsPipelineCreateInfo.basePipelineIndex = 0;

            if (VK_FAILED(vkCreateGraphicsPipelines(pDeviceContext->device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &graphicsPipeline)))
            {
                printf("Vulkan graphics pipeline create failed\n");
                return false;
            }

            vkDestroyShaderModule(pDeviceContext->device, fragmentShader, nullptr);
            vkDestroyShaderModule(pDeviceContext->device, vertexShader, nullptr);
        }

        // Create uniform buffer with descriptor pool & descriptor sets
        {
            if (!pDeviceContext->createBuffer(sceneDataBuffer, sizeof(UniformSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true))
            {
                printf("Vulkan scene data buffer create failed\n");
                return false;
            }

            VkDescriptorPoolSize poolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
            };

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            descriptorPoolCreateInfo.flags = 0;
            descriptorPoolCreateInfo.maxSets = 1;
            descriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(poolSizes);
            descriptorPoolCreateInfo.pPoolSizes = poolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(pDeviceContext->device, &descriptorPoolCreateInfo, nullptr, &descriptorPool)))
            {
                printf("Vulkan descriptor pool create failed\n");
                return false;
            }

            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;

            if (VK_FAILED(vkAllocateDescriptorSets(pDeviceContext->device, &descriptorSetAllocInfo, &descriptorSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }
        }

        // Set up scene data & load mesh
        {
            camera.position = glm::vec3(0.0F, 0.0F, -5.0F);
            camera.forward = glm::normalize(glm::vec3(0.0F) - camera.position);
            camera.aspectRatio = static_cast<float>(DefaultWindowWidth) / static_cast<float>(DefaultWindowHeight);
            transform = Transform{};

            if (!loadOBJ("data/assets/suzanne.obj", mesh))
            {
                printf("VK Renderer mesh load failed\n");
                return false;
            }
        }

        // Load material data
        {
            if (!loadTexture("data/assets/brickwall.jpg", colorTexture))
            {
                printf("Vulkan color texture create failed\n");
                return false;
            }

            if (!loadTexture("data/assets/brickwall_normal.jpg", normalTexture))
            {
                printf("Vulkan normal texture create failed\n");
                return false;
            }

            VkImageViewCreateInfo colorTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            colorTextureViewCreateInfo.flags = 0;
            colorTextureViewCreateInfo.image = colorTexture.handle;
            colorTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            colorTextureViewCreateInfo.format = colorTexture.format;
            colorTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            colorTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
            colorTextureViewCreateInfo.subresourceRange.levelCount = colorTexture.levels;
            colorTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            colorTextureViewCreateInfo.subresourceRange.layerCount = colorTexture.depthOrLayers;
            colorTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &colorTextureViewCreateInfo, nullptr, &colorTextureView)))
            {
                printf("Vulkan color texture view create failed\n");
                return false;
            }

            VkImageViewCreateInfo normalTextureViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            normalTextureViewCreateInfo.flags = 0;
            normalTextureViewCreateInfo.image = normalTexture.handle;
            normalTextureViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            normalTextureViewCreateInfo.format = normalTexture.format;
            normalTextureViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            normalTextureViewCreateInfo.subresourceRange.baseMipLevel = 0;
            normalTextureViewCreateInfo.subresourceRange.levelCount = normalTexture.levels;
            normalTextureViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            normalTextureViewCreateInfo.subresourceRange.layerCount = normalTexture.depthOrLayers;
            normalTextureViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            if (VK_FAILED(vkCreateImageView(pDeviceContext->device, &normalTextureViewCreateInfo, nullptr, &normalTextureView)))
            {
                printf("Vulkan color texture view create failed\n");
                return false;
            }
        }

        // Update descriptor sets
        {
            VkDescriptorBufferInfo sceneDataBufferInfo{};
            sceneDataBufferInfo.buffer = sceneDataBuffer.handle;
            sceneDataBufferInfo.offset = 0;
            sceneDataBufferInfo.range = sceneDataBuffer.size;

            VkDescriptorImageInfo colorTextureInfo{};
            colorTextureInfo.sampler = VK_NULL_HANDLE;
            colorTextureInfo.imageView = colorTextureView;
            colorTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normalTextureInfo{};
            normalTextureInfo.sampler = VK_NULL_HANDLE;
            normalTextureInfo.imageView = normalTextureView;
            normalTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet sceneDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            sceneDataWrite.dstSet = descriptorSet;
            sceneDataWrite.dstBinding = 0;
            sceneDataWrite.dstArrayElement = 0;
            sceneDataWrite.descriptorCount = 1;
            sceneDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneDataWrite.pBufferInfo = &sceneDataBufferInfo;

            VkWriteDescriptorSet colorTextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            colorTextureWrite.dstSet = descriptorSet;
            colorTextureWrite.dstBinding = 1;
            colorTextureWrite.dstArrayElement = 0;
            colorTextureWrite.descriptorCount = 1;
            colorTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            colorTextureWrite.pImageInfo = &colorTextureInfo;

            VkWriteDescriptorSet normalTextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            normalTextureWrite.dstSet = descriptorSet;
            normalTextureWrite.dstBinding = 2;
            normalTextureWrite.dstArrayElement = 0;
            normalTextureWrite.descriptorCount = 1;
            normalTextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            normalTextureWrite.pImageInfo = &normalTextureInfo;

            VkWriteDescriptorSet descriptorWrites[] = { sceneDataWrite, colorTextureWrite, normalTextureWrite, };
            vkUpdateDescriptorSets(pDeviceContext->device, SIZEOF_ARRAY(descriptorWrites), descriptorWrites, 0, nullptr);
        }

        printf("Initialized Vulkan Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Vulkan Renderer\n");

        vkWaitForFences(pDeviceContext->device, 1, &pDeviceContext->directQueueIdle, VK_TRUE, UINT64_MAX);

        vkDestroyImageView(pDeviceContext->device, normalTextureView, nullptr);
        normalTexture.destroy();
        vkDestroyImageView(pDeviceContext->device, colorTextureView, nullptr);
        colorTexture.destroy();

        mesh.destroy();

        vkDestroyDescriptorPool(pDeviceContext->device, descriptorPool, nullptr);
        sceneDataBuffer.unmap();
        sceneDataBuffer.destroy();

        vkDestroyPipeline(pDeviceContext->device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(pDeviceContext->device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(pDeviceContext->device, descriptorSetLayout, nullptr);
        vkDestroySampler(pDeviceContext->device, textureSampler, nullptr);

        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(pDeviceContext->device, imguiDescriptorPool, nullptr);

        delete pDeviceContext;
        Renderer::shutdown();

        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(pWindow);
        SDL_Quit();

        ImGui::DestroyContext();
    }

    void resize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(pWindow, &width, &height);
        uint32_t windowFlags = SDL_GetWindowFlags(pWindow);
        if (width == 0 || height == 0 || (windowFlags & SDL_WINDOW_MINIMIZED) != 0) {
            return;
        }

        printf("Window resized (%d x %d)\n", width, height);
        if (!pDeviceContext->resizeSwapResources(static_cast<uint32_t>(width), static_cast<uint32_t>(height)))
        {
            printf("VK Renderer swap resource resize failed\n");
            isRunning = false;
        }

        // Set viewport & scissor
        float viewportWidth = static_cast<float>(width);
        float viewportHeight = static_cast<float>(height);
        viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
        scissor = VkRect2D{ VkOffset2D{ 0, 0 }, VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }};

        // Update camera aspect ratio
        camera.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    }

    void update()
    {
        // Tick frame timer
        frameTimer.tick();

        // Update window state
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_QUIT:
                isRunning = false;
                break;
            case SDL_WINDOWEVENT: {
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    resize();
                    break;
                default:
                    break;
                }
            } break;
            default:
                break;
            }
        }

        // Record GUI state
        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Vulkan Renderer Config"))
        {
            ImGui::SeparatorText("Statistics");
            ImGui::Text("Frame time: %10.2f ms", frameTimer.deltaTimeMS());
            ImGui::Text("FPS:        %10.2f fps", 1'000.0 / frameTimer.deltaTimeMS());

            ImGui::SeparatorText("Settings");
            ImGui::RadioButton("VSync Enabled", true);
            ImGui::RadioButton("VSync Disabled", false);
            ImGui::RadioButton("VSync Disabled with tearing", false);

            ImGui::SeparatorText("Scene");
            ImGui::DragFloat("Sun Azimuth", &sunAzimuth, 1.0F, 0.0F, 360.0F);
            ImGui::DragFloat("Sun Zenith", &sunZenith, 1.0F, -90.0F, 90.0F);
            ImGui::ColorEdit3("Sun Color", &sunColor[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);
            ImGui::ColorEdit3("Ambient Light", &ambientLight[0], ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);

            ImGui::SeparatorText("Material");
            ImGui::DragFloat("Specularity", &specularity, 0.01F, 0.0F, 1.0F);
        }
        ImGui::End();

        ImGui::Render();

        // Update camera data
        camera.position = glm::vec3(2.0F, 2.0F, -5.0F);
        camera.forward = glm::normalize(glm::vec3(0.0F) - camera.position);

        // Update transform data
        transform.rotation = glm::rotate(transform.rotation, (float)frameTimer.deltaTimeMS() / 1000.0F, glm::vec3(0.0F, 1.0F, 0.0F));

        // Update scene data
        sceneData.sunDirection = glm::normalize(glm::vec3{
            glm::cos(glm::radians(sunAzimuth)) * glm::sin(glm::radians(90.0F - sunZenith)),
            glm::cos(glm::radians(90.0F - sunZenith)),
            glm::sin(glm::radians(sunAzimuth)) * glm::sin(glm::radians(90.0F - sunZenith)),
            });
        sceneData.sunColor = sunColor;
        sceneData.ambientLight = ambientLight;
        sceneData.cameraPosition = glm::vec3(0.0F, 0.0F, -5.0F);
        sceneData.viewproject = camera.matrix();
        sceneData.model = transform.matrix();
        sceneData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(sceneData.model))));
        sceneData.specularity = specularity;

        // Update uniform buffer
        assert(sceneDataBuffer.mapped);
        memcpy(sceneDataBuffer.pData, &sceneData, sizeof(UniformSceneData));
    }

    void render()
    {
        // Await & start new frame
        vkWaitForFences(pDeviceContext->device, 1, &pDeviceContext->directQueueIdle, VK_TRUE, UINT64_MAX);
        if (!pDeviceContext->newFrame()) {
            resize();
            return;
        }

        uint32_t backbufferIndex = pDeviceContext->getCurrentBackbufferIndex();
        vkResetFences(pDeviceContext->device, 1, &pDeviceContext->directQueueIdle);
        vkResetCommandBuffer(pDeviceContext->commandBuffer, 0 /* no flags */);

        // Record frame commands
        {
            VkCommandBufferBeginInfo commandBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            commandBeginInfo.flags = 0;
            commandBeginInfo.pInheritanceInfo = nullptr;

            if (VK_FAILED(vkBeginCommandBuffer(pDeviceContext->commandBuffer, &commandBeginInfo)))
            {
                printf("Vulkan command buffer begin failed\n");
                isRunning = false;
                return;
            }

            VkExtent2D const swapExtent = pDeviceContext->swapchainCreateInfo.imageExtent;
            VkClearValue clearValues[] = {
                VkClearValue{{ 0.1F, 0.1F, 0.1F, 1.0F }},
                VkClearValue{{ 1.0F, 0x00 }},
            };

            VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassBeginInfo.renderPass = pDeviceContext->renderPass;
            renderPassBeginInfo.framebuffer = pDeviceContext->swapFramebuffers[backbufferIndex];
            renderPassBeginInfo.renderArea = VkRect2D{ VkOffset2D{ 0, 0 }, swapExtent };
            renderPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(pDeviceContext->commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Bind graphics pipeline
            vkCmdSetViewport(pDeviceContext->commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(pDeviceContext->commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(pDeviceContext->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            // Bind descriptor sets
            vkCmdBindDescriptorSets(pDeviceContext->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            // Draw mesh
            VkBuffer vertexBuffers[] = { mesh.vertexBuffer.handle, };
            VkDeviceSize offsets[] = { 0, };
            vkCmdBindVertexBuffers(pDeviceContext->commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(pDeviceContext->commandBuffer, mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(pDeviceContext->commandBuffer, mesh.indexCount, 1, 0, 0, 0);

            // Draw GUI
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), pDeviceContext->commandBuffer);

            vkCmdEndRenderPass(pDeviceContext->commandBuffer);

            if (VK_FAILED(vkEndCommandBuffer(pDeviceContext->commandBuffer)))
            {
                printf("Vulkan command buffer end failed\n");
                isRunning = false;
                return;
            }
        }

        // Submit recorded commands
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        };

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &pDeviceContext->swapAvailable;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &pDeviceContext->commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &pDeviceContext->swapReleased;

        if (VK_FAILED(vkQueueSubmit(pDeviceContext->directQueue, 1, &submitInfo, pDeviceContext->directQueueIdle)))
        {
            printf("Vulkan queue submit failed\n");
            isRunning = false;
            return;
        }

        if (!pDeviceContext->present()) {
            resize();
        }
    }
} // namespace Engine

int main()
{
    if (!Engine::init())
    {
        Engine::shutdown();
        return 1;
    }

    while (Engine::isRunning)
    {
        Engine::update();
        Engine::render();
    }

    Engine::shutdown();
    return 0;
}
