#include "device_resources.h"
#include "renderer_core.h"
#include <unordered_map>

enum FunctionQueueLifetime
{
    CORE,
    RANGE
};

#include "../../common/function_queue.h"

struct
{
    std::unordered_map<std::string, DeviceResources::Buffer> buffers;
} internal;

DeviceResources::Buffer DeviceResources::create_buffer(const std::string& buffer_name, VkDeviceSize size)
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
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

void DeviceResources::immediate_copy_data_to_gpu(const std::string& buffer_name, void* data, VkDeviceSize size_in_bytes)
{
    Buffer staging_buffer {};

    VkBufferCreateInfo buffer_create_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size_in_bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VmaAllocationCreateInfo vma_allocation_create_info
    {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_allocation_create_info, &staging_buffer.handle, &staging_buffer.allocation, nullptr);

    staging_buffer.size = size_in_bytes;

    void* mapped_data;

    vmaMapMemory(Renderer::Core::get_vma_allocator(), staging_buffer.allocation, &mapped_data);
    memcpy(mapped_data, data, size_in_bytes);
    vmaUnmapMemory(Renderer::Core::get_vma_allocator(), staging_buffer.allocation);

    Renderer::Core::submit_immediate_command([=](VkCommandBuffer cmd) {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = size_in_bytes;
        vkCmdCopyBuffer(cmd, staging_buffer.handle, DeviceResources::get_buffer(buffer_name).handle, 1, &copy);
    });

    vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), staging_buffer.handle, staging_buffer.allocation);
}


void DeviceResources::initialize()
{

}

void DeviceResources::terminate()
{
    for (auto& buffer : internal.buffers)
        vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), buffer.second.handle, buffer.second.allocation);

    QUEUE_FLUSH(CORE);
}
