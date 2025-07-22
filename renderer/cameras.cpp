#include "cameras.h"
#include <vector>

struct
{
    u32 current_camera_index{ 0 };
    std::vector<Renderer::CameraInstanceData> camera_instances{ 1 };
} internal;

namespace Renderer
{
    CameraInstanceData& get_current_camera_ref()
    {
        return internal.camera_instances[internal.current_camera_index];
    }

    CameraInstanceData Cameras::get_current_camera_data_copy()
    {
        return get_current_camera_ref();
    }

    void Cameras::set_current_camera(u32 index)
    {
        internal.current_camera_index = index;
    }

    void Cameras::set_current_camera_matrix(glm::mat4 matrix)
    {
        get_current_camera_ref().camera_matrix = matrix;
    }
}
