#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

struct TextureSize
{
	uint32_t width;
	uint32_t height;
	uint32_t depthOrLayers;
};

/// @brief GPU texture with associated data.
class Texture
{
public:
	Texture(
		VkDevice device,
		VkImage handle, 
		VkDeviceMemory memory,
		VkFormat format,
		TextureSize const& size,
		uint32_t levels
	);
	~Texture();

	Texture(Texture const&) = delete;
	Texture& operator=(Texture const&) = delete;

	Texture(Texture&&) = delete;
	Texture& operator=(Texture&&) = delete;

	/// @brief Initialize the default image view for this texture.
	/// @param viewType 
	/// @param aspectMask 
	/// @return 
	bool initDefaultView(VkImageViewType viewType, VkImageAspectFlags aspectMask);

	// TODO(nemjit001): Make members private
	VkDevice device = VK_NULL_HANDLE;
	VkImage handle = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	TextureSize size{};
	uint32_t levels = 0;
};
