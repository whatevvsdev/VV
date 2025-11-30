#pragma once
#include "../../common/types.h"
#include "ogt_vox.h"

#include <vector>
#include <glm/glm.hpp>

typedef u64 VoxelOccupancyBrick; // We use a 4^3 brick
constexpr u32 VOXEL_BRICK_SIZE = 4;
constexpr u32 VOXELS_PER_BRICK = VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE;

namespace Data::AS
{
    struct VoxelBrickAS
    {
        std::vector<VoxelOccupancyBrick> bricks;
        glm::uvec3 size_in_bricks;
    };

    VoxelBrickAS build_brick_AS(const ogt_vox_model& model, glm::ivec3 repeat);
}
