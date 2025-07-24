#include "compute_pipeline.h"

#include "../../common/types.h"
#include "renderer_core.h"

VkShaderModule create_shader_module(const std::vector<u8>& bytecode)
{
    VkShaderModuleCreateInfo shader_module_create_info
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytecode.size(),
        .pCode = reinterpret_cast<const u32*>(bytecode.data()),
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;

    VK_CHECK(vkCreateShaderModule(Renderer::Core::get_logical_device(), &shader_module_create_info, nullptr, &shader_module));

    return shader_module;
}

void ComputePipeline::dispatch(VkCommandBuffer command_buffer, u32 group_count_x, u32 group_count_y, u32 group_count_z, void* push_constants_data_ptr)
{
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    VkBufferDeviceAddressInfo buffer_device_address_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = descriptor_buffer,
    };

    VkDeviceAddress address = vkGetBufferDeviceAddress(Renderer::Core::get_logical_device(), &buffer_device_address_info);

    VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .address = address,
        .usage   = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
    };

    vkCmdBindDescriptorBuffersEXT(command_buffer, 1, &descriptor_buffer_binding_info);

    u32 buffer_index = 0;
    VkDeviceSize buffer_offset = 0;
    vkCmdSetDescriptorBufferOffsetsEXT(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &buffer_index, &buffer_offset);

    if ((push_constants_size != 0) && push_constants_data_ptr)
        vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants_size, push_constants_data_ptr);

    vkCmdDispatch(command_buffer, group_count_x, group_count_y, group_count_z);
}

void ComputePipeline::destroy()
{
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    vmaDestroyBuffer(Renderer::Core::get_vma_allocator(), descriptor_buffer, descriptor_buffer_allocation);
    vkDestroyPipelineLayout(Renderer::Core::get_logical_device(), pipeline_layout, nullptr);
    vkDestroyPipeline(Renderer::Core::get_logical_device(), pipeline, nullptr);
}

ComputePipelineBuilder::ComputePipelineBuilder(const std::filesystem::path& path)
{
    auto comp_binary = IO::read_binary_file(path);

    if (comp_binary.empty())
    {
        printf("Failed to read %s compiled binary file.\n", path.c_str());
        return;
    }

    shader_module = create_shader_module(comp_binary);
}

