#define _CRT_SECURE_NO_WARNINGS //< Used to silence C file IO function warnings

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#define SDL_MAIN_HANDLED
#define VOLK_IMPLEMENTATION
#include <SDL.h>
#include <SDL_vulkan.h>
#include <volk.h>

#define SIZEOF_ARRAY(val)   (sizeof((val)) / sizeof((val)[0]))
#define VK_FAILED(expr)     ((expr) != VK_SUCCESS)

constexpr char const* pWindowTitle = "Vulkan Renderer";
constexpr uint32_t DefaultWindowWidth = 1600;
constexpr uint32_t DefaultWindowHeight = 900;

/// @brief Vertex struct with interleaved per-vertex data.
struct Vertex
{
    float position[3];
    float color[3];
    float texCoord[2];
};

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

SDL_Window* pWindow;

VkInstance instance;
VkDebugUtilsMessengerEXT dbgMessenger;
VkSurfaceKHR surface;

VkPhysicalDevice physicalDevice;
VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
uint32_t directQueueFamily;

VkDevice device;
VkQueue directQueue;

VkSwapchainCreateInfoKHR swapchainCreateInfo;
VkSwapchainKHR swapchain;
std::vector<VkImage> swapImages;
std::vector<VkImageView> swapImageViews;

VkSemaphore swapAvailable;
VkSemaphore swapReleased;
VkFence frameReady;

VkCommandPool commandPool;
VkCommandBuffer commandBuffer;

VkImage depthStencilTarget;
VkDeviceMemory depthStencilMemory;
VkImageView depthStencilView;

VkRenderPass renderPass;
std::vector<VkFramebuffer> swapFramebuffers;

VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;

