#include "renderer.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include "types.h"

#include <Vulkan/Vulkan.h>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_vulkan.h"

struct
{
    SDL_Window* window_ptr;

    VkInstance instance { VK_NULL_HANDLE };

    VkDebugUtilsMessengerEXT debug_messenger;

    VkPhysicalDevice physical_device { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    u32 queue_family_index { 0 }; // Supports Presentation, Graphics and Compute (and Transfer implicitly)

    VkSurfaceKHR surface { VK_NULL_HANDLE };
    VkSurfaceFormatKHR swapchain_surface_format { VK_FORMAT_UNDEFINED };
    VkExtent2D swapchain_surface_extent { 0, 0};
    u32 swapchain_image_count { 0 };
    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    std::vector<VkImage> swapchain_images {};
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
        .pNext = nullptr,
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
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

    bool created_instance = vkCreateInstance(&instance_create_info, nullptr, &core.instance) == VK_SUCCESS;

    if (!created_instance)
    {
        printf("Failed to create Vulkan instance.\n");
        core.instance = VK_NULL_HANDLE;
        return false;
    }

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

    auto vk_create_debug_utils_messenger_ext_void_function = vkGetInstanceProcAddr( core.instance, "vkCreateDebugUtilsMessengerEXT" );
    auto vk_create_debug_utils_messenger_ext_function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vk_create_debug_utils_messenger_ext_void_function);

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
    VkResult result = vkEnumeratePhysicalDevices(core.instance, &physical_device_count, nullptr);

    // Get all physical devices available
    VkPhysicalDevice physical_devices[physical_device_count];
    vkEnumeratePhysicalDevices(core.instance, &physical_device_count, physical_devices);

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
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], f, core.surface, &presentation_supported);

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
    VkDeviceCreateInfo device_create_info{};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = nullptr;

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
    };

    device_create_info.enabledExtensionCount = device_extensions.size();
    device_create_info.ppEnabledExtensionNames = device_extensions.data();

    VkResult result = vkCreateDevice(core.physical_device, &device_create_info, nullptr, &core.device);
    bool created_device = result == VK_SUCCESS;

    if (!created_device)
    {
        printf("Failed to create Vulkan device.\n");
        core.device = VK_NULL_HANDLE;
        return false;
    }
    printf("Created Vulkan device.\n");

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
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core.physical_device, core.surface, &surface_capabilities);

    u32 available_surface_format_count { 0 };
    vkGetPhysicalDeviceSurfaceFormatsKHR(core.physical_device, core.surface, &available_surface_format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> available_surface_formats(available_surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(core.physical_device, core.surface, &available_surface_format_count, available_surface_formats.data());

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
    vkGetPhysicalDeviceSurfacePresentModesKHR(core.physical_device, core.surface, &available_present_modes_count, nullptr);
    std::vector<VkPresentModeKHR> available_present_modes(available_present_modes_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(core.physical_device, core.surface, &available_present_modes_count, available_present_modes.data());

    VkSwapchainCreateInfoKHR swapchain_create_info {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = core.surface,
        .minImageCount = min_swapchain_image_count,
        .imageFormat = core.swapchain_surface_format.format,
        .imageColorSpace = core.swapchain_surface_format.colorSpace,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = select_ideal_present_mode(available_present_modes),
        .clipped = true,
        .oldSwapchain = nullptr,
    };

    VkResult result = vkCreateSwapchainKHR(core.device, &swapchain_create_info, nullptr, &core.swapchain);
    bool created_swapchain = result == VK_SUCCESS;

    core.swapchain_images.resize(core.swapchain_image_count);
    vkGetSwapchainImagesKHR(core.device, core.swapchain, &core.swapchain_image_count, core.swapchain_images.data());

    if (!created_swapchain)
    {
        printf("Failed to create Vulkan swpachain.\n");
        core.swapchain = VK_NULL_HANDLE;
        return false;
    }

    printf("Created Vulkan swpachain.\n");
    return true;
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
}

void Renderer::update()
{

}

void Renderer::terminate()
{
    // TODO: Replace this with a deletion queue
    vkDestroySurfaceKHR(core.instance, core.surface, nullptr);
    vkDestroyDevice(core.device, nullptr);
    vkDestroyInstance(core.instance, nullptr);
}

