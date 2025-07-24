#pragma once

#include <vector>

#include "vv_vulkan.h"
#include "vk_mem_alloc.h"

#include "../../common/io.h"

struct ComputePipeline
{
    VkPipeline pipeline { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout descriptor_set_layout { VK_NULL_HANDLE };

    // Descriptor Buffer
    VkBuffer descriptor_buffer { VK_NULL_HANDLE };
    VmaAllocation descriptor_buffer_allocation { VK_NULL_HANDLE };

    VkDevice device { VK_NULL_HANDLE };

    VkDeviceSize push_constants_size { 0 };

    void dispatch(VkCommandBuffer command_buffer, u32 group_count_x, u32 group_count_y, u32 group_count_z, void* push_constants_data_ptr = nullptr);
    void destroy();
};

struct ComputePipelineBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> bindings {};

    /* TODO: Figure out a better way of doing this, for now
        all bindings that do not match the descriptor type
        insert a VK_NULL_HANDLE into each vector below

       TODO: Might be made slightly better when we access data via
        handles rather than directly through vulkan handles
    */

    std::vector<VkImageView> image_views {};
    std::vector<VkBuffer> buffers {};
    std::vector<VkDeviceSize> buffer_sizes;

    VkShaderModule shader_module { VK_NULL_HANDLE };
    VkDeviceSize push_constants_size { 0 };

    ComputePipelineBuilder(const std::filesystem::path& path);
    ComputePipelineBuilder& bind_storage_image(VkImageView image_view);
    ComputePipelineBuilder& bind_storage_buffer(VkBuffer buffer, VkDeviceSize buffer_size);
    ComputePipelineBuilder& set_push_constants_size(VkDeviceSize size);
    ComputePipeline create(VkDevice device);
};