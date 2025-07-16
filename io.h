#pragma once
#include <filesystem>
#include <vector>
#include "types.h"

typedef std::filesystem::path FSPath;

namespace IO
{
    std::vector<u8> read_binary_file(const FSPath& path);
}
