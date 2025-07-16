#include "renderer.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "types.h"
#include "io.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_vulkan.h"

#define VK_CHECK(x)                                        \
do {                                                       \
VkResult err = x;                                          \
if (err) {                                                 \
printf("Detected Vulkan error: %s", string_VkResult(err)); \
abort();                                                   \
}                                                          \
} while (0)

struct
{
    SDL_Window* window_ptr;

    VkInstance instance { VK_NULL_HANDLE };

    VkDebugUtilsMessengerEXT debug_messenger;

    VkPhysicalDevice physical_device { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    VkQueue queue { VK_NULL_HANDLE };
    u32 queue_family_index { 0 }; // Supports Presentation, Graphics and Compute (and Transfer implicitly)

    VkSurfaceKHR surface { VK_NULL_HANDLE };
    VkSurfaceFormatKHR swapchain_surface_format { VK_FORMAT_UNDEFINED };
    VkExtent2D swapchain_surface_extent { 0, 0};
    u32 swapchain_image_count { 0 };
    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    std::vector<VkImage> swapchain_images {};
    std::vector<VkImageView> swapchain_image_views {};

    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline graphics_pipeline { VK_NULL_HANDLE };

    VkCommandPool command_pool { VK_NULL_HANDLE };
    VkCommandBuffer command_buffer { VK_NULL_HANDLE };

    VkSemaphore present_complete_semaphore = VK_NULL_HANDLE;
    VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
    VkFence draw_fence = VK_NULL_HANDLE;
} core;

// TODO: Add some sort of error handling for these
// TODO: Enable validation layers

const std::vector validation_layers = {
#if RENDERER_DEBUG
    "VK_LAYER_KHRONOS_validation",
#endif
};

bool create_vulkan_instance()
{
    VkApplicationInfo application_info
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "VV",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "VV",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    VkInstanceCreateInfo instance_create_info
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<u32>(validation_layers.size()),
        .ppEnabledLayerNames = validation_layers.empty() ? nullptr : validation_layers.data(),
    };

    std::vector<const char*> instance_extensions;

    // Get instance extensions from SDL and add them to our create info
    u32 sdl_instance_extensions_count;
    const char * const *sdl_instance_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_instance_extensions_count);

    for (i32 i = 0; i < sdl_instance_extensions_count; i++)
        instance_extensions.push_back(sdl_instance_extensions[i]);

    // Add debug utils extension
    instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    instance_create_info.enabledExtensionCount = static_cast<u32>(instance_extensions.size());
    instance_create_info.ppEnabledExtensionNames = instance_extensions.data();

    VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &core.instance));

    printf("Created Vulkan instance.\n");
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*) {

    printf(pCallbackData->pMessage);

    return VK_FALSE; // Applications must return false here
}

#define VK_PROC_ADDR_LOAD(string_name) reinterpret_cast<PFN_##string_name>(vkGetInstanceProcAddr(core.instance, #string_name))

bool create_debug_messenger()
{
#if RENDERER_DEBUG
    //core.debug_messenger =
    VkDebugUtilsMessengerCreateInfoEXT messenger_create_info
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vk_debug_callback,
    };

    auto vk_create_debug_utils_messenger_ext_function = VK_PROC_ADDR_LOAD(vkCreateDebugUtilsMessengerEXT);

    bool created_debug_messenger = vk_create_debug_utils_messenger_ext_function(core.instance, &messenger_create_info, nullptr, &core.debug_messenger) == VK_SUCCESS;

    if (!created_debug_messenger)
    {
        printf("Failed to create Vulkan debug messenger.\n");
        core.debug_messenger = VK_NULL_HANDLE;
        return false;
    }
    printf("Created Vulkan debug messenger.\n");
    #endif
    return true;
}

