#include "renderer_core.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "types.h"
#include "io.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_vulkan.h"

#define VK_PROC_ADDR_LOAD(string_name) reinterpret_cast<PFN_##string_name>(vkGetInstanceProcAddr(internal.instance, #string_name))

namespace Renderer::Core
{
    struct
    {
        SDL_Window* window_ptr { nullptr };

        VkInstance instance { VK_NULL_HANDLE };

        VkDebugUtilsMessengerEXT debug_messenger { VK_NULL_HANDLE };

        VkPhysicalDevice physical_device { VK_NULL_HANDLE };
        VkDevice device { VK_NULL_HANDLE };
        VkQueue queue { VK_NULL_HANDLE };
        u32 queue_family_index { 0 }; // Supports Presentation, Graphics and Compute (and Transfer implicitly)

        SwapchainData swapchain_data {};

        VkSurfaceKHR surface { VK_NULL_HANDLE };

        u32 swapchain_image_count { 0 };
        VkSwapchainKHR swapchain { VK_NULL_HANDLE };
        uint32_t last_swapchain_image_index { 0 };
        uint32_t current_swapchain_image_index { 0 };

        VkCommandPool command_pool { VK_NULL_HANDLE };

        PerFrameData* per_frame_data { nullptr };
    } internal;

    const std::vector<const char*> validation_layers = {
    #if RENDERER_DEBUG
        "VK_LAYER_KHRONOS_validation",
    #endif
    };

    // TODO: Maybe needed device and instance extensions could also be passed from renderer?
    std::vector<const char*> instance_extensions = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };

    // Internal Functions
    VkSurfaceFormatKHR select_ideal_swapchain_format(const std::vector<VkSurfaceFormatKHR>& available_swapchain_formats)
    {
        for (const auto& available_swapchain_format : available_swapchain_formats)
            if (available_swapchain_format.format == VK_FORMAT_B8G8R8_SRGB && available_swapchain_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return available_swapchain_format;

        return available_swapchain_formats[0];
    }

    VkPresentModeKHR select_ideal_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes)
    {
        for (const auto& available_present_mode : available_present_modes)
            if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
                return available_present_mode;

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
            return capabilities.currentExtent;

        i32 width, height;
        SDL_GetWindowSizeInPixels(internal.window_ptr, &width, &height);

        return
        {
            // TODO: Replace with glm clamp
            std::clamp<u32>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<u32>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }

    void create_vulkan_instance()
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
            .pApplicationInfo = &application_info,
            .enabledLayerCount = static_cast<u32>(validation_layers.size()),
            .ppEnabledLayerNames = validation_layers.empty() ? nullptr : validation_layers.data(),
        };

        // Get instance extensions from SDL and add them to our create info
        u32 sdl_instance_extensions_count;
        const char * const *sdl_instance_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_instance_extensions_count);

        for (i32 i = 0; i < sdl_instance_extensions_count; i++)
            instance_extensions.push_back(sdl_instance_extensions[i]);

        instance_create_info.enabledExtensionCount = static_cast<u32>(instance_extensions.size());
        instance_create_info.ppEnabledExtensionNames = instance_extensions.data();

        VK_CHECK(vkCreateInstance(&instance_create_info, nullptr, &internal.instance));
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void*)
    {
        printf(callback_data->pMessage);
        return VK_FALSE; // Applications must return false here
    }

    void create_debug_messenger()
    {
#if RENDERER_DEBUG
        VkDebugUtilsMessengerCreateInfoEXT messenger_create_info
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vk_debug_callback,
        };

        VK_PROC_ADDR_LOAD(vkCreateDebugUtilsMessengerEXT)(internal.instance, &messenger_create_info, nullptr, &internal.debug_messenger);
