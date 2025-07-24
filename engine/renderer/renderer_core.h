#pragma once

#define RENDERER_DEBUG 1

#include "vv_vulkan.h"
#include "vk_mem_alloc.h"
#include <string>
#include "../../common/types.h"

#if RENDERER_DEBUG
#define VK_NAME(device, handle, object_type, name) \
{ \
    std::string name_object = name; \
    VkDebugUtilsObjectNameInfoEXT name_info{}; \
    name_info.objectHandle = (u64)handle; \
    name_info.objectType = object_type; \
    name_info.pNext = nullptr; \
    name_info.pObjectName = name_object.c_str(); \
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT; \
    vkSetDebugUtilsObjectNameEXT(device, &name_info); \
}
#else
#define VK_NAME(device, handle, object_type, name) {device; handle; object_type; name;}
#endif


// TODO: Move this elsewhere
inline u32 aligned_size(u32 value, u32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

struct SDL_Window;

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

    struct PhysicalDeviceProperties
    {
        VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties;
        VkPhysicalDeviceProperties2 properties;
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
        const SwapchainData& get_swapchain_data();
        const PhysicalDeviceProperties& get_physical_device_properties();
        const VmaAllocator& get_vma_allocator();
        const PerFrameData& get_current_frame_data();
    }
}