bool select_vulkan_physical_device()
{
    // Get count of physical devices available
    u32 physical_device_count { 0 };
    VK_CHECK(vkEnumeratePhysicalDevices(core.instance, &physical_device_count, nullptr));

    // Get all physical devices available
    VkPhysicalDevice physical_devices[physical_device_count];
    VK_CHECK(vkEnumeratePhysicalDevices(core.instance, &physical_device_count, physical_devices));

    // Get the properties of all devices
    std::vector<VkPhysicalDeviceProperties2> device_properties(physical_device_count);

    for (u32 i = 0; i < physical_device_count; i++)
    {
        device_properties[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(physical_devices[i], &device_properties[i]);
    }

    if (physical_device_count == 0)
    {
        printf("Failed to find any physical devices.\n");
        return false;
    }

    // Get the properties of each queue family available on each physical device
    for (u32 i = 0; i < physical_device_count; i++)
    {
        // Get queue family count
        u32 queue_family_count { 0 };
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, nullptr);

        // Get queue family properties
        std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_family_properties.data());

        // Find a suitable graphics card
        for (u32 f = 0; f < queue_family_count; f++)
        {
            VkBool32 presentation_supported {};
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], f, core.surface, &presentation_supported));

            VkQueueFlags& flags = queue_family_properties[f].queueFlags;
            // Transfer queue is implicitly valid thanks to graphics/compute
            if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_COMPUTE_BIT) && presentation_supported)
            {
                core.physical_device = physical_devices[i];
                core.queue_family_index = f;

                printf("Found and picked device with name: %s\n", device_properties[i].properties.deviceName);
                break;
            }
        }
    }

    return true;
}

VkSurfaceFormatKHR select_ideal_swapchain_format(const std::vector<VkSurfaceFormatKHR>& available_swapchain_formats)
{
    for (const auto& available_swapchain_format : available_swapchain_formats)
    {
        if (available_swapchain_format.format == VK_FORMAT_B8G8R8_SRGB && available_swapchain_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_swapchain_format;
        }
    }

    return available_swapchain_formats[0];
}

VkPresentModeKHR select_ideal_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes)
{
    for (const auto& available_present_mode : available_present_modes)
    {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return available_present_mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
        return capabilities.currentExtent;

    i32 width, height;
    SDL_GetWindowSizeInPixels(core.window_ptr, &width, &height);

    return
    {
        // TODO: Replace with glm clamp
        std::clamp<u32>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<u32>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

bool create_vulkan_device()
{
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .synchronization2 = VK_TRUE
    };

    dynamic_rendering_feature.pNext = &synchronization2_features;

    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &dynamic_rendering_feature;

    f32 queue_priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos(1);
    queue_create_infos[0] = {};
    queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[0].pNext = nullptr;
    queue_create_infos[0].flags = 0;
    queue_create_infos[0].queueFamilyIndex = core.queue_family_index;
    queue_create_infos[0].queueCount = 1;
    queue_create_infos[0].pQueuePriorities = &queue_priority;

    device_create_info.queueCreateInfoCount = queue_create_infos.size();
    device_create_info.pQueueCreateInfos = queue_create_infos.data();

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };

    device_create_info.enabledExtensionCount = device_extensions.size();
    device_create_info.ppEnabledExtensionNames = device_extensions.data();

    VK_CHECK(vkCreateDevice(core.physical_device, &device_create_info, nullptr, &core.device));

    printf("Created Vulkan device.\n");

    vkGetDeviceQueue(core.device, core.queue_family_index, 0, &core.queue);

    return true;
}

bool create_sdl_surface()
{
    bool created_surface = SDL_Vulkan_CreateSurface(core.window_ptr, core.instance, nullptr, &core.surface);

    if(!created_surface)
    {
        SDL_Log( "Window surface could not be created! SDL error: %s\n", SDL_GetError() );
        return false;
    }

    printf("Created Vulkan surface.\n");

    return true;
}

bool create_swapchain()
{
    VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core.physical_device, core.surface, &surface_capabilities));

    u32 available_surface_format_count { 0 };
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core.physical_device, core.surface, &available_surface_format_count, nullptr));
    std::vector<VkSurfaceFormatKHR> available_surface_formats(available_surface_format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core.physical_device, core.surface, &available_surface_format_count, available_surface_formats.data()));

    // Pick extent and format for swapchain backing images
    core.swapchain_surface_format = select_ideal_swapchain_format(available_surface_formats);
    core.swapchain_surface_extent = choose_swap_extent(surface_capabilities);

    // Get the minimum count of swapchain images
    u32 min_swapchain_image_count = std::max( 3u, surface_capabilities.minImageCount );
    min_swapchain_image_count = ( surface_capabilities.maxImageCount > 0 && min_swapchain_image_count > surface_capabilities.maxImageCount ) ? surface_capabilities.maxImageCount : min_swapchain_image_count;

    // Add an extra image for extra margin
    core.swapchain_image_count = min_swapchain_image_count + 1;

    // Make sure not to accidentally exceed maximum
    if (surface_capabilities.maxImageCount > 0 && core.swapchain_image_count > surface_capabilities.maxImageCount)
        core.swapchain_image_count = surface_capabilities.maxImageCount;

    u32 available_present_modes_count { 0 };
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core.physical_device, core.surface, &available_present_modes_count, nullptr));
    std::vector<VkPresentModeKHR> available_present_modes(available_present_modes_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core.physical_device, core.surface, &available_present_modes_count, available_present_modes.data()));

    VkSwapchainCreateInfoKHR swapchain_create_info {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = core.surface,
        .minImageCount = core.swapchain_image_count,
        .imageFormat = core.swapchain_surface_format.format,
        .imageColorSpace = core.swapchain_surface_format.colorSpace,
        .imageExtent = core.swapchain_surface_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = select_ideal_present_mode(available_present_modes),
        .clipped = true,
        .oldSwapchain = nullptr,
    };

    VK_CHECK(vkCreateSwapchainKHR(core.device, &swapchain_create_info, nullptr, &core.swapchain));

    core.swapchain_images.resize(core.swapchain_image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(core.device, core.swapchain, &core.swapchain_image_count, core.swapchain_images.data()));

    printf("Created Vulkan swapchain.\n");
    return true;
}

