#pragma once
#include <optional>
#include <vector>
#include <filesystem>
#include "../../common/types.h"

struct VOXChunkHeader
{
	u8 id[4];
	i32 chunk_content_bytes;
	i32 chunk_children_chunk_bytes;

	static VOXChunkHeader from(u8*& from)
	{
		from += sizeof(VOXChunkHeader);
		return *((VOXChunkHeader*)(from - sizeof(VOXChunkHeader)));
	}
};

struct VoxVoxel
{
	u8 pos_x;
	u8 pos_y;
	u8 pos_z;
	u8 color_index;
};

//4. Chunk id 'PACK' : if it is absent, only one model in the file; only used for the animation in 0.98.2
//-------------------------------------------------------------------------------
//# Bytes  | Type       | Value
//-------------------------------------------------------------------------------
//4        | int        | numModels : num of SIZE and XYZI chunks
//------------------------------------------------------------------------------
struct PACKChunk
{
	i32 num_models;

	static PACKChunk from(u8*& from)
	{
		from += sizeof(PACKChunk);
		return *((PACKChunk*)(from - sizeof(PACKChunk)));
	}
};

//5. Chunk id 'SIZE' : model size
//------------------------------------------------------------------------------ -
//# Bytes | Type | Value
//------------------------------------------------------------------------------ -
//4 | int | size x
//4 | int | size y
//4 | int | size z : gravity direction
//------------------------------------------------------------------------------ -
struct SIZEChunk
{
	i32 size_x;
	i32 size_y;
	i32 size_z;

	static SIZEChunk from(u8*& from)
	{
		from += sizeof(SIZEChunk);
		return *((SIZEChunk*)(from - sizeof(SIZEChunk)));
	}
};

//6. Chunk id 'XYZI' : model voxels, paired with the SIZE chunk
//------------------------------------------------------------------------------ -
//# Bytes | Type | Value
//------------------------------------------------------------------------------ -
//4 | int | numVoxels(N)
//4 x N | int | (x, y, z, colorIndex) : 1 byte for each component
//------------------------------------------------------------------------------ -

struct XYZIChunk
{
	i32 num_voxels{ 0 };
	std::vector<VoxVoxel> voxels;

	static XYZIChunk from(u8*& from)
	{
		XYZIChunk chunk;
		chunk.num_voxels = *(i32*)from;

		VoxVoxel* voxels_ptr = ((VoxVoxel*)from) + 1; // Skip first (num_voxels)
		chunk.voxels.resize(chunk.num_voxels);
		memcpy(chunk.voxels.data(), voxels_ptr, chunk.num_voxels * sizeof(VoxVoxel));

		from += sizeof(i32) + sizeof(VoxVoxel) * chunk.voxels.size();

		return chunk;
	}
};

//7. Chunk id 'RGBA' : palette
//------------------------------------------------------------------------------ -
//# Bytes | Type | Value
//------------------------------------------------------------------------------ -
//4 x 256 | int | (R, G, B, A) : 1 byte for each component
//------------------------------------------------------------------------------ -

struct RGBAChunk
{
	u32 color_palette[256];

	static RGBAChunk from(u8*& from)
	{
		from += sizeof(RGBAChunk);
		return *((RGBAChunk*)(from - sizeof(RGBAChunk)));
	}
};

struct VoxModel
{
	std::optional<PACKChunk> pack;

	std::vector<SIZEChunk> sizes;
	std::vector<XYZIChunk> xyzis;

	RGBAChunk color_palette{}; // Will always have a palette, but will be replaced with default if original not found
};

namespace VoxModels
{
	VoxModel load_model(const std::filesystem::path& model_path);
}