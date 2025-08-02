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
#include <glm/gtc/type_ptr.hpp>

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

// TODO: Move this elsewhere
struct VVBuffer
{
    VkBuffer handle { VK_NULL_HANDLE };
    VkDeviceSize size { 0 };
    VmaAllocation allocation { VK_NULL_HANDLE };

    void destroy();
};

void VVBuffer::destroy()
{
    vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), handle, allocation);
}

VVBuffer create_buffer(VkDeviceSize size, bool cpu_to_gpu = false)
{
    VVBuffer created_buffer {};

    VkBufferCreateInfo buffer_create_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
    };

    VmaAllocationCreateInfo vma_allocation_create_info
    {
        .usage = cpu_to_gpu ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_AUTO,
    };

    vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_allocation_create_info, &created_buffer.handle, &created_buffer.allocation, nullptr);
    return created_buffer;
}

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

    VVBuffer voxel_model_buffer;
    VVBuffer raygen_buffer;

    Renderer::AllocatedImage draw_image {};
} state;

struct
{
    glm::mat4 camera_matrix { glm::mat4(1) };
} compute_push_constants;

void create_raygen_pipeline()
{
    auto& extent = Renderer::Core::get_swapchain_data().surface_extent;
    state.raygen_buffer = create_buffer(sizeof(glm::vec4) * extent.width * extent.height);
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, state.raygen_buffer.destroy());

    state.raygen_pipeline = ComputePipelineBuilder(SHADER_COMPILED_PATH "rt_raygen.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer(state.raygen_buffer.handle, state.raygen_buffer.size)
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
    auto ogt_file = IO::read_binary_file("../test_model.vox");
    const ogt_vox_scene* scene = ogt_vox_read_scene(ogt_file.data(), ogt_file.size());

    struct alignas(16) ModelHeader
    {
        glm::ivec4 index_and_size;
        glm::mat4 inverse_transform;
    };

    ModelHeader headers[8];
    for (i32 i = 0; i < 8; i++)
    {
        headers[i].index_and_size = glm::ivec4(-1, -1, -1, -1);
        headers[i].inverse_transform = glm::mat4(1.0f);
    }

    u32 combined_model_size_in_voxels { 0 };
    u32 combined_model_size_in_bytes { 0 };

    for (u32 i = 0u; i < scene->num_instances && i < 8u; i++)
    {
        const auto& instance = scene->instances[i];
        const auto& model = scene->models[instance.model_index];
        const auto size = glm::ivec3(model->size_x, model->size_y, model->size_z);

        glm::mat4 transform = glm::make_mat4(&instance.transform.m00);

        glm::mat4 axis_correction = glm::mat4(
            glm::vec4(1, 0,  0, 0),  // X stays X
            glm::vec4(0, 0,  -1, 0),  // Y becomes Z
            glm::vec4(0,  1, 0, 0),  // Z becomes -Y
            glm::vec4(0, 0,  0, 1)   // translation
        );

        transform = axis_correction * transform;
        headers[i].index_and_size = glm::ivec4(i, size.x, size.y, size.z);
        headers[i].inverse_transform = glm::inverse(transform);

        auto model_size_in_voxels = size.x * size.y * size.z;
        auto model_size_in_bytes = sizeof(u32) * model_size_in_voxels;

        combined_model_size_in_voxels += model_size_in_voxels;
        combined_model_size_in_bytes += model_size_in_bytes;
    }

    std::vector<u32> voxels(combined_model_size_in_voxels);
    memset(voxels.data(), 0, sizeof(u32) * combined_model_size_in_voxels);
    i32 offset = 0;

    u32 model_offsets[8];

    for (u32 i = 0u; i < scene->num_models; i++)
    {
        const auto& model = scene->models[i];
        const auto size = glm::ivec3(model->size_x, model->size_y, model->size_z);

        for (u32 v = 0u; v < size.x * size.y * size.z; v++)
        {
            u32 source_x = (v % size.x);
            u32 source_y = (v / size.x % size.y);
            u32 source_z = (v / (size.x * size.y)) % size.z;

            //u32 index_1d = offset + source_x + source_y * size.x + source_z * size.x * size.y; // MagicaVoxel uses a difference space!
            voxels[v + offset] = model->voxel_data[v]; // Might be sideways
        }
        model_offsets[i] = offset;
        offset += size.x * size.y * size.z;
    }

    for (u32 i = 0u; i < scene->num_instances && i < 8u; i++)
    {
        headers[i].index_and_size.r = static_cast<i32>(model_offsets[scene->instances[i].model_index]);
    }

    state.voxel_model_buffer = create_buffer(sizeof(ModelHeader) * 8 + combined_model_size_in_bytes, true);
    QUEUE_FUNCTION(FunctionQueueLifetime::CORE, state.voxel_model_buffer.destroy());

    u8* mapped_data;
    vmaMapMemory(Renderer::Core::get_vma_allocator(), state.voxel_model_buffer.allocation, reinterpret_cast<void**>(&mapped_data));

    memcpy(mapped_data, headers, sizeof(ModelHeader) * 8);
    memcpy(mapped_data + (sizeof(ModelHeader) * 8), voxels.data(), voxels.size() * sizeof(u32));

    vmaUnmapMemory(Renderer::Core::get_vma_allocator(), state.voxel_model_buffer.allocation);

    ogt_vox_destroy_scene(scene);

    state.intersect_pipeline = ComputePipelineBuilder( SHADER_COMPILED_PATH "rt_intersect.comp.spv")
        .bind_storage_image(state.draw_image.view)
        .bind_storage_buffer(state.raygen_buffer.handle, state.raygen_buffer.size)
        .bind_storage_buffer(state.voxel_model_buffer.handle, state.voxel_model_buffer.size)
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
                .bind_storage_buffer(state.raygen_buffer.handle, state.raygen_buffer.size)
                .bind_storage_buffer(state.voxel_model_buffer.handle, state.voxel_model_buffer.size)
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