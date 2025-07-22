#pragma once

#define VK_CHECK(x)                                        \
do {                                                       \
VkResult err = x;                                          \
if (err) {                                                 \
printf("Detected Vulkan error: %s", string_VkResult(err)); \
abort();                                                   \
}                                                          \
} while (0)

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include "volk.h"
