#include "voxel_brick.h"
#include "../../common/math.h"

namespace Data::AS
{
    constexpr u32 voxel_count_to_brick_count(u32 voxel_count)
    {
        return (voxel_count + VOXELS_PER_BRICK - 1) / VOXELS_PER_BRICK;
    }

    VoxelBrickAS build_brick_AS(const ogt_vox_model& model, glm::ivec3 repeat)
    {
        VoxelBrickAS brick_as;

        // VOX has different space so we use X Z Y to get the size in voxels
        glm::uvec3 size_in_voxels = glm::uvec3(round_up_to_multiple(model.size_x * repeat.x, VOXEL_BRICK_SIZE), round_up_to_multiple(model.size_z * repeat.y, VOXEL_BRICK_SIZE), round_up_to_multiple(model.size_y * repeat.z, VOXEL_BRICK_SIZE));
        u32 voxel_volume = size_in_voxels.x * size_in_voxels.y * size_in_voxels.z;
        brick_as.size_in_bricks = size_in_voxels / VOXEL_BRICK_SIZE;

        brick_as.bricks.resize(voxel_count_to_brick_count(voxel_volume), 0);
        // TODO: Technically all of our voxel models are now aligned to a size of 64, so we
        //       don't need voxel_count_to_brick_count anymore and can just divide by 64

        auto& new_size = size_in_voxels;
        auto old_size = glm::ivec3(model.size_x, model.size_y, model.size_z);

        for (i32 z = 0; z < old_size.z; z++)
        {
            for (i32 y = 0; y < old_size.y; y++)
            {
                for (i32 x = 0; x < old_size.x; x++)
                {
                    i32 vox_index = x + y * old_size.x + z * old_size.x * old_size.y;

                    for (i32 rz = 0; rz < repeat.z; rz++)
                    {
                        for (i32 ry = 0; ry < repeat.y; ry++)
                        {
                            for (i32 rx = 0; rx < repeat.x; rx++)
                            {
                                i32 new_x = x + old_size.x * rx;
                                i32 new_y = z + old_size.z * rz;
                                i32 new_z = old_size.y - 1 - y + old_size.y * ry;

                                //i32 index = new_x + new_y * new_size.x + new_z * new_size.x * new_size.y;

                                glm::uvec3 voxel_position = glm::uvec3(new_x, new_y, new_z);
                                glm::uvec3 brick_position = voxel_position / VOXEL_BRICK_SIZE;
                                glm::uvec3 brick_local_position = voxel_position % VOXEL_BRICK_SIZE;

                                u32 brick_index =
                                    (brick_position.x * 1) +
                                    (brick_position.y * brick_as.size_in_bricks.x) +
                                    (brick_position.z * brick_as.size_in_bricks.x * brick_as.size_in_bricks.y);

                                u32 brick_local_index =
                                    (brick_local_position.x * 1) +
                                    (brick_local_position.y * VOXEL_BRICK_SIZE) +
                                    (brick_local_position.z * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE);

                                if (model.voxel_data[vox_index] != 0)
                                    brick_as.bricks[brick_index] |= (1ull << brick_local_index);
                            }
                        }
                    }
                }
            }
        }

        return brick_as;
    }
}
