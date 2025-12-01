#include "voxel_brick.h"

#include <cstdio>

#include "../../common/math.h"

namespace Data::AS
{
    VoxelBrickAS build_brick_AS(const RawVoxelModel& model)
    {

        VoxelBrickAS brick_as;

        // VOX has different space so we use X Z Y to get the size in voxels
        glm::uvec3 brick_aligned_size_in_voxels = glm::uvec3(round_up_to_multiple(model.size.x, VOXEL_BRICK_SIZE), round_up_to_multiple(model.size.y, VOXEL_BRICK_SIZE), round_up_to_multiple(model.size.z, VOXEL_BRICK_SIZE));
        u32 brick_aligned_voxel_volume = brick_aligned_size_in_voxels.x * brick_aligned_size_in_voxels.y * brick_aligned_size_in_voxels.z;
        brick_as.size_in_bricks = brick_aligned_size_in_voxels / VOXEL_BRICK_SIZE;
        brick_as.bricks.resize(brick_aligned_voxel_volume / VOXELS_PER_BRICK, 0);

        for (i32 z = 0, i = 0; z < model.size.z; z++)
        {
            for (i32 y = 0; y < model.size.y; y++)
            {
                for (i32 x = 0; x < model.size.x; x++, i++)
                {
                    glm::uvec3 voxel_position = glm::uvec3(x, y, z);
                    glm::uvec3 brick_position = voxel_position >> glm::uvec3(2);
                    glm::uvec3 brick_local_position = voxel_position & glm::uvec3(3);

                    u32 brick_index =
                        (brick_position.x * 1) +
                        (brick_position.y * brick_as.size_in_bricks.x) +
                        (brick_position.z * brick_as.size_in_bricks.x * brick_as.size_in_bricks.y);

                    u32 brick_local_index =
                        (brick_local_position.x * 1) +
                        (brick_local_position.y * VOXEL_BRICK_SIZE) +
                        (brick_local_position.z * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE);

                    if (model.voxels[i] != 0)
                        brick_as.bricks[brick_index] |= (1 << brick_local_index);
                }
            }
        }

        return brick_as;
    }
}
