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

    Buffer create_buffer(const std::string& buffer_name, VkDeviceSize size, bool cpu_to_gpu = false);
    Buffer get_buffer(const std::string& buffer_name);

    void initialize();
    void terminate();
}