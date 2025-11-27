#pragma once
#include <string>

#include "vv_vulkan.h"
#include "vk_mem_alloc.h"

namespace DeviceResources
{
    struct Buffer
    {
        VkBuffer handle { VK_NULL_HANDLE };
        VkDeviceSize size { 0 };
        VmaAllocation allocation { VK_NULL_HANDLE };
    };

    Buffer create_buffer(const std::string& buffer_name, VkDeviceSize size);
    Buffer get_buffer(const std::string& buffer_name);
    void immediate_copy_data_to_gpu(const std::string& buffer_name, void* data, VkDeviceSize size_in_bytes);

    void initialize();
    void terminate();
}