uint32_t vertexCount;
uint32_t indexCount;
VkBuffer vertexBuffer;
VkDeviceMemory vertexBufferMemory;
VkBuffer indexBuffer;
VkDeviceMemory indexBufferMemory;

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
    {
        vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);
    }

    // Destroy swap dependent resources
    {
        vkDestroyImageView(device, depthStencilView, nullptr);
        vkFreeMemory(device, depthStencilMemory, nullptr);
        vkDestroyImage(device, depthStencilTarget, nullptr);

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
            swapImageViews.push_back(view);
        }
    }

    // Recreate swap dependent resources
    {
        VkImageCreateInfo depthStencilTargetCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        depthStencilTargetCreateInfo.flags = 0;
        depthStencilTargetCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        depthStencilTargetCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        depthStencilTargetCreateInfo.extent = VkExtent3D{ swapchainCreateInfo.imageExtent.width, swapchainCreateInfo.imageExtent.height, 1 };
        depthStencilTargetCreateInfo.mipLevels = 1;
        depthStencilTargetCreateInfo.arrayLayers = 1;
        depthStencilTargetCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthStencilTargetCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthStencilTargetCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthStencilTargetCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthStencilTargetCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (VK_FAILED(vkCreateImage(device, &depthStencilTargetCreateInfo, nullptr, &depthStencilTarget)))
        {
            printf("Vulkan depth stencil create failed\n");
            return;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device, depthStencilTarget, &memoryRequirements);

        VkMemoryAllocateInfo imageAllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        imageAllocateInfo.allocationSize = memoryRequirements.size;
        imageAllocateInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (VK_FAILED(vkAllocateMemory(device, &imageAllocateInfo, nullptr, &depthStencilMemory)))
        {
            printf("Vulkan depth stencil memory allocation failed\n");
            return;
        }
        vkBindImageMemory(device, depthStencilTarget, depthStencilMemory, 0);

        VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        depthStencilViewCreateInfo.flags = 0;
        depthStencilViewCreateInfo.image = depthStencilTarget;
        depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilViewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
        depthStencilViewCreateInfo.subresourceRange.levelCount = 1;
        depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        depthStencilViewCreateInfo.subresourceRange.layerCount = 1;
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
            swapFramebuffers.push_back(framebuffer);
        }
    }
}

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    pWindow = SDL_CreateWindow(pWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DefaultWindowWidth, DefaultWindowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (pWindow == nullptr)
    {
        printf("SDL window create failed: %s\n", SDL_GetError());
        return 1;
    }

    if (VK_FAILED(volkInitialize()))
    {
        printf("Volk init failed\n");
        return 1;
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
        appInfo.apiVersion = VK_API_VERSION_1_0;

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
            return 1;
        }
        volkLoadInstanceOnly(instance);

#ifndef NDEBUG
        if (VK_FAILED(vkCreateDebugUtilsMessengerEXT(instance, &dbgMessengerCreateInfo, nullptr, &dbgMessenger)))
        {
            printf("Vulkan debug messenger create failed\n");
            return 1;
        }
#endif
    }

    // Create render surface
    {
        if (!SDL_Vulkan_CreateSurface(pWindow, instance, &surface))
        {
            printf("Vulkan surface create failed\n");
            return 1;
        }
    }

    // Find a physical device
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

        physicalDevice = physicalDevices[0];
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
        directQueueFamily = findQueueFamily(physicalDevice, surface, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT, 0);

        if (directQueueFamily == VK_QUEUE_FAMILY_IGNORED)
        {
            printf("Vulkan direct queue unavailable\n");
            return 1;
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

        VkDeviceCreateInfo deviceCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceCreateInfo.flags = 0;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &directQueueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = extensions.data();

        if (VK_FAILED(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)))
        {
            printf("Vulkan device create failed\n");
            return 1;
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
            return 1;
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
            return 1;
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
            return 1;
        }

        VkCommandBufferAllocateInfo commandBufAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufAllocInfo.commandPool = commandPool;
        commandBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufAllocInfo.commandBufferCount = 1;

        if (VK_FAILED(vkAllocateCommandBuffers(device, &commandBufAllocInfo, &commandBuffer)))
        {
            printf("Vulkan command buffer allocation failed\n");
            return 1;
        }
    }
    
    // Create depth stencil target
    {
        VkImageCreateInfo depthStencilTargetCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        depthStencilTargetCreateInfo.flags = 0;
        depthStencilTargetCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        depthStencilTargetCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        depthStencilTargetCreateInfo.extent = VkExtent3D{ swapchainCreateInfo.imageExtent.width, swapchainCreateInfo.imageExtent.height, 1 };
        depthStencilTargetCreateInfo.mipLevels = 1;
        depthStencilTargetCreateInfo.arrayLayers = 1;
        depthStencilTargetCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthStencilTargetCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthStencilTargetCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthStencilTargetCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depthStencilTargetCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (VK_FAILED(vkCreateImage(device, &depthStencilTargetCreateInfo, nullptr, &depthStencilTarget)))
        {
            printf("Vulkan depth stencil create failed\n");
            return 1;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device, depthStencilTarget, &memoryRequirements);

        VkMemoryAllocateInfo imageAllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        imageAllocateInfo.allocationSize = memoryRequirements.size;
        imageAllocateInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        if (VK_FAILED(vkAllocateMemory(device, &imageAllocateInfo, nullptr, &depthStencilMemory)))
        {
            printf("Vulkan depth stencil memory allocation failed\n");
            return 1;
        }
        vkBindImageMemory(device, depthStencilTarget, depthStencilMemory, 0);

        VkImageViewCreateInfo depthStencilViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        depthStencilViewCreateInfo.flags = 0;
        depthStencilViewCreateInfo.image = depthStencilTarget;
        depthStencilViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilViewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
        depthStencilViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        depthStencilViewCreateInfo.subresourceRange.baseMipLevel = 0;
        depthStencilViewCreateInfo.subresourceRange.levelCount = 1;
        depthStencilViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        depthStencilViewCreateInfo.subresourceRange.layerCount = 1;
        depthStencilViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (VK_FAILED(vkCreateImageView(device, &depthStencilViewCreateInfo, nullptr, &depthStencilView)))
        {
            printf("Vulkan depth stencil view create failed\n");
            return 1;
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

        VkSubpassDescription subpass{};
        subpass.flags = 0;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = nullptr;
        subpass.colorAttachmentCount = SIZEOF_ARRAY(colorAttachmentRefs);
        subpass.pColorAttachments = colorAttachmentRefs;
        subpass.pResolveAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depthStencilAttachmentRef;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        VkAttachmentDescription attachments[] = { colorAttachment, depthStencilAttachment, };
        VkSubpassDescription subpasses[] = { subpass, };
        VkSubpassDependency dependencies[] = { dependency, };
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
            return 1;
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
            swapFramebuffers.push_back(framebuffer);
        }
    }

    // Create pipelin layout & graphics pipeline
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutCreateInfo.flags = 0;
        pipelineLayoutCreateInfo.setLayoutCount = 0;
        pipelineLayoutCreateInfo.pSetLayouts = nullptr;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        if (VK_FAILED(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout)))
        {
            printf("Vulkan pipeline layout create failed\n");
            return 1;
        }

        std::vector<uint32_t> vertexShaderCode;
        if (!readShaderFile("static.vert.spv", vertexShaderCode))
        {
            printf("VK Renderer vertex shader read failed\n");
            return 1;
        }

        VkShaderModuleCreateInfo vertexShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        vertexShaderCreateInfo.flags = 0;
        vertexShaderCreateInfo.codeSize = static_cast<uint32_t>(vertexShaderCode.size() * sizeof(uint32_t));
        vertexShaderCreateInfo.pCode = vertexShaderCode.data();

        VkShaderModule vertexShader = VK_NULL_HANDLE;
        if (VK_FAILED(vkCreateShaderModule(device, &vertexShaderCreateInfo, nullptr, &vertexShader)))
        {
            printf("Vulkan vertex shader create failed\n");
            return 1;
        }

        std::vector<uint32_t> fragmentShaderCode;
        if (!readShaderFile("forward.frag.spv", fragmentShaderCode))
        {
            printf("VK Renderer fragment shader read failed\n");
            return 1;
        }

        VkShaderModuleCreateInfo fragmentShaderCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        fragmentShaderCreateInfo.flags = 0;
        fragmentShaderCreateInfo.codeSize = static_cast<uint32_t>(fragmentShaderCode.size() * sizeof(uint32_t));
        fragmentShaderCreateInfo.pCode = fragmentShaderCode.data();

        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        if (VK_FAILED(vkCreateShaderModule(device, &fragmentShaderCreateInfo, nullptr, &fragmentShader)))
        {
            printf("Vulkan vertex shader create failed\n");
            return 1;
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
            VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) },
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

        VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(swapchainCreateInfo.imageExtent.width), static_cast<float>(swapchainCreateInfo.imageExtent.height), 0.0F, 1.0F };
        VkRect2D scissor{ VkOffset2D{ 0, 0 }, swapchainCreateInfo.imageExtent };

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
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front =
            VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        depthStencilState.back =
            VkStencilOpState{ VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        depthStencilState.minDepthBounds = 0.0F;
        depthStencilState.maxDepthBounds = 0.0F;

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
            return 1;
        }

        vkDestroyShaderModule(device, fragmentShader, nullptr);
        vkDestroyShaderModule(device, vertexShader, nullptr);
    }

    // Create mesh data
    {
        Vertex vertices[] = {
            Vertex{ { -1.0F,  1.0F, 0.0F, }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F }, },
            Vertex{ {  1.0F,  1.0F, 0.0F, }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F }, },
            Vertex{ {  1.0F, -1.0F, 0.0F, }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 1.0F }, },
            Vertex{ { -1.0F, -1.0F, 0.0F, }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 1.0F }, },
        };

        uint32_t indices[] = {
            0, 1, 2,
            2, 3, 0,
        };

        vertexCount = SIZEOF_ARRAY(vertices);
        indexCount = SIZEOF_ARRAY(indices);

        VkBufferCreateInfo vertexBufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        vertexBufferCreateInfo.flags = 0;
        vertexBufferCreateInfo.size = sizeof(vertices);
        vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (VK_FAILED(vkCreateBuffer(device, &vertexBufferCreateInfo, nullptr, &vertexBuffer)))
        {
            printf("Vulkan vertex buffer create failed\n");
            return 1;
        }

        VkMemoryRequirements vertexBufferMemRequirements{};
        vkGetBufferMemoryRequirements(device, vertexBuffer, &vertexBufferMemRequirements);

        VkMemoryAllocateInfo vertexBufAllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        vertexBufAllocInfo.allocationSize = vertexBufferMemRequirements.size;
        vertexBufAllocInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, vertexBufferMemRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (VK_FAILED(vkAllocateMemory(device, &vertexBufAllocInfo, nullptr, &vertexBufferMemory)))
        {
            printf("Vulkan vertex buffer memory allocation failed\n");
            return 1;
        }
        vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

        VkBufferCreateInfo indexBufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        indexBufferCreateInfo.flags = 0;
        indexBufferCreateInfo.size = sizeof(indices);
        indexBufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        indexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (VK_FAILED(vkCreateBuffer(device, &indexBufferCreateInfo, nullptr, &indexBuffer)))
        {
            printf("Vulkan index buffer create failed\n");
            return 1;
        }

        VkMemoryRequirements indexBufferMemRequirements{};
        vkGetBufferMemoryRequirements(device, indexBuffer, &indexBufferMemRequirements);

        VkMemoryAllocateInfo indexBufAllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        indexBufAllocInfo.allocationSize = indexBufferMemRequirements.size;
        indexBufAllocInfo.memoryTypeIndex = getMemoryTypeIndex(deviceMemoryProperties, indexBufferMemRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (VK_FAILED(vkAllocateMemory(device, &indexBufAllocInfo, nullptr, &indexBufferMemory)))
        {
            printf("Vulkan index buffer memory allocation failed\n");
            return 1;
        }
        vkBindBufferMemory(device, indexBuffer, indexBufferMemory, 0);

        // Copy data to buffers
        void* pVertexBuffer = nullptr;
        vkMapMemory(device, vertexBufferMemory, 0, sizeof(vertices), 0, &pVertexBuffer);
        assert(pVertexBuffer != nullptr);
        memcpy(pVertexBuffer, vertices, sizeof(vertices));
        vkUnmapMemory(device, vertexBufferMemory);

        void* pIndexBuffer = nullptr;
        vkMapMemory(device, indexBufferMemory, 0, sizeof(indices), 0, &pIndexBuffer);
        assert(pIndexBuffer != nullptr);
        memcpy(pIndexBuffer, indices, sizeof(indices));
        vkUnmapMemory(device, indexBufferMemory);
    }

    bool isRunning = true;
    while (isRunning)
    {
        // Update SDL state
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
            continue;
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
                return 1;
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

            VkViewport viewport{ 0.0F, 0.0F, static_cast<float>(swapExtent.width), static_cast<float>(swapExtent.height), 0.0F, 1.0F };
            VkRect2D scissor{ VkOffset2D{ 0, 0 }, swapExtent };
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            VkBuffer vertexBuffers[] = { vertexBuffer, };
            VkDeviceSize offsets[] = { 0, };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);

            vkCmdEndRenderPass(commandBuffer);

            if (VK_FAILED(vkEndCommandBuffer(commandBuffer)))
            {
                printf("Vulkan command buffer end failed\n");
                return 1;
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
            return 1;
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
            continue;
        default:
            isRunning = false;
            break;
        }
    }

    vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);

    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    for (auto& framebuffer : swapFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyImageView(device, depthStencilView, nullptr);
    vkFreeMemory(device, depthStencilMemory, nullptr);
    vkDestroyImage(device, depthStencilTarget, nullptr);

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
    return 0;
}
