#pragma once
#include <vector>
#include <filesystem>
#include <glm/glm.hpp>
#include "../../common/types.h"

namespace MagicaVoxel
{
	#define CHARS_TO_TYPE(a,b,c,d) (u32(a) << 0) + (u32(b) << 8) + (u32(c) << 16) + (u32(d) << 24)

	enum ChunkType : u32
	{
		INVALID = 0,
		VOX  = CHARS_TO_TYPE('V', 'O', 'X', ' '),
		RGBA = CHARS_TO_TYPE('R', 'G', 'B', 'A'),
		MAIN = CHARS_TO_TYPE('M', 'A', 'I', 'N'),
		PACK = CHARS_TO_TYPE('P', 'A', 'C', 'K'),
		SIZE = CHARS_TO_TYPE('S', 'I', 'Z', 'E'),
		XYZI = CHARS_TO_TYPE('X', 'Y', 'Z', 'I'),
		nTRN = CHARS_TO_TYPE('n', 'T', 'R', 'N'),
		nGRP = CHARS_TO_TYPE('n', 'G', 'R', 'P'),
		nSHP = CHARS_TO_TYPE('n', 'S', 'H', 'P'),
		MATL = CHARS_TO_TYPE('M', 'A', 'T', 'L'),
		LAYR = CHARS_TO_TYPE('L', 'A', 'Y', 'R'),
		rOBJ = CHARS_TO_TYPE('r', 'O', 'B', 'J'),
		rCAM = CHARS_TO_TYPE('r', 'C', 'A', 'M'),
		NOTE = CHARS_TO_TYPE('N', 'O', 'T', 'E'),
		IMAP = CHARS_TO_TYPE('I', 'M', 'A', 'P'),
	};

	inline i32 read_i32(u8*& from)
	{
		from += sizeof(i32);
		return *reinterpret_cast<i32*>(from - sizeof(i32));
	}

	typedef i8 Rotation;

	struct Voxel
	{
		u8 pos_x;
		u8 pos_y;
		u8 pos_z;
		u8 color_index;
	};

	struct String
	{
		i32 buffer_size;
		std::vector<i8> buffer;

		String() = default;
		String(u8*& from)
		{
			buffer_size = read_i32(from);

			buffer.resize(buffer_size);
			memcpy(buffer.data(), from, buffer_size);
			from += buffer_size;
		}
	};

	struct Dict
	{
		struct Pair
		{
			String key;
			String value;
		};

		i32 key_value_pair_count;
		std::vector<Pair> key_value_pairs;

		Dict() = default;
		Dict(u8*& from)
		{
			key_value_pair_count = read_i32(from);

			key_value_pairs.resize(key_value_pair_count);

			for (int i = 0; i < key_value_pair_count; i += 1)
			{
				key_value_pairs[i].key = String(from);
				key_value_pairs[i].value = String(from);
			}
		}
	};

	void read_from(String& string, u8*& from);
	void read_from(Dict& dict, u8*& from);

	struct ChunkID
	{
		ChunkID(ChunkType type)
		{
			as_enum = type;
		}

		union
		{
			u8 as_chars[4];
			u32 as_u32;
			ChunkType as_enum;
		};

		bool compare(u8* data);
	};

	struct ChunkHeader
	{
		ChunkID id { ChunkType::INVALID };
		i32 chunk_content_bytes { 0 };
		i32 chunk_children_chunk_bytes { 0 };

		ChunkHeader(u8*& from)
		{
			from += sizeof(ChunkHeader);
			memcpy(this, from - sizeof(ChunkHeader), sizeof(ChunkHeader));
		}
	};

	//4. Chunk id 'PACK' : if it is absent, only one model in the file; only used for the animation in 0.98.2
	struct PACKChunk
	{
		i32 num_models { 0 };
		PACKChunk() = default;
		PACKChunk(u8*& from)
		{
			from += sizeof(PACKChunk);
			memcpy(this, from - sizeof(PACKChunk), sizeof(PACKChunk));
		}
	};

