#include "profiling.h"

#include "renderer_core.h"
#include <unordered_map>

#include "SDL3/SDL_timer.h"

// TODO: This code is currently NOT multithread safe

/* TODO: This code is also kind of scuffed at the moment, you have to get timings before it resets, but during
    a command buffer recording (I think)
*/

// This is the maximum amount of queries the user can make, the device gets double to query start + end
constexpr u32 MAX_TIMESTAMP_QUERIES { 64 };
constexpr u32 MAX_TIMESTAMP_QUERY_SLOTS { MAX_TIMESTAMP_QUERIES * 2 };

struct DeviceTimingQueryData
{
    i32 frames_since_query { 0 };
    f32 last_time{ 0.0f }; // Do not set manually

    u32 last_10_write_index { 0 };
    f32 last_10_times[10] {};

    void set_new_time(f32);
    u32 get_index() const { return index * 2; };

    DeviceTimingQueryData(u32 in_index) : index(in_index) {};
    u32 index;
};

void DeviceTimingQueryData::set_new_time(f32 new_time)
{
    last_time = new_time;
    last_10_times[last_10_write_index % 10] = new_time;
    last_10_write_index++;
}

struct HostTimingQueryData
{
    i32 frames_since_query { 0 };
    f32 last_time{ 0.0f }; // Do not set manually
    u32 last_10_write_index { 0 };
    f32 last_10_times[10]{};

    u64 start_time{ 0u };
    u64 end_time{ 0u };

    void set_new_time(f32);
};

void HostTimingQueryData::set_new_time(f32 new_time)
{
    last_time = new_time;
    last_10_times[last_10_write_index % 10] = new_time;
    last_10_write_index++;
}

struct
{
    std::unordered_map<std::string, DeviceTimingQueryData> device_profiling_timing_queries;
    std::unordered_map<std::string, HostTimingQueryData> host_profiling_timing_queries;

    // Command buffer time querying
    VkQueryPool timestamp_query_pool{};
    f32 device_timestamp_nanoseconds_per_query_increment{ 0.0f };
    u32 last_query_index{ 0 };

    f32 host_timestamp_ticks_per_second { 0.0f };
    bool timestamp_supported_on_graphics_and_compute{ false };
} internal;

void ProfilingQueries::initialize(VkPhysicalDevice physical_device, VkDevice device)
{
    VkQueryPoolCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = {}; // Flags are reserved for future use
    create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    create_info.queryCount = MAX_TIMESTAMP_QUERY_SLOTS;

    VK_CHECK(vkCreateQueryPool(device, &create_info, nullptr, &internal.timestamp_query_pool));

    // Get time step of query (in nanoseconds) and support for compute timestamps
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    auto& limits = properties.limits;

    internal.timestamp_supported_on_graphics_and_compute = limits.timestampComputeAndGraphics == VK_TRUE;
    internal.device_timestamp_nanoseconds_per_query_increment = limits.timestampPeriod;

    internal.host_timestamp_ticks_per_second = static_cast<f64>(SDL_GetPerformanceFrequency());

    if (!internal.timestamp_supported_on_graphics_and_compute)
    {
        printf("Timestamps are not supported on compute or graphics queues.\n");
    }
}

void ProfilingQueries::terminate(VkDevice device)
{
    vkDestroyQueryPool(device, internal.timestamp_query_pool, nullptr);
}

void ProfilingQueries::reset_device_profiling_queries(VkCommandBuffer command_buffer)
{
    vkCmdResetQueryPool(command_buffer, internal.timestamp_query_pool, 0, MAX_TIMESTAMP_QUERY_SLOTS);
}

void ProfilingQueries::end_frame()
{
    for (auto& [key, timing_query] : internal.device_profiling_timing_queries)
        timing_query.frames_since_query++;
    for (auto& [key, timing_query] : internal.host_profiling_timing_queries)
        timing_query.frames_since_query++;
}

void ProfilingQueries::device_start(const std::string& name, VkCommandBuffer command_buffer)
{
    bool can_query = internal.timestamp_supported_on_graphics_and_compute;

    if (can_query)
    {
        auto potential_query = internal.device_profiling_timing_queries.find(name);
        bool query_already_exists = potential_query != internal.device_profiling_timing_queries.end();

        if (!query_already_exists)
        {
            if (internal.last_query_index == MAX_TIMESTAMP_QUERIES)
            {
                printf("Exceeded the maximum number of timestamps");
                return;
            }

            potential_query = internal.device_profiling_timing_queries.insert({ name, DeviceTimingQueryData(internal.last_query_index) }).first;
            internal.last_query_index++;
        }

        auto& query = potential_query->second;

        vkCmdWriteTimestamp2(command_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, internal.timestamp_query_pool, query.get_index());
    }
}

