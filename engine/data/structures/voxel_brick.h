#pragma once
#include "voxel_raw.h"
#include "../../common/types.h"
#include "ogt_vox.h"

#include <vector>
#include <glm/glm.hpp>

namespace Data::AS
{
    typedef u64 VoxelOccupancyBrick; // We use a 4^3 brick
    constexpr u32 VOXEL_BRICK_SIZE = 4u;
    constexpr u32 VOXELS_PER_BRICK = VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE;

    struct VoxelBrickAS
    {
        std::vector<VoxelOccupancyBrick> bricks;
        glm::uvec3 size_in_bricks;
    };

    VoxelBrickAS build_brick_AS(const RawVoxelModel& model);
}
