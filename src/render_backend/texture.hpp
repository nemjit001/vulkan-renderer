#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

/// @brief GPU texture with associated data.
struct Texture
{
	/// @brief Destroy this texture.
	void destroy();

	/// @brief Initialize the default image view for this texture.
	/// @param viewType 
	/// @param aspectMask 
	/// @return 
	bool initDefaultView(VkImageViewType viewType, VkImageAspectFlags aspectMask);

	VkDevice device;
	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depthOrLayers;
	uint32_t levels;
};