	//5. Chunk id 'SIZE' : model size
	struct SIZEChunk
	{
		i32 size_x;
		i32 size_y;
		i32 size_z; // gravity direction

		SIZEChunk(u8*& from)
		{
			from += sizeof(SIZEChunk);
			memcpy(this, from - sizeof(SIZEChunk), sizeof(SIZEChunk));
		}
	};

	//6. Chunk id 'XYZI' : model voxels, paired with the SIZE chunk
	struct XYZIChunk
	{
		i32 num_voxels{ 0 };
		std::vector<Voxel> voxels;

		XYZIChunk(u8*& from)
		{
			num_voxels = *(i32*)from;
			from += sizeof(i32);

			voxels.resize(num_voxels);
			memcpy(voxels.data(), from, num_voxels * sizeof(Voxel));

			from += sizeof(Voxel) * voxels.size();
		}
	};

	//7. Chunk id 'RGBA' : palette
	struct RGBAChunk
	{
		u32 color_palette[256];

		RGBAChunk() = default;
		RGBAChunk(u8*& from)
		{
			from += sizeof(RGBAChunk);
			memcpy(this, from - sizeof(RGBAChunk), sizeof(RGBAChunk));
		}
	};

	/*
	=================================
	(1) Transform Node Chunk : "nTRN"

	int32	: node id
	DICT	: node attributes
		  (_name : string)
		  (_hidden : 0/1)
	int32 	: child node id
	int32 	: reserved id (must be -1)
	int32	: layer id
	int32	: num of frames (must be greater than 0)

	// for each frame
	{
	DICT	: frame attributes
		  (_r : int8)    ROTATION, see (c)
		  (_t : int32x3) translation
		  (_f : int32)   frame index, start from 0
	}xN
	*/

	struct nTRNChunk
	{
		i32 id;
		Dict attributes;
		i32 child_id;
		i32 reserved_id { -1 };
		i32 layer_id;
		i32 num_frames { 0 };

		std::vector<Dict> frame_attributes;

		nTRNChunk() = default;
		nTRNChunk(u8*& from)
		{
			id = read_i32(from);
			attributes = Dict(from);
			child_id = read_i32(from);
			reserved_id = read_i32(from);
			layer_id = read_i32(from);
			num_frames = read_i32(from);

			frame_attributes.resize(num_frames);
			for (int i = 0; i < num_frames; i += 1)
				frame_attributes[i] = Dict(from);
		}
	};

	/*
	=================================
	(2) Group Node Chunk : "nGRP"

	int32	: node id
	DICT	: node attributes
	int32 	: num of children nodes

	// for each child
	{
	int32	: child node id
	}xN
	*/

	struct nGRPChunk
	{
		i32 id;
		Dict attributes;
		i32 num_children;

		std::vector<i32> child_node_ids;

		nGRPChunk() = default;
		nGRPChunk(u8*& from)
		{
			id = read_i32(from);
			attributes = Dict(from);
			num_children = read_i32(from);

			child_node_ids.resize(num_children);
			memcpy(child_node_ids.data(), from, num_children * sizeof(i32));
			from += num_children * sizeof(i32);
		}
	};

	/*
	=================================
	(3) Shape Node Chunk : "nSHP"

	int32	: node id
	DICT	: node attributes
	int32 	: num of models (must be greater than 0)

	// for each model
	{
	int32	: model id
	DICT	: model attributes : reserved
		(_f : int32)   frame index, start from 0
	}xN
	*/

	struct nSHPChunk
	{
		i32 id;
		Dict attributes;
		i32 num_models;

		struct Model
		{
			i32 id;
			Dict attributes;
		};

		std::vector<Model> models;

