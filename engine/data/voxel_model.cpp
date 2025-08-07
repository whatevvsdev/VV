﻿#include "voxel_model.h"
#include "../renderer/renderer_core.h"
#include "../renderer/device_resources.h"
#include "../../common/io.h"

#include "ogt_vox.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>


typedef u32 Voxel;

/* TODO: this is currently technically an instance, not a model
    but it makes no sense to split it atm.
*/
struct alignas(16) DeviceVoxelModelInstanceData
{
    glm::ivec4 index_and_size;
    glm::mat4 inverse_transform;
};

struct VoxelModelData
{
    struct InstanceData
    {
        glm::mat4 inverse_transform;
        u32 model_index;
    };

    std::vector<Voxel> voxels;
    glm::ivec3 size;
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
        device.instances[i].index_and_size = glm::ivec4(0, 0, 0, 0);
        device.instances[i].inverse_transform = glm::mat4(0.0f);
    }

    u32 total_model_size_in_voxels { 0 };

    for (auto& [key, voxel_model] : internal.voxel_models)
    {
        auto model_size_in_voxels = voxel_model.size.x * voxel_model.size.y * voxel_model.size.z;
        total_model_size_in_voxels += model_size_in_voxels;
    }
    std::vector<u32> voxels(total_model_size_in_voxels);

    auto created_buffer = DeviceResources::create_buffer("voxel_data", sizeof(DeviceVoxelModelInstanceData  ) * instance_count + total_model_size_in_voxels * sizeof(Voxel), true);

    i32 header_data_size = instance_count * sizeof(DeviceVoxelModelInstanceData);
    u8* mapped_data { nullptr };
    vmaMapMemory(Renderer::Core::get_vma_allocator(), created_buffer.allocation, reinterpret_cast<void**>(&mapped_data));
    memset(mapped_data, 0, header_data_size + total_model_size_in_voxels * sizeof(Voxel));
    // Copy voxel data to GPU and set instance data

    i32 voxel_offset { 0 };
    i32 instance_index { 0 };
    for (auto& [key, voxel_model] : internal.voxel_models)
    {
        i32 volume = voxel_model.size.x * voxel_model.size.y * voxel_model.size.z;

        memcpy(mapped_data + header_data_size + voxel_offset * static_cast<i32>(sizeof(Voxel)), voxel_model.voxels.data(), volume * sizeof(Voxel));

        for (auto& instance : voxel_model.instances)
        {
            auto& writing_instance = device.instances[instance_index];
            writing_instance.index_and_size = glm::ivec4(voxel_offset, voxel_model.size);
            writing_instance.inverse_transform = instance.inverse_transform;
            instance_index++;
        }

        voxel_offset += volume;
    }

    // Copy instance headers to GPU
    memcpy(mapped_data, device.instances, header_data_size);

    vmaUnmapMemory(Renderer::Core::get_vma_allocator(), created_buffer.allocation);
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

void VoxelModels::load(std::filesystem::path path)
{
    auto ogt_file = IO::read_binary_file(path);
    const ogt_vox_scene* scene = ogt_vox_read_scene(ogt_file.data(), ogt_file.size());
    auto filename = path.filename().string();

    std::vector<VoxelModelData> new_models;

    for (u32 i = 0u; i < scene->num_models; i++)
    {
        auto& ogt_model = *scene->models[i];
        u32 volume = ogt_model.size_x * ogt_model.size_y * ogt_model.size_z;

        VoxelModelData voxel_model;
        voxel_model.size = glm::ivec3(ogt_model.size_x, ogt_model.size_z, ogt_model.size_y); // VOX has different space so we use X Z Y
        voxel_model.voxels.resize(volume);

        auto& new_size = voxel_model.size;
        auto old_size = glm::ivec3(ogt_model.size_x, ogt_model.size_y, ogt_model.size_z);

        for (i32 z = 0; z < old_size.z; z++)
        {
            for (i32 y = 0; y < old_size.y; y++)
            {
                for (i32 x = 0; x < old_size.x; x++)
                {
                    i32 vox_index = x + y * old_size.x + z * old_size.x * old_size.y;

                    i32 new_x = x;
                    i32 new_y = z;
                    i32 new_z = old_size.y - 1 - y;

                    i32 index = new_x + new_y * new_size.x + new_z * new_size.x * new_size.y;

                    voxel_model.voxels[index] = ogt_model.voxel_data[vox_index];
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
