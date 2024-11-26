#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define SIZEOF_ARRAY(val)   (sizeof((val)) / sizeof((val)[0]))
#define VK_FAILED(expr)     ((expr) != VK_SUCCESS)