bool create_swapchain_image_views()
{
    core.swapchain_image_views.clear();
    core.swapchain_image_views.resize(core.swapchain_image_count);

    VkImageViewCreateInfo swapchain_image_view_create_info {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D,
        .format = core.swapchain_surface_format.format,
        .subresourceRange = VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0 ,1 },
    };

    i32 index = 0;
    for (auto image : core.swapchain_images)
    {
        swapchain_image_view_create_info.image = image;
        VK_CHECK(vkCreateImageView(core.device, &swapchain_image_view_create_info, nullptr, &core.swapchain_image_views[index]));
        index++;
    }

    printf("Created Vulkan swapchain image views.\n");

    return true;
}

VkShaderModule create_shader_module(const std::vector<u8>& bytecode)
{
    VkShaderModuleCreateInfo shader_module_create_info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytecode.size() * sizeof(u8),
        .pCode = reinterpret_cast<const u32*>(bytecode.data()),
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;

    VK_CHECK(vkCreateShaderModule(core.device, &shader_module_create_info, nullptr, &shader_module));

    return shader_module;
}

bool create_graphics_pipeline()
{
    auto vert_binary = IO::read_binary_file("shaders/shader_base.vert.spv");
    auto frag_binary = IO::read_binary_file("shaders/shader_base.frag.spv");

    if (vert_binary.empty() || frag_binary.empty())
    {
        printf("Failed to read .frag or .vert compiled binary file.\n");
        return false;
    }
    printf("Read .frag and .vert compiled binary file.\n");

    auto vert_shader_module = create_shader_module(vert_binary);
    auto frag_shader_module = create_shader_module(frag_binary);

    if (vert_shader_module == VK_NULL_HANDLE || frag_shader_module == VK_NULL_HANDLE)
    {
        printf("Failed to create .frag or .vert shader module.\n");
        return false;
    }

    printf("Created .frag and .vert shader module.\n");

    VkPipelineShaderStageCreateInfo vert_shader_stage_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vert_shader_module,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo frag_shader_stage_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = frag_shader_module,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_create_info, frag_shader_stage_create_info};

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(core.swapchain_surface_extent.width);
    viewport.height = static_cast<float>(core.swapchain_surface_extent.height);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = core.swapchain_surface_extent;

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineViewportStateCreateInfo viewport_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info {
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

    //vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};
    VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState pipeline_color_blend_attachment_state = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &pipeline_color_blend_attachment_state,
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0,
    };

    VK_CHECK(vkCreatePipelineLayout(core.device, &pipeline_layout_create_info, nullptr, &core.pipeline_layout));

    VkPipelineRenderingCreateInfo pipeline_rendering_create_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &core.swapchain_surface_format.format,
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
        .layout = core.pipeline_layout,
        .renderPass = nullptr,
    };

    VK_CHECK(vkCreateGraphicsPipelines(core.device, nullptr, 1, &graphics_pipeline_create_info, nullptr, &core.graphics_pipeline ));

    printf("Created graphics pipeline.\n");

    return true;
}

bool create_command_pool()
{
    VkCommandPoolCreateInfo command_pool_create_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    VK_CHECK(vkCreateCommandPool(core.device, &command_pool_create_info, nullptr, &core.command_pool));

    return true;
}

bool create_command_buffer()
{
    VkCommandBufferAllocateInfo command_buffer_allocate_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = core.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK(vkAllocateCommandBuffers(core.device, &command_buffer_allocate_info, &core.command_buffer));

    return true;
}

