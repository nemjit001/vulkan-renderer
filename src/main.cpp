#define _CRT_SECURE_NO_WARNINGS //< Used to silence C file IO function warnings

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#define SDL_MAIN_HANDLED
#define TINYOBJLOADER_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#include <SDL.h>
#include <SDL_vulkan.h>
#include <tiny_obj_loader.h>
#include <volk.h>

#include "math.hpp"
#include "timer.hpp"

#define SIZEOF_ARRAY(val)   (sizeof((val)) / sizeof((val)[0]))
#define VK_FAILED(expr)     ((expr) != VK_SUCCESS)

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

        /// @brief GPU buffer with associated data.
        struct Buffer
        {
            /// @brief Destroy this buffer.
            void destroy();

            /// @brief Map buffer memory.
            void map();

            /// @brief Unmap buffer memory.
            void unmap();

            VkDevice device;
            VkBuffer handle;
            VkDeviceMemory memory;
            size_t size;
            bool mapped;
            void* pData;
        };

        /// @brief GPU texture with associated data.
        struct Texture
        {
            /// @brief Destroy this texture.
            void destroy();

            VkDevice device;
            VkImage handle;
            VkDeviceMemory memory;
            VkFormat format;
            uint32_t width;
            uint32_t height;
            uint32_t depthOrLayers;
            uint32_t levels;
        };

        /// @brief Simple TRS transform.
        struct Transform
        {
            glm::mat4 matrix() const;

            glm::vec3 position = glm::vec3(0.0F);
            glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            glm::vec3 scale = glm::vec3(1.0F);
        };

        /// @brief Virtual camera.
        struct Camera
        {
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
            void destroy();

            uint32_t vertexCount;
            uint32_t indexCount;
            Buffer vertexBuffer{};
            Buffer indexBuffer{};
        };

        /// @brief Uniform scene data structure, matches data uniform available in shaders.
        struct UniformSceneData
        {
            alignas(4)  glm::vec3 cameraPosition;
            alignas(16) glm::mat4 viewproject;
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 normal;
        };
    } // namespace

    constexpr char const* pWindowTitle = "Vulkan Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool isRunning = true;
    SDL_Window* pWindow = nullptr;
    Timer frameTimer;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT dbgMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties{};
    uint32_t directQueueFamily = VK_QUEUE_FAMILY_IGNORED;

    VkDevice device = VK_NULL_HANDLE;
    VkQueue directQueue = VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages{};
    std::vector<VkImageView> swapImageViews{};

    VkSemaphore swapAvailable = VK_NULL_HANDLE;
    VkSemaphore swapReleased = VK_NULL_HANDLE;
    VkFence frameReady = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    Texture depthStencilTexture{};
    VkImageView depthStencilView = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> swapFramebuffers{};

    // Per pass data
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

    // CPU side render data
    void* pSceneData = nullptr;
    UniformSceneData sceneData{};

    namespace
    {
        /// @brief Vulkan debug callback.
        /// @param severity 
        /// @param type 
        /// @param pCallbackData 
        /// @param pUserData 
        /// @return VK_FALSE as per Vulkan spec.
        VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
            void* pUserData
        )
        {
            (void)(severity);
            (void)(type);
            (void)(pUserData);

            printf("[Vulkan] %s\n", pCallbackData->pMessage);
            return VK_FALSE;
        }

        /// @brief Find a device queue family based on required and exclusion flags.
        /// @param physicalDevice 
        /// @param surface Optional surface parameter, if not VK_NULL_HANDLE the returned queue family supports presenting to this surface.
        /// @param flags Required queue flags.
        /// @param exclude Queue flags that must not be set.
        /// @return The found queue family or VK_QUEUE_FAMILY_IGNORED on failure.
        uint32_t findQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkQueueFlags flags, VkQueueFlags exclude)
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

            for (uint32_t i = 0; i < queueFamilyCount; i++)
            {
                VkBool32 surfaceSupport = VK_FALSE;
                if (surface != VK_NULL_HANDLE)
                {
                    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &surfaceSupport);
                }

                if ((queueFamilies[i].queueFlags & flags) == flags
                    && (queueFamilies[i].queueFlags & exclude) == 0)
                {
                    if (surface != VK_NULL_HANDLE && !surfaceSupport)
                    {
                        continue;
                    }

                    return i;
                }
            }

            printf("Failed to find queue family for combination of surface/flags/exclusions\n");
            return VK_QUEUE_FAMILY_IGNORED;
        }

        /// @brief Get the memory type index based on memory requirements and property flags.
        /// @param deviceMemProperties 
        /// @param requirements 
        /// @param propertyFlags 
        /// @return The memory type index or UINT32_MAX on failure.
        uint32_t getMemoryTypeIndex(VkPhysicalDeviceMemoryProperties const& deviceMemProperties, VkMemoryRequirements const& requirements, VkMemoryPropertyFlags propertyFlags)
        {
            for (uint32_t memIdx = 0; memIdx < deviceMemProperties.memoryTypeCount; memIdx++)
            {
                uint32_t const memoryTypeBits = (1 << memIdx);

                if ((requirements.memoryTypeBits & memoryTypeBits) != 0
                    && (deviceMemProperties.memoryTypes[memIdx].propertyFlags & propertyFlags) == propertyFlags)
                {
                    return memIdx;
                }
            }

            printf("Failed to find memory type index for memory requirements and propry flags combination\n");
            return ~0U;
        }

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

        /// @brief Create a GPU buffer.
        /// @param buffer Buffer to initialize.
        /// @param device 
        /// @param size Buffer size, must be greater than 0.
        /// @param usage 
        /// @param memoryProperties 
        /// @param createMapped Set to true if the buffer must be mapped on creation.
        /// @return A boolean indicating success.
        bool createBuffer(Buffer& buffer, VkDevice device, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties, bool createMapped = false)
        {
            assert(size > 0);

            buffer.device = device;
            buffer.size = size;
            buffer.mapped = false;
            buffer.pData = nullptr;

            VkBufferCreateInfo bufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferCreateInfo.flags = 0;
            bufferCreateInfo.usage = usage;
            bufferCreateInfo.size = size;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (VK_FAILED(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer.handle)))
            {
                return false;
            }

            VkMemoryRequirements memRequirements{};
            vkGetBufferMemoryRequirements(device, buffer.handle, &memRequirements);

            VkMemoryAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocateInfo.allocationSize = memRequirements.size;
            allocateInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, memRequirements, memoryProperties);

            if (VK_FAILED(vkAllocateMemory(device, &allocateInfo, nullptr, &buffer.memory)))
            {
                return false;
            }
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0);

            if (createMapped) {
                buffer.map();
            }

            return true;
        }

        /// @brief Create a GPU texture.
        /// @param texture Texture to initialize.
        /// @param device 
        /// @param imageType 
        /// @param format 
        /// @param usage 
        /// @param memoryProperties 
        /// @param width Texture width, must be greater than 0.
        /// @param height Texture height, must be greater than 0.
        /// @param depth Texture depth, must be greater than 0.
        /// @param levels Texture mip levels, must be greater than 0.
        /// @param layers Texture layers, if depth > 1 this must be 1.
        /// @param samples 
        /// @param tiling 
        /// @param initialLayout 
        /// @return A boolean indicating success.
        bool createTexture(
            Texture& texture,
            VkDevice device,
            VkImageType imageType,
            VkFormat format,
            VkImageUsageFlags usage,
            VkMemoryPropertyFlags memoryProperties,
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            uint32_t levels = 1,
            uint32_t layers = 1,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        )
        {
            assert(width > 0 && height > 0 && depth > 0);
            assert(levels > 0);
            assert(layers > 0);
            assert(depth == 1 || layers == 1); //< layers and depth may not both be >1.

            texture.device = device;
            texture.format = format;
            texture.width = width;
            texture.height = height;
            texture.depthOrLayers = depth == 1 ? layers : depth;
            texture.levels = levels;

            VkImageCreateInfo imageCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            imageCreateInfo.flags = 0;
            imageCreateInfo.imageType = imageType;
            imageCreateInfo.format = format;
            imageCreateInfo.extent = VkExtent3D{ width, height, depth };
            imageCreateInfo.mipLevels = levels;
            imageCreateInfo.arrayLayers = layers;
            imageCreateInfo.samples = samples;
            imageCreateInfo.tiling = tiling;
            imageCreateInfo.usage = usage;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCreateInfo.initialLayout = initialLayout;

            if (VK_FAILED(vkCreateImage(device, &imageCreateInfo, nullptr, &texture.handle)))
            {
                return false;
            }

            VkMemoryRequirements memRequirements{};
            vkGetImageMemoryRequirements(device, texture.handle, &memRequirements);

            VkMemoryAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocateInfo.allocationSize = memRequirements.size;
            allocateInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, memRequirements, memoryProperties);

            if (VK_FAILED(vkAllocateMemory(device, &allocateInfo, nullptr, &texture.memory)))
            {
                return false;
            }
            vkBindImageMemory(device, texture.handle, texture.memory, 0);

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

            if (!createBuffer(mesh.vertexBuffer, device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                return false;
            }

            if (!createBuffer(mesh.indexBuffer, device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
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

        void Buffer::destroy()
        {
            if (mapped) {
                unmap();
            }

            vkFreeMemory(device, memory, nullptr);
            vkDestroyBuffer(device, handle, nullptr);
        }

        void Buffer::map()
        {
            pData = nullptr;
            vkMapMemory(device, memory, 0, size, 0, &pData);
            assert(pData != nullptr);

            mapped = true;
        }

        void Buffer::unmap()
        {
            vkUnmapMemory(device, memory);
            mapped = false;
        }

        void Texture::destroy()
        {
            vkFreeMemory(device, memory, nullptr);
            vkDestroyImage(device, handle, nullptr);
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

        if (VK_FAILED(volkInitialize()))
        {
            printf("Volk init failed\n");
            return false;
        }

        // Create Vulkan instance
        {
            std::vector<char const*> layers;
            std::vector<char const*> extensions;

            uint32_t windowExtCount = 0;
            SDL_Vulkan_GetInstanceExtensions(nullptr /* unused */, &windowExtCount, nullptr);
            extensions.resize(windowExtCount);
            SDL_Vulkan_GetInstanceExtensions(nullptr /* unused */, &windowExtCount, extensions.data());

#ifndef NDEBUG
            layers.push_back("VK_LAYER_KHRONOS_validation");
            layers.push_back("VK_LAYER_KHRONOS_synchronization2");

            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

            VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
            appInfo.applicationVersion = 0;
            appInfo.pApplicationName = "VK Renderer";
            appInfo.engineVersion = 0;
            appInfo.pEngineName = "VK Renderer";
            appInfo.apiVersion = VK_API_VERSION_1_3;

            VkInstanceCreateInfo instanceCreateInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
            instanceCreateInfo.flags = 0;
            instanceCreateInfo.pApplicationInfo = &appInfo;
            instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            instanceCreateInfo.ppEnabledLayerNames = layers.data();
            instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
            VkDebugUtilsMessengerCreateInfoEXT dbgMessengerCreateInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            dbgMessengerCreateInfo.flags = 0;
            dbgMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
            dbgMessengerCreateInfo.pfnUserCallback = debugCallback;
            dbgMessengerCreateInfo.pUserData = nullptr;
            instanceCreateInfo.pNext = &dbgMessengerCreateInfo;
#endif

            if (VK_FAILED(vkCreateInstance(&instanceCreateInfo, nullptr, &instance)))
            {
                printf("Vulkan instance create failed\n");
                return false;
            }
            volkLoadInstanceOnly(instance);

#ifndef NDEBUG
            if (VK_FAILED(vkCreateDebugUtilsMessengerEXT(instance, &dbgMessengerCreateInfo, nullptr, &dbgMessenger)))
            {
                printf("Vulkan debug messenger create failed\n");
                return false;
            }
#endif
        }

        // Create render surface
        {
            if (!SDL_Vulkan_CreateSurface(pWindow, instance, &surface))
            {
                printf("Vulkan surface create failed\n");
                return false;
            }
        }

        // Find a physical device
        {
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
            std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
            vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

            physicalDevice = VK_NULL_HANDLE;
            for (auto& device : physicalDevices)
            {
                VkPhysicalDeviceFeatures2 deviceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
                deviceFeatures.pNext = nullptr;
                vkGetPhysicalDeviceFeatures2(device, &deviceFeatures);

                VkPhysicalDeviceProperties2 deviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
                deviceProperties.pNext = nullptr;
                vkGetPhysicalDeviceProperties2(device, &deviceProperties);

                if (deviceFeatures.features.samplerAnisotropy == VK_TRUE
                    && deviceFeatures.features.depthBounds == VK_TRUE)
                {
                    physicalDevice = device;
                    printf("Automatically selected render device: %s\n", deviceProperties.properties.deviceName);
                    break;
                }
            }

            if (physicalDevice == VK_NULL_HANDLE)
            {
                printf("Vulkan no supported physical device available\n");
                return false;
            }

            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
            directQueueFamily = findQueueFamily(physicalDevice, surface, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT, 0);

            if (directQueueFamily == VK_QUEUE_FAMILY_IGNORED)
            {
                printf("Vulkan direct queue unavailable\n");
                return false;
            }
        }

        // Create logical device
        {
            std::vector<char const*> extensions;
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

            float priorities[] = { 1.0F };
            VkDeviceQueueCreateInfo directQueueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            directQueueCreateInfo.flags = 0;
            directQueueCreateInfo.queueFamilyIndex = directQueueFamily;
            directQueueCreateInfo.queueCount = SIZEOF_ARRAY(priorities);
            directQueueCreateInfo.pQueuePriorities = priorities;

            VkPhysicalDeviceFeatures2 enabledFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            enabledFeatures.features.samplerAnisotropy = VK_TRUE;
            enabledFeatures.features.depthBounds = VK_TRUE;

            VkDeviceCreateInfo deviceCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
            deviceCreateInfo.flags = 0;
            deviceCreateInfo.queueCreateInfoCount = 1;
            deviceCreateInfo.pQueueCreateInfos = &directQueueCreateInfo;
            deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = extensions.data();
            deviceCreateInfo.pEnabledFeatures = nullptr;
            deviceCreateInfo.pNext = &enabledFeatures;

            if (VK_FAILED(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)))
            {
                printf("Vulkan device create failed\n");
                return false;
            }
            volkLoadDevice(device);
            vkGetDeviceQueue(device, directQueueFamily, 0, &directQueue);
            assert(directQueue != VK_NULL_HANDLE);
        }

        // Create swap chain & swap resources
        {
            // Query surface capabilities, formats, present modes, etc.
            VkSurfaceCapabilitiesKHR surfaceCaps{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

            uint32_t surfaceFormatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data());

            VkFormat preferredFormat = surfaceFormats[0].format;
            for (auto const& format : surfaceFormats)
            {
                // Pick first available SRGB format for swap
                if (format.format == VK_FORMAT_B8G8R8A8_SRGB
                    || format.format == VK_FORMAT_R8G8B8A8_SRGB)
                {
                    preferredFormat = format.format;
                }
            }

            // Create swap chain
            swapchainCreateInfo = VkSwapchainCreateInfoKHR{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
            swapchainCreateInfo.flags = 0;
            swapchainCreateInfo.surface = surface;
            swapchainCreateInfo.minImageCount = (surfaceCaps.maxImageCount == 0 || surfaceCaps.minImageCount + 1 < surfaceCaps.maxImageCount) ?
                surfaceCaps.minImageCount + 1 : surfaceCaps.maxImageCount;
            swapchainCreateInfo.imageFormat = preferredFormat;
            swapchainCreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            swapchainCreateInfo.imageExtent = (surfaceCaps.currentExtent.width == UINT32_MAX || surfaceCaps.currentExtent.height == UINT32_MAX) ?
                VkExtent2D{ DefaultWindowWidth, DefaultWindowHeight } : surfaceCaps.currentExtent;
            swapchainCreateInfo.imageArrayLayers = 1;
            swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainCreateInfo.preTransform = surfaceCaps.currentTransform;
            swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            swapchainCreateInfo.clipped = VK_FALSE;
            swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

            if (VK_FAILED(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)))
            {
                printf("Vulkan swap chain create failed\n");
                return false;
            }

            // Fetch swap images & create swap views
            uint32_t swapImageCount = 0;
            vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, nullptr);
            swapImages.resize(swapImageCount);
            vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());

            swapImageViews.reserve(swapImages.size());
            for (auto& image : swapImages)
            {
                VkImageViewCreateInfo swapViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                swapViewCreateInfo.flags = 0;
                swapViewCreateInfo.image = image;
                swapViewCreateInfo.format = swapchainCreateInfo.imageFormat;
                swapViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                swapViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.subresourceRange.baseMipLevel = 0;
                swapViewCreateInfo.subresourceRange.levelCount = 1;
                swapViewCreateInfo.subresourceRange.baseArrayLayer = 0;
                swapViewCreateInfo.subresourceRange.layerCount = 1;
                swapViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

                VkImageView view = VK_NULL_HANDLE;
                vkCreateImageView(device, &swapViewCreateInfo, nullptr, &view);
                assert(view != VK_NULL_HANDLE);
                swapImageViews.push_back(view);
            }
        }

        // Create synchronization primitives
        {
            VkSemaphoreCreateInfo semaphoreCreateInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            semaphoreCreateInfo.flags = 0;

            VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            if (VK_FAILED(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapAvailable))
                || VK_FAILED(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapReleased))
                || VK_FAILED(vkCreateFence(device, &fenceCreateInfo, nullptr, &frameReady)))
            {
                printf("Vulkan sync primitive create failed\n");
                return false;
            }
        }

        // Create command pool & command buffer
        {
            VkCommandPoolCreateInfo commandPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            commandPoolCreateInfo.queueFamilyIndex = directQueueFamily;

            if (VK_FAILED(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool)))
            {
                printf("Vulkan command pool create failed\n");
                return false;
            }

            VkCommandBufferAllocateInfo commandBufAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            commandBufAllocInfo.commandPool = commandPool;
            commandBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufAllocInfo.commandBufferCount = 1;

            if (VK_FAILED(vkAllocateCommandBuffers(device, &commandBufAllocInfo, &commandBuffer)))
            {
                printf("Vulkan command buffer allocation failed\n");
                return false;
            }
        }

        // Create depth stencil target
        {
            if (!createTexture(
                depthStencilTexture,
                device,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                swapchainCreateInfo.imageExtent.width, swapchainCreateInfo.imageExtent.height, 1
            ))
            {
                printf("Vulkan depth stencil texture create failed\n");
                return false;
            }

            VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            depthStencilViewCreateInfo.flags = 0;
            depthStencilViewCreateInfo.image = depthStencilTexture.handle;
            depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthStencilViewCreateInfo.format = depthStencilTexture.format;
            depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
            depthStencilViewCreateInfo.subresourceRange.levelCount = depthStencilTexture.levels;
            depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            depthStencilViewCreateInfo.subresourceRange.layerCount = depthStencilTexture.depthOrLayers;
            depthStencilViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (VK_FAILED(vkCreateImageView(device, &depthStencilViewCreateInfo, nullptr, &depthStencilView)))
            {
                printf("Vulkan depth stencil view create failed\n");
                return false;
            }
        }

        // Create main render pass
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.flags = 0;
            colorAttachment.format = swapchainCreateInfo.imageFormat;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentDescription depthStencilAttachment{};
            depthStencilAttachment.flags = 0;
            depthStencilAttachment.format = VK_FORMAT_D32_SFLOAT;
            depthStencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthStencilAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthStencilAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorAttachmentRefs[] = {
                VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            };

            VkAttachmentReference depthStencilAttachmentRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

            VkSubpassDescription forwardPass{};
            forwardPass.flags = 0;
            forwardPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            forwardPass.inputAttachmentCount = 0;
            forwardPass.pInputAttachments = nullptr;
            forwardPass.colorAttachmentCount = SIZEOF_ARRAY(colorAttachmentRefs);
            forwardPass.pColorAttachments = colorAttachmentRefs;
            forwardPass.pResolveAttachments = nullptr;
            forwardPass.pDepthStencilAttachment = &depthStencilAttachmentRef;
            forwardPass.preserveAttachmentCount = 0;
            forwardPass.pPreserveAttachments = nullptr;

            VkSubpassDependency previousFrameDependency{};
            previousFrameDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            previousFrameDependency.dstSubpass = 0;
            previousFrameDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            previousFrameDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            previousFrameDependency.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            previousFrameDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

            VkAttachmentDescription attachments[] = { colorAttachment, depthStencilAttachment, };
            VkSubpassDescription subpasses[] = { forwardPass, };
            VkSubpassDependency dependencies[] = { previousFrameDependency, };
            VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
            renderPassCreateInfo.flags = 0;
            renderPassCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
            renderPassCreateInfo.pAttachments = attachments;
            renderPassCreateInfo.subpassCount = SIZEOF_ARRAY(subpasses);
            renderPassCreateInfo.pSubpasses = subpasses;
            renderPassCreateInfo.dependencyCount = SIZEOF_ARRAY(dependencies);
            renderPassCreateInfo.pDependencies = dependencies;

            if (VK_FAILED(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass)))
            {
                printf("Vulkan render pass create failed\n");
                return false;
            }
        }

        // Create swap framebuffers
        {
            swapFramebuffers.reserve(swapImageViews.size());
            for (auto& swapView : swapImageViews)
            {
                VkExtent2D const swapExtent = swapchainCreateInfo.imageExtent;
                VkImageView attachments[] = { swapView, depthStencilView, };

                VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebufferCreateInfo.flags = 0;
                framebufferCreateInfo.renderPass = renderPass;
                framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
                framebufferCreateInfo.pAttachments = attachments;
                framebufferCreateInfo.width = swapExtent.width;
                framebufferCreateInfo.height = swapExtent.height;
                framebufferCreateInfo.layers = 1;

                VkFramebuffer framebuffer = VK_NULL_HANDLE;
                vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer);
                assert(framebuffer != VK_NULL_HANDLE);
                swapFramebuffers.push_back(framebuffer);
            }
        }

        // Create descriptor set layouts, pipeline layout, and graphics pipeline
        {
            VkDescriptorSetLayoutBinding sceneDataUniformBinding{};
            sceneDataUniformBinding.binding = 0;
            sceneDataUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneDataUniformBinding.descriptorCount = 1;
            sceneDataUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            sceneDataUniformBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = { sceneDataUniformBinding, };
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            descriptorSetLayoutCreateInfo.flags = 0;
            descriptorSetLayoutCreateInfo.bindingCount = SIZEOF_ARRAY(descriptorSetLayoutBindings);
            descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;

            if (VK_FAILED(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout)))
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

            if (VK_FAILED(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout)))
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
            if (VK_FAILED(vkCreateShaderModule(device, &vertexShaderCreateInfo, nullptr, &vertexShader)))
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
            if (VK_FAILED(vkCreateShaderModule(device, &fragmentShaderCreateInfo, nullptr, &fragmentShader)))
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

            float viewportWidth = static_cast<float>(swapchainCreateInfo.imageExtent.width);
            float viewportHeight = static_cast<float>(swapchainCreateInfo.imageExtent.height);
            viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
            scissor = VkRect2D{ VkOffset2D{ 0, 0 }, swapchainCreateInfo.imageExtent };

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
            graphicsPipelineCreateInfo.renderPass = renderPass;
            graphicsPipelineCreateInfo.subpass = 0;
            graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            graphicsPipelineCreateInfo.basePipelineIndex = 0;

            if (VK_FAILED(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &graphicsPipeline)))
            {
                printf("Vulkan graphics pipeline create failed\n");
                return false;
            }

            vkDestroyShaderModule(device, fragmentShader, nullptr);
            vkDestroyShaderModule(device, vertexShader, nullptr);
        }

        // Create uniform buffer with descriptor pool & descriptor sets
        {
            if (!createBuffer(sceneDataBuffer, device, sizeof(UniformSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true))
            {
                printf("Vulkan scene data buffer create failed\n");
                return false;
            }

            VkDescriptorPoolSize poolSizes[] = {
                VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            };

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            descriptorPoolCreateInfo.flags = 0;
            descriptorPoolCreateInfo.maxSets = 1;
            descriptorPoolCreateInfo.poolSizeCount = SIZEOF_ARRAY(poolSizes);
            descriptorPoolCreateInfo.pPoolSizes = poolSizes;

            if (VK_FAILED(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool)))
            {
                printf("Vulkan descriptor pool create failed\n");
                return false;
            }

            VkDescriptorSetAllocateInfo descriptorSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            descriptorSetAllocInfo.descriptorPool = descriptorPool;
            descriptorSetAllocInfo.descriptorSetCount = 1;
            descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;

            if (VK_FAILED(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSet)))
            {
                printf("Vulkan descriptor set allocation failed\n");
                return false;
            }
        }

        // Set up scene data
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

        // Update descriptor sets
        {
            VkDescriptorBufferInfo sceneDataBufferInfo{};
            sceneDataBufferInfo.buffer = sceneDataBuffer.handle;
            sceneDataBufferInfo.offset = 0;
            sceneDataBufferInfo.range = sceneDataBuffer.size;

            VkWriteDescriptorSet writeDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writeDescriptorSet.dstSet = descriptorSet;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.dstArrayElement = 0;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSet.pBufferInfo = &sceneDataBufferInfo;

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }

        printf("Initialized Vulkan Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Vulkan Renderer\n");

        vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);

        mesh.destroy();

        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        sceneDataBuffer.unmap();
        sceneDataBuffer.destroy();

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        for (auto& framebuffer : swapFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        vkDestroyRenderPass(device, renderPass, nullptr);

        vkDestroyImageView(device, depthStencilView, nullptr);
        depthStencilTexture.destroy();

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(device, commandPool, nullptr);

        vkDestroyFence(device, frameReady, nullptr);
        vkDestroySemaphore(device, swapReleased, nullptr);
        vkDestroySemaphore(device, swapAvailable, nullptr);

        for (auto& view : swapImageViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        vkDestroySwapchainKHR(device, swapchain, nullptr);

        vkDestroyDevice(device, nullptr);

        vkDestroySurfaceKHR(instance, surface, nullptr);
#ifndef NDEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, dbgMessenger, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
    }

    void resize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(pWindow, &width, &height);
        if (width == 0 || height == 0) {
            return;
        }

        printf("Window resized (%d x %d)\n", width, height);

        // Wait for previous frame
        vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);

        // Destroy swap dependent resources
        {
            vkDestroyImageView(device, depthStencilView, nullptr);
            depthStencilTexture.destroy();

            for (auto& framebuffer : swapFramebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            swapFramebuffers.clear();
        }

        // Recreate swap chain & swap resources
        {
            // Destroy swap views
            for (auto& view : swapImageViews) {
                vkDestroyImageView(device, view, nullptr);
            }
            swapImageViews.clear();
            swapImages.clear();

            // Query new surface capabilites
            VkSurfaceCapabilitiesKHR surfaceCaps{};
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

            // Recreate swap chain & destroy old swap chain
            swapchainCreateInfo.minImageCount = (surfaceCaps.maxImageCount == 0 || surfaceCaps.minImageCount + 1 < surfaceCaps.maxImageCount) ?
                surfaceCaps.minImageCount + 1 : surfaceCaps.maxImageCount;
            swapchainCreateInfo.imageExtent = (surfaceCaps.currentExtent.width == UINT32_MAX || surfaceCaps.currentExtent.height == UINT32_MAX) ?
                VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) } : surfaceCaps.currentExtent;
            swapchainCreateInfo.oldSwapchain = swapchain;

            if (VK_FAILED(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)))
            {
                printf("Vulkan swap chain resize failed\n");
                return;
            }
            vkDestroySwapchainKHR(device, swapchainCreateInfo.oldSwapchain, nullptr);

            // Fetch swap images & recreate views
            uint32_t swapImageCount = 0;
            vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, nullptr);
            swapImages.resize(swapImageCount);
            vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages.data());

            swapImageViews.reserve(swapImages.size());
            for (auto& image : swapImages)
            {
                VkImageViewCreateInfo swapViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                swapViewCreateInfo.flags = 0;
                swapViewCreateInfo.image = image;
                swapViewCreateInfo.format = swapchainCreateInfo.imageFormat;
                swapViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                swapViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
                swapViewCreateInfo.subresourceRange.baseMipLevel = 0;
                swapViewCreateInfo.subresourceRange.levelCount = 1;
                swapViewCreateInfo.subresourceRange.baseArrayLayer = 0;
                swapViewCreateInfo.subresourceRange.layerCount = 1;
                swapViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

                VkImageView view = VK_NULL_HANDLE;
                vkCreateImageView(device, &swapViewCreateInfo, nullptr, &view);
                assert(view != VK_NULL_HANDLE);
                swapImageViews.push_back(view);
            }
        }

        // Recreate swap dependent resources
        {
            if (!createTexture(
                depthStencilTexture,
                device,
                VK_IMAGE_TYPE_2D,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                swapchainCreateInfo.imageExtent.width, swapchainCreateInfo.imageExtent.height, 1
            ))
            {
                printf("Vulkan depth stencil texture create failed\n");
                return;
            }

            VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            depthStencilViewCreateInfo.flags = 0;
            depthStencilViewCreateInfo.image = depthStencilTexture.handle;
            depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthStencilViewCreateInfo.format = depthStencilTexture.format;
            depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
            depthStencilViewCreateInfo.subresourceRange.levelCount = depthStencilTexture.levels;
            depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            depthStencilViewCreateInfo.subresourceRange.layerCount = depthStencilTexture.depthOrLayers;
            depthStencilViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (VK_FAILED(vkCreateImageView(device, &depthStencilViewCreateInfo, nullptr, &depthStencilView)))
            {
                printf("Vulkan depth stencil view create failed\n");
                return;
            }

            swapFramebuffers.reserve(swapImageViews.size());
            for (auto& swapView : swapImageViews)
            {
                VkExtent2D const swapExtent = swapchainCreateInfo.imageExtent;
                VkImageView attachments[] = { swapView, depthStencilView, };

                VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                framebufferCreateInfo.flags = 0;
                framebufferCreateInfo.renderPass = renderPass;
                framebufferCreateInfo.attachmentCount = SIZEOF_ARRAY(attachments);
                framebufferCreateInfo.pAttachments = attachments;
                framebufferCreateInfo.width = swapExtent.width;
                framebufferCreateInfo.height = swapExtent.height;
                framebufferCreateInfo.layers = 1;

                VkFramebuffer framebuffer = VK_NULL_HANDLE;
                vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer);
                assert(framebuffer != VK_NULL_HANDLE);
                swapFramebuffers.push_back(framebuffer);
            }
        }

        // Set viewport & scissor
        float viewportWidth = static_cast<float>(width);
        float viewportHeight = static_cast<float>(height);
        viewport = VkViewport{ 0.0F, viewportHeight, viewportWidth, -1.0F * viewportHeight, 0.0F, 1.0F }; // viewport height hack is needed because of OpenGL / Vulkan viewport differences
        scissor = VkRect2D{ VkOffset2D{ 0, 0 }, swapchainCreateInfo.imageExtent };

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

        // Update scene data
        camera.position = glm::vec3(2.0F, 2.0F, -5.0F);
        camera.forward = glm::normalize(glm::vec3(0.0F) - camera.position);

        transform.rotation = glm::rotate(transform.rotation, (float)frameTimer.deltaTimeMS() / 1000.0F, glm::vec3(0.0F, 1.0F, 0.0F));

        sceneData.cameraPosition = glm::vec3(0.0F, 0.0F, -5.0F);
        sceneData.viewproject = camera.matrix();
        sceneData.model = transform.matrix();
        sceneData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(sceneData.model))));

        // Update uniform buffer
        assert(sceneDataBuffer.mapped);
        memcpy(sceneDataBuffer.pData, &sceneData, sizeof(UniformSceneData));
    }

    void render()
    {
        // Acquire next backbuffer
        uint32_t backbufferIndex = 0;
        vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);
        switch (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapAvailable, VK_NULL_HANDLE, &backbufferIndex))
        {
        case VK_SUCCESS:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
            resize();
            return;
        default:
            isRunning = false;
            break;
        }

        vkResetFences(device, 1, &frameReady);
        vkResetCommandBuffer(commandBuffer, 0 /* no flags */);

        // Record frame commands
        {
            VkCommandBufferBeginInfo commandBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            commandBeginInfo.flags = 0;
            commandBeginInfo.pInheritanceInfo = nullptr;

            if (VK_FAILED(vkBeginCommandBuffer(commandBuffer, &commandBeginInfo)))
            {
                printf("Vulkan command buffer begin failed\n");
                isRunning = false;
                return;
            }

            VkExtent2D const swapExtent = swapchainCreateInfo.imageExtent;
            VkClearValue clearValues[] = {
                VkClearValue{{ 0.1F, 0.1F, 0.1F, 1.0F }},
                VkClearValue{{ 1.0F, 0x00 }},
            };

            VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = swapFramebuffers[backbufferIndex];
            renderPassBeginInfo.renderArea = VkRect2D{ VkOffset2D{ 0, 0 }, swapExtent };
            renderPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            VkBuffer vertexBuffers[] = { mesh.vertexBuffer.handle, };
            VkDeviceSize offsets[] = { 0, };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);

            vkCmdEndRenderPass(commandBuffer);

            if (VK_FAILED(vkEndCommandBuffer(commandBuffer)))
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
        submitInfo.pWaitSemaphores = &swapAvailable;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &swapReleased;

        if (VK_FAILED(vkQueueSubmit(directQueue, 1, &submitInfo, frameReady)))
        {
            printf("Vulkan queue submit failed\n");
            isRunning = false;
            return;
        }

        // Present current backbuffer
        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &swapReleased;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &backbufferIndex;
        presentInfo.pResults = nullptr;

        switch (vkQueuePresentKHR(directQueue, &presentInfo))
        {
        case VK_SUCCESS:
            break;
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
            resize();
            return;
        default:
            isRunning = false;
            break;
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