void ProfilingQueries::device_stop(const std::string& name, VkCommandBuffer command_buffer)
{
    auto potential_query = internal.device_profiling_timing_queries.find(name);
    bool query_exists = potential_query != internal.device_profiling_timing_queries.end();
    bool can_query = internal.timestamp_supported_on_graphics_and_compute;

    if (query_exists && can_query)
    {
        auto& query = potential_query->second;
        query.frames_since_query = 0;

        u32 querying = query.get_index() + 1;

        vkCmdWriteTimestamp2(command_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, internal.timestamp_query_pool, querying);
    }
}

void ProfilingQueries::host_start(const std::string& name)
{
    auto potential_query = internal.host_profiling_timing_queries.find(name);
    bool query_already_exists = potential_query != internal.host_profiling_timing_queries.end();

    if (!query_already_exists)
    {
        internal.host_profiling_timing_queries.insert({ name, HostTimingQueryData() });
        potential_query = internal.host_profiling_timing_queries.find(name);
    }

    auto& query = potential_query->second;

    query.start_time = SDL_GetPerformanceCounter();
}

void ProfilingQueries::host_stop(const std::string& name)
{
    auto potential_query = internal.host_profiling_timing_queries.find(name);
    bool query_exists = potential_query != internal.host_profiling_timing_queries.end();

    if (query_exists)
    {
        auto& query = potential_query->second;
        query.frames_since_query = 0;
        query.end_time = SDL_GetPerformanceCounter();
    }
}

ProfilingQueries::Timing ProfilingQueries::get_device_time_elapsed_ms(const std::string& name)
{
    Timing current_timing;

    auto potential_query = internal.device_profiling_timing_queries.find(name);
    bool query_exists = potential_query != internal.device_profiling_timing_queries.end();
    bool can_query = internal.timestamp_supported_on_graphics_and_compute;

    if (!query_exists or !can_query)
        return current_timing;

    auto& query = potential_query->second;
    bool new_timing = query.frames_since_query <= 1;
    if (new_timing)
    {
        u32 query_start = query.get_index();

        u64 buffer[2];
        VK_CHECK(vkGetQueryPoolResults(Renderer::Core::get_logical_device(), internal.timestamp_query_pool, query_start, 2, sizeof(u64) * 2, buffer, sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

        f64 milliseconds_per_query_increment = internal.device_timestamp_nanoseconds_per_query_increment / 1000000.0f;
        f64 ms_elapsed = static_cast<f64>(buffer[1] - buffer[0]) * milliseconds_per_query_increment;

        query.set_new_time(ms_elapsed);
    }

    current_timing.has_been_updated_this_frame = new_timing;
    current_timing.time_ms = query.last_time;

    current_timing.average_10_time_ms = 0.0f;
    for (i32 i = 0; i < std::min(query.last_10_write_index, 10u); i++)
        current_timing.average_10_time_ms += query.last_10_times[i];
    current_timing.average_10_time_ms /= static_cast<f32>(std::min(query.last_10_write_index, 10u));

    current_timing.name = name;

    return current_timing;
}

ProfilingQueries::Timing ProfilingQueries::get_host_time_elapsed_ms(const std::string& name)
{
    Timing current_timing;

    auto potential_query = internal.host_profiling_timing_queries.find(name);
    bool query_exists = potential_query != internal.host_profiling_timing_queries.end();

    if (!query_exists)
        return current_timing;

    auto& query = potential_query->second;

    bool new_timing = query.frames_since_query <= 1;
    if (new_timing)
    {
        u64 diff = (query.end_time - query.start_time);
        f64 smaller = static_cast<f64>(diff) / internal.host_timestamp_ticks_per_second;
        query.set_new_time(smaller * 1000.0f);
    }

    current_timing.has_been_updated_this_frame = new_timing;
    current_timing.time_ms = query.last_time;

    current_timing.average_10_time_ms = 0.0f;
    for (i32 i = 0; i < query.last_10_write_index && i < 10; i++)
        current_timing.average_10_time_ms += query.last_10_times[i];
    current_timing.average_10_time_ms /= static_cast<f32>(std::min(query.last_10_write_index, 10u));

    current_timing.name = name;

    return current_timing;
}

std::vector<ProfilingQueries::Timing> ProfilingQueries::get_all_device_times_elapsed_ms()
{
    std::vector<Timing> timings;
    timings.reserve(internal.device_profiling_timing_queries.size());

    for (auto& [key, timing_entry] : internal.device_profiling_timing_queries)
    {
        timings.push_back(get_device_time_elapsed_ms(key));
    }

    return timings;
}

std::vector<ProfilingQueries::Timing> ProfilingQueries::get_all_host_times_elapsed_ms()
{
    std::vector<Timing> timings;
    timings.reserve(internal.host_profiling_timing_queries.size());

    for (auto& [key, timing_entry] : internal.host_profiling_timing_queries)
    {
        timings.push_back(get_host_time_elapsed_ms(key));
    }

    return timings;
}
