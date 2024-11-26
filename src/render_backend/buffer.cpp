#include "buffer.hpp"

#include <cassert>

#include <volk.h>

void Buffer::destroy()
{
    if (device == VK_NULL_HANDLE) {
        return;
    }

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