void transition_image_layout(
    uint32_t imageIndex,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags2 srcAccessMask,
    VkAccessFlags2 dstAccessMask,
    VkPipelineStageFlags2 srcStageMask,
    VkPipelineStageFlags2 dstStageMask
) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = core.swapchain_images[imageIndex],
        .subresourceRange = {
            .aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    VkDependencyInfo dependency_info {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(core.command_buffer, &dependency_info);
}

void record_command_buffer(u32 image_index)
{
    VkCommandBufferBeginInfo command_buffer_begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(core.command_buffer, &command_buffer_begin_info));

    transition_image_layout(image_index,
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
        .imageView = core.swapchain_image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_color_value,
    };

    VkRenderingInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {.x = 0, .y = 0}, .extent = core.swapchain_surface_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &rendering_attachment_info,
    };


    vkCmdBeginRendering(core.command_buffer, &rendering_info);

    vkCmdBindPipeline(core.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, core.graphics_pipeline);

    auto viewport = VkViewport(0.0f, 0.0f, static_cast<float>(core.swapchain_surface_extent.width), static_cast<float>(core.swapchain_surface_extent.height), 0.0f, 1.0f);
    auto scissor = VkRect2D(VkOffset2D(0, 0), core.swapchain_surface_extent);
    vkCmdSetViewport(core.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(core.command_buffer, 0, 1, &scissor);

    vkCmdDraw(core.command_buffer, 3, 1, 0, 0);

    vkCmdEndRendering(core.command_buffer);

    transition_image_layout(image_index,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        {},
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );

    vkEndCommandBuffer(core.command_buffer);
}

void create_sync_objects()
{
    VkSemaphoreCreateInfo semaphore_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VK_CHECK(vkCreateSemaphore(core.device, &semaphore_info, nullptr, &core.present_complete_semaphore));
    VK_CHECK(vkCreateSemaphore(core.device, &semaphore_info, nullptr, &core.render_finished_semaphore));
    VK_CHECK(vkCreateFence(core.device, &fence_info, nullptr, &core.draw_fence));
}

void resize_swapchain()
{
    // Wait until the device is idle to safely destroy resources
    vkDeviceWaitIdle(core.device);

    // Destroy old swapchain image views
    for (auto image_view : core.swapchain_image_views)
    {
        vkDestroyImageView(core.device, image_view, nullptr);
    }

    core.swapchain_image_views.clear();

    // Destroy the old swapchain
    if (core.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(core.device, core.swapchain, nullptr);
        core.swapchain = VK_NULL_HANDLE;
    }

    // Recreate the swapchain and dependent resources
    create_swapchain();
    create_swapchain_image_views();

    // Recreate or record the command buffer again if it depends on swapchain images
    // If using multiple frames in flight, you'd update all command buffers here
    create_command_buffer(); // or vkResetCommandBuffer(...) if persistent
}

void Renderer::initialize(SDL_Window* sdl_window_ptr)
{
    create_vulkan_instance();
    create_debug_messenger();

    core.window_ptr = sdl_window_ptr;
    create_sdl_surface();

    select_vulkan_physical_device();
    create_vulkan_device();
    create_swapchain();
    create_swapchain_image_views();

    create_graphics_pipeline();
    create_command_pool();
    create_command_buffer();
    create_sync_objects();
}

u32 rendered_image_count = 0;
void Renderer::update()
{
    VkResult acquire_image_result = vkAcquireNextImageKHR(core.device, core.swapchain, UINT64_MAX, core.present_complete_semaphore, nullptr, &rendered_image_count);

    if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        resize_swapchain();
        return;
    }


    record_command_buffer(rendered_image_count);
    VK_CHECK(vkResetFences(core.device, 1, &core.draw_fence));

    VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &core.present_complete_semaphore,
        .pWaitDstStageMask = &stage_flags,
        .commandBufferCount = 1,
        .pCommandBuffers = &core.command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &core.render_finished_semaphore,
    };

    VK_CHECK(vkQueueSubmit(core.queue, 1, &submit_info, core.draw_fence));

    VK_CHECK(vkWaitForFences(core.device,1, &core.draw_fence, VK_TRUE, UINT64_MAX));

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &core.render_finished_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &core.swapchain,
        .pImageIndices = &rendered_image_count,
    };

    VkResult present_result = vkQueuePresentKHR(core.queue, &present_info);

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR)
        resize_swapchain();
}

void Renderer::terminate()
{
    // TODO: Replace this with a deletion queue
    vkDestroySurfaceKHR(core.instance, core.surface, nullptr);
    vkDestroyDevice(core.device, nullptr);
    vkDestroyInstance(core.instance, nullptr);
}

