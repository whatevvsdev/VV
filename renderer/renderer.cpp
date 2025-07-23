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
#include <glm/mat4x4.hpp> // glm::mat4

#include "compute_pipeline.h"
#include "cameras.h"

enum FunctionQueueLifetime
{
    CORE,
    RANGE
};
#include "../common/function_queue.h"

struct
{
    ComputePipeline raygen_pipeline;
    ComputePipeline intersect_pipeline;

    VkBuffer raygen_buffer;
    VkDeviceSize raygen_buffer_size = sizeof(glm::vec4) * 1280 * 720;
    VmaAllocation raygen_buffer_allocation;

    Renderer::AllocatedImage draw_image {};
} state;

struct
{
    glm::mat4 camera_matrix { glm::mat4(1) };
} compute_push_constants;

void create_raygen_pipeline()
{
    VkBufferCreateInfo buffer_create_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = state.raygen_buffer_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
    };

    VmaAllocationCreateInfo vma_allocation_create_info
    {
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_allocation_create_info, &state.raygen_buffer, &state.raygen_buffer_allocation, nullptr);
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), state.raygen_buffer, state.raygen_buffer_allocation));

    state.raygen_pipeline = ComputePipelineBuilder("shaders/rt_raygen.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer(state.raygen_buffer, state.raygen_buffer_size)
        .set_push_constants_size(sizeof(compute_push_constants))
        .create(Renderer::Core::get_logical_device());

    /* TODO: When we are hot-reloading and live reconstructing the pipelines,
        we cannot rely on the deletion queue (unless we can specify a key to
        remove the pipline from it if we have to destroy the pipeline early)
    */
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, state.raygen_pipeline.destroy());
}

void create_intersection_pipeline()
{
    state.intersect_pipeline = ComputePipelineBuilder("shaders/rt_intersect.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer(state.raygen_buffer, state.raygen_buffer_size)
        .set_push_constants_size(sizeof(compute_push_constants))
        .create(Renderer::Core::get_logical_device());

    /* TODO: When we are hot-reloading and live reconstructing the pipelines,
        we cannot rely on the deletion queue (unless we can specify a key to
        remove the pipline from it if we have to destroy the pipeline early)
    */
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, state.intersect_pipeline.destroy());
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    Core::initialize(sdl_window_ptr);
    auto swapchain_data = Core::get_swapchain_data();

    state.draw_image = Renderer::Core::create_image(swapchain_data.surface_extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "compute_draw_image");

    create_raygen_pipeline();
    create_intersection_pipeline();
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

void Renderer::begin_frame()
{
    auto per_frame_data = Renderer::Core::begin_frame();
}


void Renderer::end_frame()
{
    auto per_frame_data = Renderer::Core::get_current_frame_data();
    auto swapchain_data = Renderer::Core::get_swapchain_data();

    compute_push_constants.camera_matrix = Renderer::Cameras::get_current_camera_data_copy().camera_matrix;

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

    u32 dispatch_width = std::ceil(swapchain_data.surface_extent.width / 16.0);
    u32 dispatch_height = std::ceil(swapchain_data.surface_extent.height / 16.0);
    state.raygen_pipeline.dispatch(per_frame_data.command_buffer, dispatch_width, dispatch_height, 1, &compute_push_constants);
    state.intersect_pipeline.dispatch(per_frame_data.command_buffer, dispatch_width, dispatch_height, 1, &compute_push_constants);

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