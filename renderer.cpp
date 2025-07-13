#include "renderer.h"
#include <cstdio>
#include <vector>
#include "types.h"

#include <Vulkan/Vulkan.h>

struct
{
    VkInstance instance { VK_NULL_HANDLE };
    VkPhysicalDevice physical_device { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    u32 graphics_and_compute_queue_family_index { 0 };
} core;

bool create_vulkan_instance()
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
        printf("Failed to create Vulkan instance.\n");
        core.instance = VK_NULL_HANDLE;
        return false;
    }

    printf("Created Vulkan instance.\n");
    return true;
}

bool pick_vulkan_physical_device()
{
    // Get count of physical devices available
    u32 physical_device_count { 0 };
    VkResult result = vkEnumeratePhysicalDevices(core.instance, &physical_device_count, nullptr);

    // Get all physical devices available
    VkPhysicalDevice physical_devices[physical_device_count];
    vkEnumeratePhysicalDevices(core.instance, &physical_device_count, physical_devices);

    struct DeviceProperties
    {
        VkPhysicalDeviceProperties properties;
        VkPhysicalDeviceProperties2 properties2;
    };

    // Get the properties of all devices
    std::vector<DeviceProperties> device_properties(physical_device_count);

    for (u32 i = 0; i < physical_device_count; i++)
    {
        device_properties[i].properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        device_properties[i].properties2.pNext = &device_properties[i].properties;
        vkGetPhysicalDeviceProperties2(physical_devices[i], &device_properties[i].properties2);
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
            VkQueueFlags& flags = queue_family_properties[f].queueFlags;
            // Transfer queue is implicitly valid thanks to graphics/compute
            if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_COMPUTE_BIT))
            {
                core.physical_device = physical_devices[i];
                core.graphics_and_compute_queue_family_index = f;
                printf("Found and picked device with name: %s\n", device_properties[i].properties2.properties.deviceName);
                break;
            }
        }
    }

    return true;
}

bool create_vulkan_device()
{
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = nullptr;

    float queue_priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos(1);
    queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[0].pNext = nullptr;
    queue_create_infos[0].flags = 0;
    queue_create_infos[0].queueFamilyIndex = core.graphics_and_compute_queue_family_index;
    queue_create_infos[0].queueCount = 1;
    queue_create_infos[0].pQueuePriorities = &queue_priority;

    create_info.queueCreateInfoCount = queue_create_infos.size();
    create_info.pQueueCreateInfos = queue_create_infos.data();

    create_info.enabledExtensionCount = 0;
    create_info.ppEnabledExtensionNames = nullptr;

    bool created_device = vkCreateDevice(core.physical_device, &create_info, nullptr, &core.device) == VK_SUCCESS;

    if (!created_device)
    {
        printf("Failed to create Vulkan device.\n");
        core.device = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void Renderer::initialize()
{
    create_vulkan_instance();
    pick_vulkan_physical_device();
    create_vulkan_device();
}

void Renderer::update()
{

}

void Renderer::terminate()
{
    // TODO: Replace this with a deletion queue
    vkDestroyInstance(core.instance, nullptr);
}

