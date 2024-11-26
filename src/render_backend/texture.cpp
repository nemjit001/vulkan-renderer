#include "texture.hpp"

#include <volk.h>

#include "utils.hpp"

void Texture::destroy()
{
    if (device == VK_NULL_HANDLE) {
        return;
    }

    vkFreeMemory(device, memory, nullptr);
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, handle, nullptr);
}

bool Texture::initDefaultView(VkImageViewType viewType, VkImageAspectFlags aspectMask)
{
    VkImageViewCreateInfo viewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCreateInfo.image = handle;
    viewCreateInfo.viewType = viewType;
    viewCreateInfo.format = format;
    viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = levels;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = depthOrLayers;
    viewCreateInfo.subresourceRange.aspectMask = aspectMask;

    if (VK_FAILED(vkCreateImageView(device, &viewCreateInfo, nullptr, &view))) {
        return false;
    }

    return true;
}
