#include "renderer.hpp"

#include <cassert>
#include <stdexcept>

#include <SDL_vulkan.h>
#include <volk.h>

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

RenderDeviceContext::RenderDeviceContext(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t windowWidth, uint32_t windowHeight)
    :
    physicalDevice(physicalDevice),
    surface(surface)
{
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    directQueueFamily = findQueueFamily(physicalDevice, surface, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT, 0);

    if (directQueueFamily == VK_QUEUE_FAMILY_IGNORED) {
        throw std::runtime_error("Vulkan direct queue unavailable");
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

        if (VK_FAILED(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device))) {
            throw std::runtime_error("Vulkan device create failed");
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
            VkExtent2D{ windowWidth, windowHeight } : surfaceCaps.currentExtent;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.preTransform = surfaceCaps.currentTransform;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainCreateInfo.clipped = VK_FALSE;
        swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

        if (VK_FAILED(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain))) {
            throw std::runtime_error("Vulkan swap chain create failed");
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

        // Create depth stencil target
        if (!createTexture(
            depthStencilTexture,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            swapchainCreateInfo.imageExtent.width, swapchainCreateInfo.imageExtent.height, 1
        )) {
            throw std::runtime_error("Vulkan depth stencil texture create failed");
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

        if (VK_FAILED(vkCreateImageView(device, &depthStencilViewCreateInfo, nullptr, &depthStencilView))) {
            throw std::runtime_error("Vulkan depth stencil view create failed");
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
            throw std::runtime_error("Vulkan sync primitive create failed");
        }
    }

    // Create command pool & command buffer
    {
        VkCommandPoolCreateInfo commandPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex = directQueueFamily;

        if (VK_FAILED(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool)))
        {
            throw std::runtime_error("Vulkan command pool create failed");
        }

        VkCommandBufferAllocateInfo commandBufAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufAllocInfo.commandPool = commandPool;
        commandBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufAllocInfo.commandBufferCount = 1;

        if (VK_FAILED(vkAllocateCommandBuffers(device, &commandBufAllocInfo, &commandBuffer)))
        {
            throw std::runtime_error("Vulkan command buffer allocation failed\n");
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
            throw std::runtime_error("Vulkan render pass create failed\n");
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
}

RenderDeviceContext::~RenderDeviceContext()
{
    for (auto& framebuffer : swapFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vkDestroyRenderPass(device, renderPass, nullptr);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyFence(device, frameReady, nullptr);
    vkDestroySemaphore(device, swapReleased, nullptr);
    vkDestroySemaphore(device, swapAvailable, nullptr);

    vkDestroyImageView(device, depthStencilView, nullptr);
    depthStencilTexture.destroy();
    for (auto& view : swapImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyDevice(device, nullptr);
}

bool RenderDeviceContext::newFrame()
{
    switch (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapAvailable, VK_NULL_HANDLE, &backbufferIndex))
    {
    case VK_SUCCESS:
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
        return false;
    default:
        throw std::runtime_error("Vulkan fatal swap chain error (backbuffer acquire)");
        break;
    }

    return true;
}

bool RenderDeviceContext::present()
{
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
        return false;
    default:
        throw std::runtime_error("Vulkan fatal swap chain error (presentation)");
        break;
    }

    return true;
}

bool RenderDeviceContext::resizeSwapResources(uint32_t width, uint32_t height)
{
    // Wait for previous frame
    vkWaitForFences(device, 1, &frameReady, VK_TRUE, UINT64_MAX);

    // Destroy swap dependent resources
    {
        for (auto& framebuffer : swapFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        swapFramebuffers.clear();
    }

    // Recreate swap chain & swap resources
    {
        // Destroy depth stencil texture & view
        vkDestroyImageView(device, depthStencilView, nullptr);
        depthStencilTexture.destroy();

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
            VkExtent2D{ width, height } : surfaceCaps.currentExtent;
        swapchainCreateInfo.oldSwapchain = swapchain;

        if (VK_FAILED(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain)))
        {
            printf("Vulkan swap chain recreate failed\n");
            return false;
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

        // Create depth stencil texture & view
        if (!createTexture(
            depthStencilTexture,
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

    // Recreate swap dependent resources
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

    return true;
}

bool RenderDeviceContext::createBuffer(
    Buffer& buffer,
    size_t size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    bool createMapped
)
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
    allocateInfo.memoryTypeIndex = getMemoryTypeIndex(memRequirements, properties);

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

bool RenderDeviceContext::createTexture(
    Texture& texture,
    VkImageType imageType,
    VkFormat format,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t levels,
    uint32_t layers,
    VkSampleCountFlagBits samples,
    VkImageTiling tiling,
    VkImageLayout initialLayout
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
    allocateInfo.memoryTypeIndex = getMemoryTypeIndex(memRequirements, properties);

    if (VK_FAILED(vkAllocateMemory(device, &allocateInfo, nullptr, &texture.memory)))
    {
        return false;
    }
    vkBindImageMemory(device, texture.handle, texture.memory, 0);

    return true;
}

uint32_t RenderDeviceContext::getCurrentBackbufferIndex() const
{
    return backbufferIndex;
}

uint32_t RenderDeviceContext::findQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkQueueFlags flags, VkQueueFlags exclude)
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

uint32_t RenderDeviceContext::getMemoryTypeIndex(VkMemoryRequirements const& requirements, VkMemoryPropertyFlags propertyFlags) const
{
    for (uint32_t memIdx = 0; memIdx < memoryProperties.memoryTypeCount; memIdx++)
    {
        uint32_t const memoryTypeBits = (1 << memIdx);

        if ((requirements.memoryTypeBits & memoryTypeBits) != 0
            && (memoryProperties.memoryTypes[memIdx].propertyFlags & propertyFlags) == propertyFlags)
        {
            return memIdx;
        }
    }

    printf("Failed to find memory type index for memory requirements and propry flags combination\n");
    return ~0U;
}

namespace Renderer
{
    SDL_Window* pAssociatedWindow = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT dbgMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

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

    bool init(SDL_Window* pWindow)
    {
        assert(pWindow != nullptr);
        pAssociatedWindow = pWindow;

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

        return true;
    }

    void shutdown()
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
#ifndef NDEBUG
        vkDestroyDebugUtilsMessengerEXT(instance, dbgMessenger, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
        volkFinalize();
    }

    VkInstance getInstance()
    {
        assert(instance != VK_NULL_HANDLE);
        return instance;
    }

    RenderDeviceContext* pickRenderDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
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
            return nullptr;
        }

        int width = 0, height = 0;
        SDL_GetWindowSize(pAssociatedWindow, &width, &height);
        return new RenderDeviceContext(physicalDevice, surface, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}
