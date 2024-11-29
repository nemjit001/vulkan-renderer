#include "buffer.hpp"

#include <cassert>

#include <volk.h>

Buffer::Buffer(VkDevice device, VkBuffer handle, VkDeviceMemory memory, size_t size)
    :
    m_device(device),
    m_handle(handle),
    m_memory(memory),
    m_size(size)
{
    //
}

Buffer::~Buffer()
{
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    if (m_mapped) {
        unmap();
    }

    vkFreeMemory(m_device, m_memory, nullptr);
    vkDestroyBuffer(m_device, m_handle, nullptr);
}

void Buffer::map()
{
    m_pData = nullptr;
    vkMapMemory(m_device, m_memory, 0, m_size, 0, &m_pData);
    assert(m_pData != nullptr);

    m_mapped = true;
}

void Buffer::unmap()
{
    vkUnmapMemory(m_device, m_memory);
    m_mapped = false;
}
