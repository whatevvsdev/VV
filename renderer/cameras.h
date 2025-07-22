#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include "../common/types.h"

namespace Renderer
{
    struct CameraInstanceData
    {
        glm::mat4 camera_matrix{ glm::mat4(1) };
    };

    namespace Cameras
    {
        void start_of_frame_update();
        CameraInstanceData get_current_camera_data_copy();

        void set_current_camera(u32 index);
        void set_current_camera_matrix(glm::mat4 matrix);
    }
}
