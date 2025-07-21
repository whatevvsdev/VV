#include "renderer.h"
#include "renderer_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#include "../common/types.h"
#include "../common/io.h"

#include "vv_vulkan.h"
#include <vulkan/vk_enum_string_helper.h>

#include "imgui.h"
#include "SDL3/SDL_vulkan.h"

enum FunctionQueueLifetime
{
    CORE,
    RANGE
};
#include "../common/function_queue.h"

struct
{
    VkPipelineLayout compute_pipeline_layout { VK_NULL_HANDLE };
    VkPipeline compute_pipeline { VK_NULL_HANDLE };

    Renderer::AllocatedImage draw_image {};

    VkDescriptorSetLayout _drawImageDescriptorLayout;

    VkSampler draw_image_sampler { VK_NULL_HANDLE };
    VkBuffer draw_image_descriptor_buffer { VK_NULL_HANDLE };
    VmaAllocation draw_image_descriptor_buffer_allocation { VK_NULL_HANDLE };
    VkDeviceSize draw_image_descriptor_layout_size { 0 };
    VkDeviceSize draw_image_set_layout_descriptor_offset { 0 };
} state;

VkShaderModule create_shader_module(const std::vector<u8>& bytecode)
{
    VkShaderModuleCreateInfo shader_module_create_info
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytecode.size() * sizeof(u8),
        .pCode = reinterpret_cast<const u32*>(bytecode.data()),
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;

    VK_CHECK(vkCreateShaderModule(Renderer::Core::get_logical_device(), &shader_module_create_info, nullptr, &shader_module));

    return shader_module;
}

struct DescriptorLayoutBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void add_binding(uint32_t binding, VkDescriptorType type);
    void clear();
    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
    for (auto& b : bindings)
        b.stageFlags |= shaderStages;

    VkDescriptorSetLayoutCreateInfo info =
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    };
    info.pNext = pNext;

    info.pBindings = bindings.data();
    info.bindingCount = (uint32_t)bindings.size();
    info.flags = flags | VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;


    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, vkDestroyDescriptorSetLayout(device, set, nullptr));

    return set;
}

void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind {};
    newbind.binding = binding;
    newbind.descriptorCount = 1;
    newbind.descriptorType = type;

    bindings.push_back(newbind);
}

void DescriptorLayoutBuilder::clear()
{
    bindings.clear();
}

void create_compute_pipeline()
{
    auto comp_binary = IO::read_binary_file("shaders/gradient.comp.spv");

    if (comp_binary.empty())
    {
        printf("Failed to read .comp compiled binary file.\n");
        return;
    }

    auto comp_shader_module = create_shader_module(comp_binary);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        state._drawImageDescriptorLayout = builder.build(Renderer::Core::get_logical_device(), VK_SHADER_STAGE_COMPUTE_BIT);
    }

    auto descriptor_buffer_properties = Renderer::Core::get_physical_device_properties().descriptor_buffer_properties;

    // Get the size of the descriptor set and align it properly
    vkGetDescriptorSetLayoutSizeEXT(Renderer::Core::get_logical_device(), state._drawImageDescriptorLayout, &state.draw_image_descriptor_layout_size);
    state.draw_image_descriptor_layout_size = aligned_size(state.draw_image_descriptor_layout_size, descriptor_buffer_properties.descriptorBufferOffsetAlignment);

    vkGetDescriptorSetLayoutBindingOffsetEXT(Renderer::Core::get_logical_device(), state._drawImageDescriptorLayout, 0u, &state.draw_image_set_layout_descriptor_offset);

    VkBufferCreateInfo buffer_create_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = state.draw_image_descriptor_layout_size,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };

    VmaAllocationCreateInfo vma_create_info
    {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    // Create a buffer to hold the set
    VK_CHECK(vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_create_info, &state.draw_image_descriptor_buffer, &state.draw_image_descriptor_buffer_allocation, nullptr));
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), state.draw_image_descriptor_buffer, state.draw_image_descriptor_buffer_allocation));

    VkDescriptorImageInfo image_descriptor{};
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_descriptor.imageView = state.draw_image.view;
    image_descriptor.sampler = VK_NULL_HANDLE;//state.draw_image_sampler;

    VkDescriptorGetInfoEXT image_descriptor_info
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    };

    image_descriptor_info.data.pCombinedImageSampler = &image_descriptor;

    void* mapped_ptr = nullptr;
    VK_CHECK(vmaMapMemory(Renderer::Core::get_vma_allocator(), state.draw_image_descriptor_buffer_allocation, &mapped_ptr));

    vkGetDescriptorEXT(Renderer::Core::get_logical_device(), &image_descriptor_info, descriptor_buffer_properties.combinedImageSamplerDescriptorSize, mapped_ptr);

    vmaUnmapMemory(Renderer::Core::get_vma_allocator(), state.draw_image_descriptor_buffer_allocation);

    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &state._drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(Renderer::Core::get_logical_device(), &computeLayout, nullptr, &state.compute_pipeline_layout));
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, vkDestroyPipelineLayout(Renderer::Core::get_logical_device(), state.compute_pipeline_layout, nullptr));

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = comp_shader_module;
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = state.compute_pipeline_layout;
    computePipelineCreateInfo.stage = stageinfo;
    computePipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

    VK_CHECK(vkCreateComputePipelines(Renderer::Core::get_logical_device(), nullptr, 1, &computePipelineCreateInfo, nullptr, &state.compute_pipeline))  ;
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, vkDestroyPipeline(Renderer::Core::get_logical_device(), state.compute_pipeline, nullptr));
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), comp_shader_module, nullptr);
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    Core::initialize(sdl_window_ptr);
    auto swapchain_data = Core::get_swapchain_data();

    state.draw_image = Renderer::Core::create_image(swapchain_data.surface_extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "compute_draw_image");

    create_compute_pipeline();
}

