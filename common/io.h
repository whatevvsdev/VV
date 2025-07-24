#pragma once
#include <filesystem>
#include <functional>
#include <vector>
#include "types.h"

namespace IO
{
    std::vector<u8> read_binary_file(const std::filesystem::path& path);
    void watch_for_file_update(const std::filesystem::path& file_path, std::function<void()>&& callback);

    void update();
}
