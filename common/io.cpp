#include "io.h"
#include <fstream>

namespace IO
{
    std::vector<u8> read_binary_file(const FSPath& path)
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
}
