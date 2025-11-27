#include "voxel_model.h"
#include "../renderer/renderer_core.h"
#include "../renderer/device_resources.h"
#include "../../common/io.h"

#include "ogt_vox.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>


typedef u32 Voxel;
typedef u64 VoxelOccupancyBrick; // We use a 4^3 brick
constexpr u32 VOXEL_BRICK_SIZE = 4;
constexpr u32 VOXELS_PER_BRICK = VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE;

constexpr u32 voxel_count_to_brick_count(u32 voxel_count)
{
    return (voxel_count + VOXELS_PER_BRICK - 1) / VOXELS_PER_BRICK;
}

inline u32 round_up_to_multiple(u32 value, u32 multiple)
{
    return (value + multiple - 1) / multiple * multiple;
}

/* TODO: this is currently technically an instance, not a model
    but it makes no sense to split it atm.
*/
struct alignas(16) DeviceVoxelModelInstanceData
{
    glm::ivec4 size_in_bricks;
    glm::ivec4 brick_index_and_size_in_voxels;
    glm::mat4 inverse_transform;
};

struct VoxelModelData
{
    struct InstanceData
    {
        glm::mat4 inverse_transform;
        u32 model_index;
    };

    std::vector<VoxelOccupancyBrick> voxel_occupancy_bricks;
    //std::vector<Voxel> voxels;
    glm::ivec3 size { glm::ivec3(0) };
    glm::ivec3 size_in_bricks { glm::ivec3(0) };
    std::vector<InstanceData> instances;
};

struct
{
    std::unordered_map<std::string, VoxelModelData> voxel_models;
} internal;

constexpr i32 instance_count = 64;

struct
{
      DeviceVoxelModelInstanceData instances[instance_count];
} device;


void VoxelModels::upload_models_to_gpu()
{
    for (i32 i = 0; i < instance_count; i++)
    {
        device.instances[i].size_in_bricks = glm::ivec4(0, 0, 0, 0);
        device.instances[i].brick_index_and_size_in_voxels = glm::ivec4(0, 0, 0, 0);
        device.instances[i].inverse_transform = glm::mat4(0.0f);
    }

    u32 voxel_brick_count_of_all_models_combined { 0 };

    for (auto& [key, voxel_model] : internal.voxel_models)
    {
        auto model_size_in_voxels = voxel_model.size.x * voxel_model.size.y * voxel_model.size.z;
        voxel_brick_count_of_all_models_combined += voxel_count_to_brick_count(model_size_in_voxels);
    }

    auto created_buffer = DeviceResources::create_buffer("voxel_data", sizeof(DeviceVoxelModelInstanceData) * instance_count + voxel_brick_count_of_all_models_combined * sizeof(VoxelOccupancyBrick));

    i32 header_data_size = instance_count * sizeof(DeviceVoxelModelInstanceData);
    i32 total_data_size = header_data_size + voxel_brick_count_of_all_models_combined * sizeof(VoxelOccupancyBrick);
    u8* mapped_data { nullptr };
    mapped_data = new u8[total_data_size];

    // Copy voxel data to GPU and set instance data
    i32 voxel_brick_offset { 0 };
    i32 instance_index { 0 };
    for (auto& [key, voxel_model] : internal.voxel_models)
    {
        i32 volume = voxel_model.size.x * voxel_model.size.y * voxel_model.size.z;
        u32 brick_count = voxel_count_to_brick_count(volume);

        memcpy(mapped_data + header_data_size + voxel_brick_offset * static_cast<i32>(sizeof(VoxelOccupancyBrick)), voxel_model.voxel_occupancy_bricks.data(), brick_count * sizeof(VoxelOccupancyBrick));

        for (auto& instance : voxel_model.instances)
        {
            auto& writing_instance = device.instances[instance_index];
            writing_instance.size_in_bricks = glm::ivec4(voxel_model.size_in_bricks, 0);
            writing_instance.brick_index_and_size_in_voxels = glm::ivec4(voxel_brick_offset, voxel_model.size);
            writing_instance.inverse_transform = instance.inverse_transform;
            instance_index++;
        }

        voxel_brick_offset += static_cast<i32>(brick_count);
    }

    // Copy instance headers to GPU
    memcpy(mapped_data, device.instances, header_data_size);

    DeviceResources::immediate_copy_data_to_gpu("voxel_data", mapped_data, total_data_size);
    delete[] mapped_data;

}

