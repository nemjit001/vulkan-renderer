#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#define SDL_MAIN_HANDLED
#define VOLK_IMPLEMENTATION
#include <SDL.h>
#include <SDL_vulkan.h>
#include <volk.h>

#define SIZEOF_ARRAY(val)   (sizeof((val)) / sizeof((val)[0]))
#define VK_FAILED(expr)     (expr != VK_SUCCESS)

constexpr char const* pWindowTitle = "Vulkan Renderer";
constexpr uint32_t DefaultWindowWidth = 1600;
constexpr uint32_t DefaultWindowHeight = 900;

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

VkRenderPass renderPass;
std::vector<VkFramebuffer> swapFramebuffers;

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
        swapFramebuffers.reserve(swapImageViews.size());
        for (auto& swapView : swapImageViews)
        {
            VkExtent2D const swapExtent = swapchainCreateInfo.imageExtent;
            VkImageView attachments[] = { swapView };

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

        // Create swap chain
        VkFormat preferredFormat = surfaceFormats[0].format;
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

        VkAttachmentReference colorAttachmentRefs[] = {
            VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        };

        VkSubpassDescription subpass{};
        subpass.flags = 0;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = nullptr;
        subpass.colorAttachmentCount = SIZEOF_ARRAY(colorAttachmentRefs);
        subpass.pColorAttachments = colorAttachmentRefs;
        subpass.pResolveAttachments = nullptr;
        subpass.pDepthStencilAttachment = nullptr;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        VkAttachmentDescription attachments[] = { colorAttachment, };
        VkSubpassDescription subpasses[] = { subpass };
        VkSubpassDependency dependencies[] = { dependency };
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
            VkImageView attachments[] = { swapView };

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

            VkClearValue clearValues[] = {
                VkClearValue{{ 0.3F, 0.6F, 0.9F, 1.0F }},
            };

            VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = swapFramebuffers[backbufferIndex];
            renderPassBeginInfo.renderArea = VkRect2D{ VkOffset2D{ 0, 0 }, swapchainCreateInfo.imageExtent };
            renderPassBeginInfo.clearValueCount = SIZEOF_ARRAY(clearValues);
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
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

    for (auto& framebuffer : swapFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    vkDestroyRenderPass(device, renderPass, nullptr);

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