#endif
    }

    void create_vulkan_device()
    {
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .dynamicRendering = VK_TRUE,
        };

        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2_features
        {
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
        queue_create_infos[0].queueFamilyIndex = internal.queue_family_index;
        queue_create_infos[0].queueCount = 1;
        queue_create_infos[0].pQueuePriorities = &queue_priority;

        device_create_info.queueCreateInfoCount = queue_create_infos.size();
        device_create_info.pQueueCreateInfos = queue_create_infos.data();

        device_create_info.enabledExtensionCount = device_extensions.size();
        device_create_info.ppEnabledExtensionNames = device_extensions.data();

        VK_CHECK(vkCreateDevice(internal.physical_device, &device_create_info, nullptr, &internal.device));
        vkGetDeviceQueue(internal.device, internal.queue_family_index, 0, &internal.queue);
    }

    void select_vulkan_physical_device()
    {
        u32 physical_device_count { 0 };
        VK_CHECK(vkEnumeratePhysicalDevices(internal.instance, &physical_device_count, nullptr));
        VkPhysicalDevice physical_devices[physical_device_count];
        VK_CHECK(vkEnumeratePhysicalDevices(internal.instance, &physical_device_count, physical_devices));

        // Get the properties of all devices
        std::vector<VkPhysicalDeviceProperties2> device_properties(physical_device_count);

        for (u32 i = 0; i < physical_device_count; i++)
        {
            device_properties[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            vkGetPhysicalDeviceProperties2(physical_devices[i], &device_properties[i]);
        }

        // Get the properties of each queue family available on each physical device
        for (u32 i = 0; i < physical_device_count; i++)
        {
            // Get queue family count
            u32 queue_family_count { 0 };
            vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_family_properties.data());

            // Find a suitable graphics card
            for (u32 f = 0; f < queue_family_count; f++)
            {
                VkBool32 is_presentation_supported {};
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], f, internal.surface, &is_presentation_supported));

                VkQueueFlags& flags = queue_family_properties[f].queueFlags;
                // Transfer queue is implicitly valid thanks to graphics/compute
                if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_COMPUTE_BIT) && is_presentation_supported)
                {
                    internal.physical_device = physical_devices[i];
                    internal.queue_family_index = f;

                    printf("Found and picked device with name: %s\n", device_properties[i].properties.deviceName);
                    break;
                }
            }
        }
    }

    void create_sdl_surface()
    {
        if(!SDL_Vulkan_CreateSurface(internal.window_ptr, internal.instance, nullptr, &internal.surface))
            SDL_Log( "Window surface could not be created! SDL error: %s\n", SDL_GetError() );
    }

    void create_swapchain_image_views()
    {
        for (i32 i = 0; i < internal.swapchain_image_count; i++)
        {
            vkDestroyImageView(internal.device, internal.per_frame_data[i].swapchain_image_view, nullptr);
        }

        VkImageViewCreateInfo swapchain_image_view_create_info
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D,
            .format = internal.swapchain_data.surface_format.format,
            .subresourceRange = VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0 ,1 },
        };

        for (i32 i = 0; i < internal.swapchain_image_count; i++)
        {
            auto& per_frame_data = internal.per_frame_data[i];
            swapchain_image_view_create_info.image = per_frame_data.swapchain_image;
            VK_CHECK(vkCreateImageView(internal.device, &swapchain_image_view_create_info, nullptr, &per_frame_data.swapchain_image_view));
        }
    }

    void create_swapchain()
    {
        vkDestroySwapchainKHR(internal.device, internal.swapchain, nullptr);

        VkSurfaceCapabilitiesKHR surface_capabilities;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(internal.physical_device, internal.surface, &surface_capabilities));

        u32 available_surface_format_count { 0 };
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(internal.physical_device, internal.surface, &available_surface_format_count, nullptr));
        std::vector<VkSurfaceFormatKHR> available_surface_formats(available_surface_format_count);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(internal.physical_device, internal.surface, &available_surface_format_count, available_surface_formats.data()));

        // Pick extent and format for swapchain backing images
        internal.swapchain_data.surface_format = select_ideal_swapchain_format(available_surface_formats);
        internal.swapchain_data.surface_extent = choose_swap_extent(surface_capabilities);

        // Get the minimum count of swapchain images
        u32 min_swapchain_image_count = std::max( 3u, surface_capabilities.minImageCount );
        min_swapchain_image_count = ( surface_capabilities.maxImageCount > 0 && min_swapchain_image_count > surface_capabilities.maxImageCount ) ? surface_capabilities.maxImageCount : min_swapchain_image_count;

        // Add an extra image for extra margin
        internal.swapchain_image_count = min_swapchain_image_count + 1;

        // Make sure not to accidentally exceed maximum
        if (surface_capabilities.maxImageCount > 0 && internal.swapchain_image_count > surface_capabilities.maxImageCount)
            internal.swapchain_image_count = surface_capabilities.maxImageCount;

        if (internal.per_frame_data == nullptr)
            internal.per_frame_data = new PerFrameData[internal.swapchain_image_count];

        u32 available_present_modes_count { 0 };
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(internal.physical_device, internal.surface, &available_present_modes_count, nullptr));
        std::vector<VkPresentModeKHR> available_present_modes(available_present_modes_count);
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(internal.physical_device, internal.surface, &available_present_modes_count, available_present_modes.data()));

        VkSwapchainCreateInfoKHR swapchain_create_info
        {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = internal.surface,
            .minImageCount = internal.swapchain_image_count,
            .imageFormat = internal.swapchain_data.surface_format.format,
            .imageColorSpace = internal.swapchain_data.surface_format.colorSpace,
            .imageExtent = internal.swapchain_data.surface_extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = surface_capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = select_ideal_present_mode(available_present_modes),
            .clipped = true,
            .oldSwapchain = nullptr,
        };

        VK_CHECK(vkCreateSwapchainKHR(internal.device, &swapchain_create_info, nullptr, &internal.swapchain));

        std::vector<VkImage> swapchain_images(internal.swapchain_image_count);
        VK_CHECK(vkGetSwapchainImagesKHR(internal.device, internal.swapchain, &internal.swapchain_image_count, swapchain_images.data()));

        for (i32 i = 0; i < internal.swapchain_image_count; i++)
            internal.per_frame_data[i].swapchain_image = swapchain_images[i];

        create_swapchain_image_views();
    }

    void create_command_pool()
    {
        VkCommandPoolCreateInfo command_pool_create_info
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };

        VK_CHECK(vkCreateCommandPool(internal.device, &command_pool_create_info, nullptr, &internal.command_pool));
    }

    void create_command_buffers()
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = internal .command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = internal.swapchain_image_count,
        };

        VkCommandBuffer command_buffers[internal.swapchain_image_count];

        VK_CHECK(vkAllocateCommandBuffers(internal.device, &command_buffer_allocate_info, command_buffers));

        for (i32 i = 0; i < internal.swapchain_image_count; i++)
            internal.per_frame_data[i].command_buffer = command_buffers[i];
    }

    void create_sync_objects()
    {
        for (i32 i = 0; i < internal.swapchain_image_count; i++)
        {
            vkDestroySemaphore(internal.device, internal.per_frame_data[i].render_semaphore, nullptr);
            vkDestroySemaphore(internal.device, internal.per_frame_data[i].swapchain_semaphore, nullptr);
            vkDestroyFence(internal.device, internal.per_frame_data[i].render_fence, nullptr);
        }

        VkSemaphoreCreateInfo semaphore_info
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fence_info
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (i32 i = 0; i < internal.swapchain_image_count; i++)
        {
            auto& per_frame_data = internal.per_frame_data[i];
            VK_CHECK(vkCreateSemaphore(internal.device, &semaphore_info, nullptr, &per_frame_data.swapchain_semaphore));
            VK_CHECK(vkCreateSemaphore(internal.device, &semaphore_info, nullptr, &per_frame_data.render_semaphore));
            VK_CHECK(vkCreateFence(internal.device, &fence_info, nullptr, &per_frame_data.render_fence));
        }
    }

    void resize_swapchain()
    {
        // Wait until the device is idle to safely destroy resources
        vkDeviceWaitIdle(internal.device);
        vkDestroySwapchainKHR(internal.device, internal.swapchain, nullptr);

        // Recreate the swapchain and dependent resources
        create_swapchain();

        create_command_buffers();
    }

    void initialize(SDL_Window* sdl_window_ptr)
    {
        create_vulkan_instance();
        create_debug_messenger();

        internal.window_ptr = sdl_window_ptr;
        create_sdl_surface();

        select_vulkan_physical_device();
        create_vulkan_device();

        create_swapchain();
        create_swapchain_image_views();

        create_command_pool();
        create_command_buffers();
        create_sync_objects();
    }

    void terminate()
    {
        // TODO: Replace this with a deletion queue
        vkDestroySurfaceKHR(internal.instance, internal.surface, nullptr);
        vkDestroyDevice(internal.device, nullptr);
        vkDestroyInstance(internal.instance, nullptr);
    }

    const PerFrameData& begin_frame()
    {
        internal.last_swapchain_image_index = internal.current_swapchain_image_index;
        auto& last_per_frame_data = internal.per_frame_data[internal.last_swapchain_image_index];

        VkResult acquire_image_result = vkAcquireNextImageKHR(internal.device, internal.swapchain, UINT64_MAX, last_per_frame_data.swapchain_semaphore, nullptr, &internal.current_swapchain_image_index);
        auto& per_frame_data = internal.per_frame_data[internal.current_swapchain_image_index];

        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR)
            resize_swapchain();

        return per_frame_data;
    }

    void end_frame()
    {
        auto& per_frame_data = internal.per_frame_data[internal.current_swapchain_image_index];

        VK_CHECK(vkResetFences(internal.device, 1, &per_frame_data.render_fence));

        VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info =
        {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &per_frame_data.swapchain_semaphore,
            .pWaitDstStageMask = &stage_flags,
            .commandBufferCount = 1,
            .pCommandBuffers = &per_frame_data.command_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &per_frame_data.render_semaphore,
        };

        VK_CHECK(vkQueueSubmit(internal.queue, 1, &submit_info, per_frame_data.render_fence));

        VK_CHECK(vkWaitForFences(internal.device,1, &per_frame_data.render_fence, VK_TRUE, UINT64_MAX));

        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &per_frame_data.render_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &internal.swapchain,
            .pImageIndices = &internal.current_swapchain_image_index,
        };

        VkResult present_result = vkQueuePresentKHR(internal.queue, &present_info);

        if (present_result == VK_ERROR_OUT_OF_DATE_KHR)
            resize_swapchain();
    }

    VkDevice get_logical_device()
    {
        return internal.device;
    }

    const SwapchainData& get_swapchain_data()
    {
        return internal.swapchain_data;
    }
}