// Holy this is a mess, but it works
void transform_vox_transform_to_engine_transform(glm::mat4& transform)
{
    glm::vec3 eulerRadians = glm::eulerAngles(glm::quat_cast(glm::mat3(transform)));
    auto swap = eulerRadians.y; eulerRadians.y = eulerRadians.z; eulerRadians.z = -swap;

    glm::vec3 position = glm::vec3(transform[3]);
    swap = position.y; position.y = position.z; position.z = -swap;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotation    = glm::eulerAngleXYZ(eulerRadians.x, eulerRadians.y, eulerRadians.z);

    transform = translation * rotation;
}

void VoxelModels::load(std::filesystem::path path, glm::ivec3 repeat)
{
    auto ogt_file = IO::read_binary_file(path);
    const ogt_vox_scene* scene = ogt_vox_read_scene(ogt_file.data(), ogt_file.size());
    auto filename = path.filename().string();

    std::vector<VoxelModelData> new_models;

    for (u32 i = 0u; i < scene->num_models; i++)
    {
        auto& ogt_model = *scene->models[i];

        VoxelModelData voxel_model;
        voxel_model.size = glm::ivec3(round_up_to_multiple(ogt_model.size_x * repeat.x, VOXEL_BRICK_SIZE), round_up_to_multiple(ogt_model.size_z * repeat.y, VOXEL_BRICK_SIZE), round_up_to_multiple(ogt_model.size_y * repeat.z, VOXEL_BRICK_SIZE)); // VOX has different space so we use X Z Y
        voxel_model.size_in_bricks = voxel_model.size / glm::ivec3(VOXEL_BRICK_SIZE);
        u32 voxel_volume = voxel_model.size.x * voxel_model.size.y * voxel_model.size.z;
        voxel_model.voxel_occupancy_bricks.resize(voxel_count_to_brick_count(voxel_volume), 0);
        // TODO: Technically all of our voxel models are now aligned to a size of 64, so we
        //       don't need voxel_count_to_brick_count anymore and can just divide by 64

        auto& new_size = voxel_model.size;
        auto old_size = glm::ivec3(ogt_model.size_x, ogt_model.size_y, ogt_model.size_z);

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
                                    (brick_position.y * voxel_model.size_in_bricks.x) +
                                    (brick_position.z * voxel_model.size_in_bricks.x * voxel_model.size_in_bricks.y);

                                u32 brick_local_index =
                                    (brick_local_position.x * 1) +
                                    (brick_local_position.y * VOXEL_BRICK_SIZE) +
                                    (brick_local_position.z * VOXEL_BRICK_SIZE * VOXEL_BRICK_SIZE);

                                if (ogt_model.voxel_data[vox_index] != 0)
                                    voxel_model.voxel_occupancy_bricks[brick_index] |= (1ull << brick_local_index);
                            }
                        }
                    }
                }
            }
        }

        new_models.push_back(voxel_model);
    }

    for (u32 i = 0u; i < scene->num_instances; i++)
    {
        auto& ogt_instance = scene->instances[i];

        VoxelModelData::InstanceData  model_instance;
        model_instance.model_index = ogt_instance.model_index + internal.voxel_models.size();
        glm::mat4 transform = glm::make_mat4(&ogt_instance.transform.m00);

        transform_vox_transform_to_engine_transform(transform);

        model_instance.inverse_transform = glm::inverse(transform);

        new_models[ogt_instance.model_index].instances.push_back(model_instance);
    }

    u32 index { 0 };
    for (auto& model : new_models)
    {
        internal.voxel_models[filename + std::to_string(index)] = model;

        index += 1;
    }

    ogt_vox_destroy_scene(scene);
}
