#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

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
