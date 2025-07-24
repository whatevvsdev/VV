#include "io.h"
#include <fstream>

namespace IO
{
    struct FileUpdateWatcher
    {
        std::filesystem::path path;
        std::vector<std::function<void()>> on_update_callbacks;
        u64 last_write_time;

        void check_for_update_and_callback();
    };

    void FileUpdateWatcher::check_for_update_and_callback()
    {
        u64 new_last_write_time = static_cast<u64>(std::filesystem::last_write_time(path).time_since_epoch().count());
        if (last_write_time < new_last_write_time)
        {
            last_write_time = new_last_write_time;
            for (const auto & on_update_callback : on_update_callbacks)
                on_update_callback();
        }
    }

    struct
    {
        std::unordered_map<std::filesystem::path, FileUpdateWatcher> watched_files;
    } internal;

    std::vector<u8> read_binary_file(const std::filesystem::path& path)
    {
        std::vector<u8> buffer(0);
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            printf("Failed to read file %ls.\n", path.c_str());
            return buffer;
        }

        buffer.resize(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<std::fstream::char_type*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));

        return buffer;
    }

    void watch_for_file_update(const std::filesystem::path& path, std::function<void()>&& on_update_callback)
    {
        auto entry = internal.watched_files.find(path);

        if (entry == internal.watched_files.end())
        {
            internal.watched_files.insert({ path, {path, std::vector<std::function<void()>>(), 0} });
            entry = internal.watched_files.find(path);
            entry->second.last_write_time = static_cast<u64>(std::filesystem::last_write_time(path).time_since_epoch().count());
            printf("last write time of file %ls was %llu\n.", path.c_str(), entry->second.last_write_time);
        }

        entry->second.on_update_callbacks.push_back(on_update_callback);
    }

    void update()
    {
        for (auto& [key,watched_file] : internal.watched_files)
            watched_file.check_for_update_and_callback();
    }


}
