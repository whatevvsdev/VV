#pragma once

#include <string>
#include <vector>
#include "../../common/types.h"
#include "vv_vulkan.h"

namespace ProfilingQueries
{
    struct Timing
    {
        std::string name;
        f32 time_ms{ 0.0f };
        f32 average_10_time_ms{ 0.0f };
        bool has_been_updated_this_frame{ false };
    };

    void initialize(VkPhysicalDevice physical_device, VkDevice device);
    void destroy(VkDevice device);
    void reset_device_profiling_queries(VkCommandBuffer command_buffer);
    void end_frame();

    void device_start(const std::string& name, VkCommandBuffer command_buffer);
    void device_stop(const std::string& name, VkCommandBuffer command_buffer);
    void host_start(const std::string& name);
    void host_stop(const std::string& name);

    Timing get_device_time_elapsed_ms(const std::string& name);
    Timing get_host_time_elapsed_ms(const std::string& name);
    std::vector<Timing> get_all_device_times_elapsed_ms();
    std::vector<Timing> get_all_host_times_elapsed_ms();
}