void transition_image_layout(VkCommandBuffer cmd_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags2 src_access_mask, VkAccessFlags2 dst_access_mask, VkPipelineStageFlags2 src_stage_mask, VkPipelineStageFlags2 dst_stage_mask)
{
    VkImageMemoryBarrier2 barrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkDependencyInfo dependency_info
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(cmd_buffer, &dependency_info);
}

void copy_image_to_image(VkCommandBuffer cmd_buffer, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize)
{
    VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

    blitRegion.srcOffsets[1].x = srcSize.width;
    blitRegion.srcOffsets[1].y = srcSize.height;
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.dstOffsets[1].x = dstSize.width;
    blitRegion.dstOffsets[1].y = dstSize.height;
    blitRegion.dstOffsets[1].z = 1;

    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
    blitInfo.dstImage = destination;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.srcImage = source;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(cmd_buffer, &blitInfo);
}

void Renderer::update()
{
    auto per_frame_data = Renderer::Core::begin_frame();
    auto swapchain_data = Renderer::Core::get_swapchain_data();

    transition_image_layout(per_frame_data.command_buffer,
       per_frame_data.swapchain_image,
       VK_IMAGE_LAYOUT_UNDEFINED,
       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
       {},
       VK_ACCESS_2_TRANSFER_WRITE_BIT,
       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    transition_image_layout(per_frame_data.command_buffer,
       state.draw_image.image,
       VK_IMAGE_LAYOUT_UNDEFINED,
       VK_IMAGE_LAYOUT_GENERAL,
       {},
       VK_ACCESS_2_SHADER_WRITE_BIT,
       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
   );

    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(per_frame_data.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.compute_pipeline);

    VkBufferDeviceAddressInfo buffer_device_address_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = state.draw_image_descriptor_buffer,
    };

    VkDeviceAddress address = vkGetBufferDeviceAddress(Renderer::Core::get_logical_device(), &buffer_device_address_info);

    VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .address = address,
        .usage   = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
    };

    vkCmdBindDescriptorBuffersEXT(per_frame_data.command_buffer, 1, &descriptor_buffer_binding_info);

    // We only have one buffer (specifically for images, because you have to separate UBO and Image)
    uint32_t buffer_index_image = 0;
    VkDeviceSize buffer_offset = 0;
    vkCmdSetDescriptorBufferOffsetsEXT(per_frame_data.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.compute_pipeline_layout, 0, 1, &buffer_index_image, &buffer_offset);

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(per_frame_data.command_buffer, std::ceil(swapchain_data.surface_extent.width / 16.0), std::ceil(swapchain_data.surface_extent.height / 16.0), 1);

    transition_image_layout(per_frame_data.command_buffer,
       state.draw_image.image,
       VK_IMAGE_LAYOUT_GENERAL,
       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
       {},
       VK_ACCESS_2_TRANSFER_READ_BIT,
       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
       VK_PIPELINE_STAGE_2_TRANSFER_BIT
   );

    copy_image_to_image(per_frame_data.command_buffer, state.draw_image.image, per_frame_data.swapchain_image, swapchain_data.surface_extent, swapchain_data.surface_extent);

   transition_image_layout(per_frame_data.command_buffer,
       per_frame_data.swapchain_image,
       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
       VK_ACCESS_2_TRANSFER_WRITE_BIT,
       {},
       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
       VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
   );

    ImGui::ShowDemoWindow();

    Renderer::Core::end_frame();
}

void Renderer::terminate()
{
    QUEUE_FLUSH(FunctionQueueLifetime::CORE);

    Renderer::Core::terminate();
}