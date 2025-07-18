#include "renderer.h"
#include "renderer_core.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "types.h"
#include "io.h"

#include <vulkan/vk_enum_string_helper.h>
#include "SDL3/SDL_vulkan.h"

enum DeletionQueueLifetime
{
    Core,
    RANGE
};
#include "deletion_queue.h"

struct
{
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline graphics_pipeline { VK_NULL_HANDLE };
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

void create_graphics_pipeline()
{
    auto vert_binary = IO::read_binary_file("shaders/shader_base.vert.spv");
    auto frag_binary = IO::read_binary_file("shaders/shader_base.frag.spv");

    if (vert_binary.empty() || frag_binary.empty())
    {
        printf("Failed to read .frag or .vert compiled binary file.\n");
        return;
    }

    auto vert_shader_module = create_shader_module(vert_binary);
    auto frag_shader_module = create_shader_module(frag_binary);

    if (vert_shader_module == VK_NULL_HANDLE || frag_shader_module == VK_NULL_HANDLE)
    {
        printf("Failed to create .frag or .vert shader module.\n");
        return;
    }

    VkPipelineShaderStageCreateInfo vert_shader_stage_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert_shader_module,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo frag_shader_stage_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag_shader_module,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo shader_stages[]
    {
        vert_shader_stage_create_info,
        frag_shader_stage_create_info
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    std::vector<VkDynamicState> dynamic_states
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineViewportStateCreateInfo viewport_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasClamp = VK_FALSE,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState pipeline_color_blend_attachment_state =
    {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &pipeline_color_blend_attachment_state,
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0,
    };

    VK_CHECK(vkCreatePipelineLayout(Renderer::Core::get_logical_device(), &pipeline_layout_create_info, nullptr, &state.pipeline_layout));

    VkPipelineRenderingCreateInfo pipeline_rendering_create_info
        {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &Renderer::Core::get_swapchain_data().surface_format.format,
    };

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipeline_rendering_create_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_state_create_info,
        .pInputAssemblyState = &input_assembly_state_create_info,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &pipeline_multisample_state_create_info,
        .pColorBlendState = &pipeline_color_blend_state_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = state.pipeline_layout,
        .renderPass = nullptr,
    };

    VK_CHECK(vkCreateGraphicsPipelines(Renderer::Core::get_logical_device(), nullptr, 1, &graphics_pipeline_create_info, nullptr, &state.graphics_pipeline ));

    QUEUE_DELETE(DeletionQueueLifetime::Core, vkDestroyPipelineLayout(Renderer::Core::get_logical_device(), state.pipeline_layout, nullptr));
    QUEUE_DELETE(DeletionQueueLifetime::Core, vkDestroyPipeline(Renderer::Core::get_logical_device(), state.graphics_pipeline, nullptr));
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), vert_shader_module, nullptr);
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), frag_shader_module, nullptr);
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    Renderer::Core::initialize(sdl_window_ptr);

    create_graphics_pipeline();
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
void Renderer::update()
{
    auto per_frame_data = Renderer::Core::begin_frame();
    auto swapchain_data = Renderer::Core::get_swapchain_data();

    VkCommandBufferBeginInfo command_buffer_begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(per_frame_data.command_buffer, &command_buffer_begin_info));

    transition_image_layout(per_frame_data.command_buffer,
        per_frame_data.swapchain_image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        {},
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkClearColorValue clear_color_value = {0.0f, 0.0f, 0.0f, 1.0f};
    VkRenderingAttachmentInfo rendering_attachment_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = per_frame_data.swapchain_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_color_value,
    };

    VkRenderingInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {.x = 0, .y = 0}, .extent = swapchain_data.surface_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &rendering_attachment_info,
    };

    vkCmdBeginRendering(per_frame_data.command_buffer, &rendering_info);

    vkCmdBindPipeline(per_frame_data.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.graphics_pipeline);

    auto viewport = VkViewport(0.0f, 0.0f, static_cast<float>(swapchain_data.surface_extent.width), static_cast<float>(swapchain_data.surface_extent.height), 0.0f, 1.0f);
    auto scissor = VkRect2D(VkOffset2D(0, 0), swapchain_data.surface_extent);
    vkCmdSetViewport(per_frame_data.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(per_frame_data.command_buffer, 0, 1, &scissor);

    vkCmdDraw(per_frame_data.command_buffer, 3, 1, 0, 0);

    vkCmdEndRendering(per_frame_data.command_buffer);

    transition_image_layout(per_frame_data.command_buffer,
        per_frame_data.swapchain_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        {},
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );

    vkEndCommandBuffer(per_frame_data.command_buffer);

    Renderer::Core::end_frame();
}

void Renderer::terminate()
{
    deletion_queues[DeletionQueueLifetime::Core].flush();

    Renderer::Core::terminate();
}

