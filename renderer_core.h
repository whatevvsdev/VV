#pragma once

struct SDL_Window;

#define RENDERER_DEBUG 1

#include <vulkan/vulkan_core.h>

namespace Renderer::Core
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

    void initialize(SDL_Window* sdl_window_ptr);
    void terminate();

    const PerFrameData& begin_frame();
    void end_frame();

    VkDevice get_logical_device();
    const SwapchainData&  get_swapchain_data();
}
