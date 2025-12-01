#include "voxel_raw.h"

Data::RawVoxelModel Data::build_raw_voxel_model(const ogt_vox_model& model, glm::ivec3 repeat)
{
    RawVoxelModel raw_model;

    // VOX has different space so we use X Z Y to get the size in voxels
    raw_model.size = glm::uvec3(model.size_x * repeat.x, model.size_z * repeat.y, model.size_y * repeat.z);
    u32 voxel_volume = raw_model.size.x * raw_model.size.y * raw_model.size.z;

    raw_model.voxels.resize(voxel_volume);

    auto& new_size = raw_model.size;
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

                            i32 new_index = new_x + new_y * new_size.x + new_z * new_size.x * new_size.y;

                            if (model.voxel_data[vox_index] != 0)
                                raw_model.voxels[new_index] = 1;
                        }
                    }
                }
            }
        }
    }

    return raw_model;
}
