#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

/// @brief GPU buffer with associated data.
class Buffer
{
public:
	Buffer(VkDevice device, VkBuffer handle, VkDeviceMemory memory, size_t size);
	~Buffer();

	Buffer(Buffer const&) = delete;
	Buffer& operator=(Buffer const&) = delete;

	Buffer(Buffer&&) = delete;
	Buffer& operator=(Buffer&&) = delete;

	/// @brief Map buffer memory.
	void map();

	/// @brief Unmap buffer memory.
	void unmap();

	VkBuffer handle() const { return m_handle; }

	size_t size() const { return m_size; }

	bool mapped() const { return m_mapped; }

	void* data() const { return m_pData; }

private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkBuffer m_handle = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	size_t m_size = 0;
	bool m_mapped = false;
	void* m_pData = nullptr;
};
