#include "renderer.h"
#include "renderer_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#include "types.h"
#include "io.h"

#include <vulkan/vk_enum_string_helper.h>
#include "SDL3/SDL_vulkan.h"

enum DeletionQueueLifetime
{
    CORE,
    RANGE
};
#include "deletion_queue.h"

struct DescriptorAllocator {

    struct PoolSizeRatio{
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool;

    void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clear_descriptors(VkDevice device);
    void destroy_pool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

void DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * maxSets)
        });
    }

    VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = 0;
    pool_info.maxSets = maxSets;
    pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

void DescriptorAllocator::clear_descriptors(VkDevice device)
{
    vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::destroy_pool(VkDevice device)
{
    vkDestroyDescriptorPool(device,pool,nullptr);
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

    return ds;
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
    for (auto& b : bindings) {
        b.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = pNext;

    info.pBindings = bindings.data();
    info.bindingCount = (uint32_t)bindings.size();
    info.flags = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

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

struct
{
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline graphics_pipeline { VK_NULL_HANDLE };

    VkPipelineLayout compute_pipeline_layout { VK_NULL_HANDLE };
    VkPipeline compute_pipeline { VK_NULL_HANDLE };

    Renderer::AllocatedImage draw_image {};

    DescriptorAllocator globalDescriptorAllocator;

    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;
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

    QUEUE_DELETE(DeletionQueueLifetime::CORE, vkDestroyPipelineLayout(Renderer::Core::get_logical_device(), state.pipeline_layout, nullptr));
    QUEUE_DELETE(DeletionQueueLifetime::CORE, vkDestroyPipeline(Renderer::Core::get_logical_device(), state.graphics_pipeline, nullptr));
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), vert_shader_module, nullptr);
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), frag_shader_module, nullptr);
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

    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    state.globalDescriptorAllocator.init_pool(Renderer::Core::get_logical_device(), 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        state._drawImageDescriptorLayout = builder.build(Renderer::Core::get_logical_device(), VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //allocate a descriptor set for our draw image
    state._drawImageDescriptors = state.globalDescriptorAllocator.allocate(Renderer::Core::get_logical_device(), state._drawImageDescriptorLayout);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = state.draw_image.view;

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;

    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = state._drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(Renderer::Core::get_logical_device(), 1, &drawImageWrite, 0, nullptr);

    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &state._drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(Renderer::Core::get_logical_device(), &computeLayout, nullptr, &state.compute_pipeline_layout));

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

    VK_CHECK(vkCreateComputePipelines(Renderer::Core::get_logical_device(),VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &state.compute_pipeline));

    vkCreateComputePipelines(Renderer::Core::get_logical_device(), nullptr, 1, &computePipelineCreateInfo, nullptr, &state.compute_pipeline);
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), comp_shader_module, nullptr);
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    Core::initialize(sdl_window_ptr);
    //
    // std::vector<VkDescriptorPoolSize> pool_sizes =
    // {
    //     { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
    //     { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    //     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    //     { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    // };
    //
    // VkDescriptorPoolCreateInfo descriptor_pool_create_info
    // {
    //     .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    //     .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    //     .maxSets = 1000,
    //     .poolSizeCount = static_cast<u32>(pool_sizes.size()),
    //     .pPoolSizes = pool_sizes.data(),
    // };
    //
    // VK_CHECK(vkCreateDescriptorPool(Renderer::Core::get_logical_device(), &descriptor_pool_create_info, nullptr, &state.descriptor_pool));
    //
    //
    //
    // VkDescriptorSetLayoutBinding pipeline_layout_binding
    // {
    //     .binding = 0,
    //     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    //     .descriptorCount = 1,
    //     //.stageFlags = ,
    //     //.pImmutableSamplers = ,
    // };
    // VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info
    // {
    //     .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    //     //.flags = VK_DESCRIPTOR_SET_LAYOUT
    //     .bindingCount = 1;
    //     .pBindings =
    // };
    //
    // (vkCreateDescriptorSetLayout(Renderer::Core::get_logical_device(), ));

    auto swapchain_data = Core::get_swapchain_data();

    state.draw_image = Renderer::Core::create_image(swapchain_data.surface_extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "compute_draw_image");

    create_graphics_pipeline();
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

    VkCommandBufferBeginInfo command_buffer_begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(per_frame_data.command_buffer, &command_buffer_begin_info));

    transition_image_layout(per_frame_data.command_buffer,
        per_frame_data.swapchain_image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        {},
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    transition_image_layout(per_frame_data.command_buffer,
       state.draw_image.image,
       VK_IMAGE_LAYOUT_UNDEFINED,
       VK_IMAGE_LAYOUT_GENERAL,
       {},
       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
   );

    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(per_frame_data.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.compute_pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(per_frame_data.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.compute_pipeline_layout, 0, 1, &state._drawImageDescriptors, 0, nullptr);

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(per_frame_data.command_buffer, std::ceil(swapchain_data.surface_extent.width / 16.0), std::ceil(swapchain_data.surface_extent.height / 16.0), 1);

    // draw_background(per_frame_data.command_buffer);

    transition_image_layout(per_frame_data.command_buffer,
       state.draw_image.image,
       VK_IMAGE_LAYOUT_GENERAL,
       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
       {},
       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
   );

    copy_image_to_image(per_frame_data.command_buffer, state.draw_image.image, per_frame_data.swapchain_image, swapchain_data.surface_extent, swapchain_data.surface_extent);

    transition_image_layout(per_frame_data.command_buffer,
        per_frame_data.swapchain_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        {},
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );

    Renderer::Core::end_frame();
}

void Renderer::terminate()
{
    deletion_queues[DeletionQueueLifetime::CORE].flush();

    Renderer::Core::terminate();
}

