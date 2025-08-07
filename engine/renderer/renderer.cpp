#include "renderer.h"
#include "renderer_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#include "../../common/types.h"
#include "../../common/io.h"

#include "vv_vulkan.h"
#include <vulkan/vk_enum_string_helper.h>

#include "imgui.h"
#include "SDL3/SDL_vulkan.h"
#include <glm/mat4x4.hpp> // glm::mat4

#include "../data/voxel_model.h"
#include "device_resources.h"
#include "compute_pipeline.h"
#include "profiling.h"
#include "cameras.h"

#define OGT_VOX_IMPLEMENTATION
#include <ogt_vox.h>

enum FunctionQueueLifetime
{
    CORE,
    RANGE
};
#include "../../common/function_queue.h"

/* TODO:
    This and its usage is a bit of a mess
*/
#define HOTRELOAD 1
#if HOTRELOAD
#define HOTRELOAD_WORKING_DIRECTORY "../"
// Have to use \\ for windows
#define SHADER_COMPILE_SCRIPT_PATH "\"..\\compile_shaders.bat\""
#define SHADER_SOURCE_PATH HOTRELOAD_WORKING_DIRECTORY "shaders/compute/"
#define SHADER_COMPILED_PATH HOTRELOAD_WORKING_DIRECTORY "shaders/spirv-out/"
#else
#define SHADER_COMPILED_PATH shaders/
#endif

struct
{
    ComputePipeline raygen_pipeline;
    ComputePipeline intersect_pipeline;

    //VVBuffer voxel_model_buffer;
    //VVBuffer raygen_buffer;

    Renderer::AllocatedImage draw_image {};
} state;

struct
{
    glm::mat4 camera_matrix { glm::mat4(1) };
} compute_push_constants;

void create_raygen_pipeline()
{
    auto& extent = Renderer::Core::get_swapchain_data().surface_extent;

    DeviceResources::create_buffer("raygen_buffer", sizeof(glm::vec4) * extent.width * extent.height);

    state.raygen_pipeline = ComputePipelineBuilder(SHADER_COMPILED_PATH "rt_raygen.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer("raygen_buffer")
        .set_push_constants_size(sizeof(compute_push_constants))
        .create(Renderer::Core::get_logical_device());

    /* TODO: When we are hot-reloading and live reconstructing the pipelines,
        we cannot rely on the deletion queue (unless we can specify a key to
        remove the pipline from it if we have to destroy the pipeline early)
    */
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, state.raygen_pipeline.destroy());
}

void load_voxel_data()
{
    VoxelModels::load("../stanford-dragon.vox");
    VoxelModels::upload_models_to_gpu();
}

void create_intersection_pipeline()
{
    state.intersect_pipeline = ComputePipelineBuilder( SHADER_COMPILED_PATH "rt_intersect.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer("raygen_buffer")
        .bind_storage_buffer("voxel_data")
        .set_push_constants_size(sizeof(compute_push_constants))
        .create(Renderer::Core::get_logical_device());

#if HOTRELOAD
    IO::watch_for_file_update(SHADER_SOURCE_PATH "rt_intersect.comp",
        []()
        {
            system(SHADER_COMPILE_SCRIPT_PATH);

            state.intersect_pipeline.destroy();
            state.intersect_pipeline = ComputePipelineBuilder(SHADER_COMPILED_PATH "rt_intersect.comp.spv")
                .bind_storage_image(state.draw_image.view)
                .bind_storage_buffer("raygen_buffer")
                .bind_storage_buffer("voxel_data")
                .set_push_constants_size(sizeof(compute_push_constants))
                .create(Renderer::Core::get_logical_device());
        });
#endif
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    Core::initialize(sdl_window_ptr);
    auto swapchain_data = Core::get_swapchain_data();

    state.draw_image = Renderer::Core::create_image(swapchain_data.surface_extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "compute_draw_image");

    create_raygen_pipeline();
    load_voxel_data();
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

    static bool display_cpu_queries = true;
    static bool display_gpu_queries = true;

    ImGui::SetWindowPos(ImVec2(0, 0));
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("Menu"))
    {
        //ImGui::MenuItem("(demo menu)", NULL, false, false);
        if (ImGui::MenuItem("Open", nullptr, false, false))
        {
            // Todo: Add loading model options here
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Options"))
        {
            // ImGui::MenuItem("Enabled", "", &enabled);
            ImGui::Checkbox("CPU Profiling queries", &display_cpu_queries);
            ImGui::Checkbox("GPU Profiling queries", &display_gpu_queries);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4"))
        {
            exit(0);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    ImGui::Begin("## Toggle mouse cursor hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::TextColored(ImVec4(0.0, 0.0, 0.0, 1.0), "Press TAB to toggle mouse cursor.");
    ImGui::SetCursorPos(ImVec2(9, 9));
    ImGui::Text("Press TAB to toggle mouse cursor.");
    ImGui::End();

    if (display_gpu_queries)
    {
        ImGui::Begin("GPU Timings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
        auto all_gpu_timings = ProfilingQueries::get_all_device_times_elapsed_ms();
        for (auto& timing : all_gpu_timings)
        {
            ImGui::Text("%s 10 avg time: %.2fms", timing.name.c_str(), timing.average_10_time_ms);
            ImGui::Text("%s        time: %.2fms", timing.name.c_str(), timing.time_ms);
        }
        ImGui::End();
    }

    if (display_cpu_queries)
    {
        ImGui::Begin("CPU Timings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
        auto all_cpu_timings = ProfilingQueries::get_all_host_times_elapsed_ms();
        for (auto& timing : all_cpu_timings)
        {
            ImGui::Text("%s 10 avg time: %.2fms", timing.name.c_str(), timing.average_10_time_ms);
            ImGui::Text("%s        time: %.2fms", timing.name.c_str(), timing.time_ms);
        }
        ImGui::End();
    }
}

void Renderer::end_frame()
{
    auto per_frame_data = Renderer::Core::get_current_frame_data();
    auto swapchain_data = Renderer::Core::get_swapchain_data();
    ProfilingQueries::host_start("frame submit");
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

    ProfilingQueries::device_start("raygen", per_frame_data.command_buffer);
    state.raygen_pipeline.dispatch(per_frame_data.command_buffer, dispatch_width, dispatch_height, 1, &compute_push_constants);
    ProfilingQueries::device_stop("raygen", per_frame_data.command_buffer);

    ProfilingQueries::device_start("intersect", per_frame_data.command_buffer);
    state.intersect_pipeline.dispatch(per_frame_data.command_buffer, dispatch_width, dispatch_height, 1, &compute_push_constants);
    ProfilingQueries::device_stop("intersect", per_frame_data.command_buffer);

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

    ProfilingQueries::host_stop("frame submit");
    Renderer::Core::end_frame();
}

void Renderer::terminate()
{
    /* TODO: doing manually because of hotreloading,
        we currently need to destroy and create pipelines before
        FunctionQueueLifetime::CORE lifetime is up. Maybe some
        key system to remove stuff from the queue if need be?
    */
    state.intersect_pipeline.destroy();
    QUEUE_FLUSH(FunctionQueueLifetime::CORE);

    Renderer::Core::terminate();
}