#include <filesystem>
#include <glm/glm.hpp>

namespace VoxelModels
{
    void load(std::filesystem::path path, glm::ivec3 repeat = glm::ivec3(1));
    void upload_models_to_gpu();
}