ComputePipelineBuilder& ComputePipelineBuilder::bind_storage_image(VkImageView image_view)
{
    VkDescriptorSetLayoutBinding new_descriptor_set_layout_binding
    {
        .binding = static_cast<u32>(bindings.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    bindings.push_back(new_descriptor_set_layout_binding);
    image_views.push_back(image_view);
    buffer_sizes.push_back(0);
    buffers.push_back(VK_NULL_HANDLE);

    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::bind_storage_buffer(VkBuffer buffer, VkDeviceSize buffer_size)
{
    VkDescriptorSetLayoutBinding new_descriptor_set_layout_binding
    {
        .binding = static_cast<u32>(bindings.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    bindings.push_back(new_descriptor_set_layout_binding);
    image_views.push_back(VK_NULL_HANDLE);
    buffer_sizes.push_back(buffer_size);
    buffers.push_back(buffer);

    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::set_push_constants_size(VkDeviceSize size)
{
    push_constants_size = size;
    return *this;
}

ComputePipeline ComputePipelineBuilder::create(VkDevice device)
{
    ComputePipeline generated_pipeline;

    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo info =
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &generated_pipeline.descriptor_set_layout));

    auto descriptor_buffer_properties = Renderer::Core::get_physical_device_properties().descriptor_buffer_properties;

    // Get the size of the descriptor set/layout and align it properly
    VkDeviceSize descriptor_set_layout_size;
    vkGetDescriptorSetLayoutSizeEXT(Renderer::Core::get_logical_device(), generated_pipeline.descriptor_set_layout, &descriptor_set_layout_size);
    descriptor_set_layout_size = aligned_size(descriptor_set_layout_size, descriptor_buffer_properties.descriptorBufferOffsetAlignment);

    // Create a buffer to hold the descriptor set/layout
    VkBufferCreateInfo buffer_create_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = descriptor_set_layout_size,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };

    VmaAllocationCreateInfo vma_create_info
    {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    VK_CHECK(vmaCreateBuffer(Renderer::Core::get_vma_allocator(), &buffer_create_info, &vma_create_info, &generated_pipeline.descriptor_buffer, &generated_pipeline.descriptor_buffer_allocation, nullptr));

    // Write descriptors to buffer
    u8* mapped_ptr = nullptr;
    VK_CHECK(vmaMapMemory(Renderer::Core::get_vma_allocator(), generated_pipeline.descriptor_buffer_allocation, reinterpret_cast<void**>(&mapped_ptr)));
    for (i32 i = 0; i < bindings.size(); i++)
    {
        VkDescriptorGetInfoEXT descriptor_info
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = bindings[i].descriptorType,
        };

        VkDeviceSize descriptor_binding_offset { 0 };
        vkGetDescriptorSetLayoutBindingOffsetEXT(Renderer::Core::get_logical_device(), generated_pipeline.descriptor_set_layout, static_cast<u32>(i), &descriptor_binding_offset);

        switch (bindings[i].descriptorType)
        {
            default:
                printf("DESCRIPTOR TYPE NOT SUPPORTED FOR DESCRIPTOR BUFFER IN COMPUTE PIPELINE BUILDER");
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                {
                    VkDescriptorImageInfo image_descriptor
                    {
                        .sampler = VK_NULL_HANDLE,
                        .imageView = image_views[i],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };
                    descriptor_info.data.pStorageImage = &image_descriptor;
                    vkGetDescriptorEXT(Renderer::Core::get_logical_device(), &descriptor_info, descriptor_buffer_properties.storageImageDescriptorSize, mapped_ptr + descriptor_binding_offset);
                }
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                {
                    VkBufferDeviceAddressInfo buffer_device_address_info
                    {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                        .buffer = buffers[i],
                    };

                    VkDescriptorAddressInfoEXT buffer_descriptor_info{
                        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                        .address = vkGetBufferDeviceAddress(device, &buffer_device_address_info),
                        .range = buffer_sizes[i],
                        .format = VK_FORMAT_UNDEFINED
                    };

                    descriptor_info.data.pStorageBuffer = &buffer_descriptor_info;
                    vkGetDescriptorEXT(Renderer::Core::get_logical_device(), &descriptor_info, descriptor_buffer_properties.storageBufferDescriptorSize, mapped_ptr + descriptor_binding_offset);
                }
                break;
        }
    }
    vmaUnmapMemory(Renderer::Core::get_vma_allocator(), generated_pipeline.descriptor_buffer_allocation);

    // Create pipeline layout
    VkPushConstantRange push_constant_range
    {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = static_cast<u32>(push_constants_size),
    };

    VkPipelineLayoutCreateInfo compute_pipeline_layout_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 1,
        .pSetLayouts = &generated_pipeline.descriptor_set_layout,
        .pushConstantRangeCount = push_constants_size == 0u ? 0u : 1u,
        .pPushConstantRanges = &push_constant_range,
    };

    generated_pipeline.push_constants_size = push_constants_size;

    VK_CHECK(vkCreatePipelineLayout(Renderer::Core::get_logical_device(), &compute_pipeline_layout_create_info, nullptr, &generated_pipeline.pipeline_layout));

    // Create pipeline
    VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shader_module,
        .pName = "main",
    };

    VkComputePipelineCreateInfo compute_pipeline_create_info
    {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stage = pipeline_shader_stage_create_info,
        .layout = generated_pipeline.pipeline_layout,
    };

    VK_CHECK(vkCreateComputePipelines(Renderer::Core::get_logical_device(), nullptr, 1, &compute_pipeline_create_info, nullptr, &generated_pipeline.pipeline))  ;
    vkDestroyShaderModule(Renderer::Core::get_logical_device(), shader_module, nullptr);

    generated_pipeline.device = device;
    return generated_pipeline;
}