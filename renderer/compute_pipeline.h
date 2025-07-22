#pragma once

#include "vv_vulkan.h"
#include "vk_mem_alloc.h"

struct ComputePipelineBuilder
{

};

struct ComputePipeline
{
    VkBuffer descriptor_buffer { VK_NULL_HANDLE };
    VmaAllocation descriptor_buffer_allocation { VK_NULL_HANDLE };
};