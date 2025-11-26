#include "device_resources.h"
#include "renderer_core.h"
#include <unordered_map>

struct
{
    std::unordered_map<std::string, DeviceResources::Buffer> buffers;
} internal;

DeviceResources::Buffer DeviceResources::create_buffer(const std::string& buffer_name, VkDeviceSize size, bool cpu_to_gpu)
{
    auto existing_entry = internal.buffers.find(buffer_name);
    if (existing_entry == internal.buffers.end())
    {
        Buffer created_buffer {};

        VkBufferCreateInfo buffer_create_info
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
        };

        VmaAllocationCreateInfo vma_allocation_create_info
        {
            // TODO: VMA_MEMORY_USAGE_CPU_TO_GPU is deprecated
            .flags = cpu_to_gpu ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        
        vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_allocation_create_info, &created_buffer.handle, &created_buffer.allocation, nullptr);

        created_buffer.size = size;
        internal.buffers[buffer_name] = created_buffer;
        return created_buffer;
    }
    printf("Creating already existing buffer (%s)? Maybe you meant to resize it instead?", buffer_name.c_str());

    return existing_entry->second;
}

DeviceResources::Buffer DeviceResources::get_buffer(const std::string& buffer_name)
{
    return internal.buffers.find(buffer_name)->second;
}

void DeviceResources::initialize()
{
    
}

void DeviceResources::terminate()
{
    for (auto& buffer : internal.buffers)
        vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), buffer.second.handle, buffer.second.allocation);
}
