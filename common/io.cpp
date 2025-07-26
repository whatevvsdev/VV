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

    std::vector<std::filesystem::path> parse_dependencies_from_file(const std::string& file_data)
    {
        usize target_end_index = file_data.find(": ") + 1; // Skip colon and next white space
        std::string input = file_data.substr(target_end_index, file_data.size());

        // Normalize slash style used
        for (char& c : input)
        {
            if (c == '/')
                c = '\\';
        }

        std::vector<std::filesystem::path> paths;

        usize i { 0 };
        while (i < input.size())
        {
            if (i + 2 < input.size() && std::isalpha(input[i]) && input[i + 1] == ':' && input[i + 2] == '\\')
            {
                usize start { i };
                i += 3;

                // We are within the string, and the current character is printable
                while (i < input.size() && std::isprint(input[i]) && input[i] != ' ')
                {
                    i++;
                }

                std::filesystem::path path (input.substr(start, i - start));

                paths.push_back(path);
                continue;
            }
            i++;
        }

        return paths;
    }

    void watch_for_file_update(const std::filesystem::path& path, const std::function<void()>& on_update_callback)
    {
        auto entry = internal.watched_files.find(path);

        if (entry == internal.watched_files.end())
        {
            std::filesystem::path auto_full_path(path);

            internal.watched_files.insert({ path, {path, std::vector<std::function<void()>>(), 0} });
            entry = internal.watched_files.find(path);
            entry->second.last_write_time = static_cast<u64>(std::filesystem::last_write_time(path).time_since_epoch().count());

            std::filesystem::path dependencies_file = (path.string() + ".dependencies");
            if (std::filesystem::exists(dependencies_file))
            {
                auto dependencies_file_data = read_binary_file(dependencies_file);
                std::string depfile_content(reinterpret_cast<const char*>(dependencies_file_data.data()), dependencies_file_data.size());
                auto dependencies = parse_dependencies_from_file(depfile_content);

                for (const auto& dependency : dependencies)
                {
                    if (dependency.filename() != path.filename())
                    {
                        watch_for_file_update(dependency, on_update_callback);
                    }
                }
            }
        }

        entry->second.on_update_callbacks.push_back(on_update_callback);
    }

    void update()
    {
        for (auto& [key,watched_file] : internal.watched_files)
            watched_file.check_for_update_and_callback();
    }


}
