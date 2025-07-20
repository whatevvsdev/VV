#pragma once

struct SDL_Window;

#define RENDERER_DEBUG 1

#include <vulkan/vulkan_core.h>
#include "vk_mem_alloc.h"
#include <string>

#define VK_PROC_ADDR_LOAD(string_name) reinterpret_cast<PFN_##string_name>(vkGetInstanceProcAddr(internal.instance, #string_name))

#define VK_NAME(handle, object_type, name) \
{ \
    std::string name_object = name; \
    VkDebugUtilsObjectNameInfoEXT name_info{}; \
    name_info.objectHandle = (u64)handle; \
    name_info.objectType = object_type; \
    name_info.pNext = nullptr; \
    name_info.pObjectName = name_object.c_str(); \
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT; \
    VK_PROC_ADDR_LOAD(vkSetDebugUtilsObjectNameEXT)(internal.device, &name_info); \
}

namespace Renderer
{
    struct PerFrameData
    {
        VkSemaphore render_semaphore { VK_NULL_HANDLE };
        VkFence render_fence { VK_NULL_HANDLE };
        VkCommandBuffer command_buffer { VK_NULL_HANDLE };

        VkImage swapchain_image { VK_NULL_HANDLE };
        VkImageView swapchain_image_view { VK_NULL_HANDLE };
    };

    struct SwapchainData
    {
        VkSurfaceFormatKHR surface_format { VK_FORMAT_UNDEFINED };
        VkExtent2D surface_extent { 0, 0};
    };

    struct AllocatedImage
    {
        VkImage image;
        VkImageView view;
        VmaAllocation allocation;
        VkExtent3D extent;
        VkFormat format;
    };

    namespace Core
    {
        AllocatedImage create_image(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage_flags, VkImageAspectFlags aspect_flags, const std::string& name = "");

        void initialize(SDL_Window* sdl_window_ptr);
        void terminate();

        const PerFrameData& begin_frame();
        void end_frame();

        VkDevice get_logical_device();
        const SwapchainData&  get_swapchain_data();
    }
}
