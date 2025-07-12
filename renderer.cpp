#include "renderer.h"
#include <cstdio>

#include <Vulkan/Vulkan.h>

struct
{
    VkInstance instance { VK_NULL_HANDLE };
} core;

bool init_vulkan_instance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VV";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VV";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.pNext = nullptr;
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = 0;

    bool created_instance = vkCreateInstance(&createInfo, nullptr, &core.instance) == VK_SUCCESS;

    if (!created_instance)
    {
        printf("Failed to initialize Vulkan instance.\n");
        core.instance = VK_NULL_HANDLE;
        return false;
    }

    printf("Initialized Vulkan instance.\n");
    return true;
}

void Renderer::initialize()
{
    init_vulkan_instance();
}

void Renderer::update()
{

}

void Renderer::terminate()
{
    // TODO: Replace this with a deletion queue
    if (core.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(core.instance, nullptr);
    }
}

