#pragma once

#include "../../common/types.h"
#include "ogt_vox.h"

#include <vector>
#include <glm/glm.hpp>

namespace Data
{
    struct RawVoxelModel
    {
        glm::ivec3 size;
        std::vector<u8> voxels;
    };

    RawVoxelModel build_raw_voxel_model(const ogt_vox_model& model, glm::ivec3 repeat);
}