		nSHPChunk() = default;
		nSHPChunk(u8*& from)
		{
			id = read_i32(from);
			attributes = Dict(from);
			num_models = read_i32(from);

			models.resize(num_models);
			for (int i = 0; i < num_models; i += 1)
			{
				models[i].id = read_i32(from);
				models[i].attributes = Dict(from);
			}
		}
	};

	/*
	=================================
	(4) Material Chunk : "MATL"

	int32	: material id
	DICT	: material properties
		  (_type : str) _diffuse, _metal, _glass, _emit
		  (_weight : float) range 0 ~ 1
		  (_rough : float)
		  (_spec : float)
		  (_ior : float)
		  (_att : float)
		  (_flux : float)
		  (_plastic)
	*/

	struct MATLChunk
	{
		i32 id;
		Dict properties;

		MATLChunk() = default;
		MATLChunk(u8*& from)
		{
			id = read_i32(from);
			properties = Dict(from);
		}
	};

	/*
	=================================
	(5) Layer Chunk : "LAYR"

	int32	: layer id
	DICT	: layer attribute
		  (_name : string)
		  (_hidden : 0/1)
	int32	: reserved id, must be -1
	*/

	struct LAYRChunk
	{
		i32	id;
		Dict attribute;
		i32 reserved_id;

		LAYRChunk() = default;
		LAYRChunk(u8*& from)
		{
			id = read_i32(from);
			attribute = Dict(from);
			reserved_id = read_i32(from);
		}
	};

	/*
	=================================
	(6) Render Objects Chunk : "rOBJ"

	DICT	: rendering attributes
	*/

	struct rOBJChunk
	{
		Dict rendering_attributes;

		rOBJChunk() = default;
		rOBJChunk(u8*& from)
		{
			rendering_attributes = Dict(from);
		}
	};

	/*
	=================================
	(7) Render Camera Chunk : "rCAM"

	int32	: camera id
	DICT	: camera attribute
		  (_mode : string)
		  (_focus : vec(3))
		  (_angle : vec(3))
		  (_radius : int)
		  (_frustum : float)
		  (_fov : int)
	*/

	struct rCAMChunk
	{
		i32	id;
		Dict attribute;

		rCAMChunk() = default;
		rCAMChunk(u8*& from)
		{
			id = read_i32(from);
			attribute = Dict(from);
		}
	};

	/*
	=================================
	(8) Palette Note Chunk : "NOTE"

	int32	: num of color names

	// for each name
	{
	STRING	: color name
	}xN
	*/

	struct NOTEChunk
	{
		i32	num_color_names;

		std::vector<String> color_names;

		NOTEChunk() = default;
		NOTEChunk(u8*& from)
		{
			num_color_names = read_i32(from);

			color_names.resize(num_color_names);
			for (int i = 0; i < num_color_names; i += 1)
				color_names[i] = String(from);
		}
	};

	/*
	=================================
	(9) Index MAP Chunk : "IMAP"

	size	: 256
	// for each index
	{
	int32	: palette index association
	}x256
	*/

	struct IMAPChunk
	{
		i32 palette_index_associations[256];

		IMAPChunk() = default;
		IMAPChunk(u8*& from)
		{
			memcpy(palette_index_associations, from, sizeof(palette_index_associations));
			from += sizeof(IMAPChunk);
		}
	};

	struct Model
	{
		PACKChunk pack{};

		std::vector<SIZEChunk> sizes{};
		std::vector<XYZIChunk> xyzis{};

		RGBAChunk color_palette{}; // Will always have a palette, but will be replaced with default if original not found
	};

	struct Instance
	{
		i32 model_id;
		glm::mat4 transform;
	};

	struct Scene
	{
		PACKChunk pack{};

		std::vector<SIZEChunk> sizes{};
		std::vector<XYZIChunk> xyzis{};

		std::vector<Instance> instances{};

		RGBAChunk color_palette{};
	};

	namespace Models
	{
		Model load_model(const std::filesystem::path& model_path);
		Scene parse_file(const std::filesystem::path& file_path);
	}
}