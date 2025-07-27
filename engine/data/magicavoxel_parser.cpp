#include "magicavoxel_parser.h"
#include "../../common/types.h"
#include "../../common/io.h"

#include <fstream>
#include <format>
#include <string.h>

#define STYLE_LOG_TEMP_REPLACEMENT(string) printf("%s", string.c_str())

namespace MagicaVoxel
{
	bool validate_vox_format_and_version(u8*& file_pointer)
	{
		// Parse VOX ID
		u32 read_id = *reinterpret_cast<u32*>(file_pointer);
		bool valid_vox_id = read_id == ChunkType::VOX;
		file_pointer += 4;

		if (!valid_vox_id)
			return false;

		// Parse VOX Version
		bool valid_vox_version = false;
		{
			i32 vox_version = read_i32(file_pointer);
			valid_vox_version = (vox_version >= 150);
		}

		if (!valid_vox_version)
			return false;

		return true;
	}

	Model Models::load_model(const std::filesystem::path& model_path)
	{
		std::vector<u8> file_data_buffer = IO::read_binary_file(model_path);
		u8* file_pointer = file_data_buffer.data();

		if(!validate_vox_format_and_version(file_pointer))
			STYLE_LOG_TEMP_REPLACEMENT(std::format("Parsed file had invalid .vox version or header! a .vox file {}.\n", model_path.filename().string()));

		Model model;

		auto main = ChunkHeader(file_pointer);
		auto next_header = ChunkHeader(file_pointer);

		i32 num_models{ 1 };
		if (next_header.id.as_enum == ChunkType::PACK)
		{
			model.pack = PACKChunk(file_pointer);
			num_models = model.pack.num_models;
		}
		else
		{
			// There is no pack header, we need the last header as a SIZE instead
			file_pointer -= sizeof(ChunkHeader);
		}

		for (i32 i = 0; i < num_models; i++)
		{
			auto size_header = ChunkHeader(file_pointer);
			auto size_chunk = SIZEChunk(file_pointer);
			auto xyzi_header = ChunkHeader(file_pointer);
			auto xyzi_chunk = XYZIChunk(file_pointer);

			model.sizes.push_back(size_chunk);
			model.xyzis.push_back(xyzi_chunk);
		}

		// Look for RGBA
		bool found_rgba{ false };
		while (file_pointer < (&file_data_buffer[0] + main.chunk_children_chunk_bytes))
		{
			next_header = ChunkHeader(file_pointer);

			if (next_header.id.as_enum == ChunkType::RGBA)
			{
				found_rgba = true;
				break;
			}

			file_pointer += next_header.chunk_children_chunk_bytes;
			file_pointer += next_header.chunk_content_bytes;
		}

		if (found_rgba)
		{
			model.color_palette = RGBAChunk(file_pointer);
		}
		else
		{
			memset(&model.color_palette, 0xFF, sizeof(u32) * 256);
		}

		STYLE_LOG_TEMP_REPLACEMENT(std::format("Successfully parsed a .vox file {}.\n", model_path.filename().string()));

		return model;
	}

	Scene Models::parse_file(const std::filesystem::path& file_path)
	{
		std::vector<u8> file_data_buffer = IO::read_binary_file(file_path);
		u8* file_pointer = file_data_buffer.data();

		if (!validate_vox_format_and_version(file_pointer))
			STYLE_LOG_TEMP_REPLACEMENT(std::format("Parsed file had invalid .vox version or header! a .vox file {}.\n", file_path.filename().string()));

		struct GroupInfo
		{
			i32 num_children;
			glm::mat4 transform;
		};

		Scene scene;
		auto main = ChunkHeader(file_pointer);

		std::vector<GroupInfo> unfinished_groups;

		std::vector<SIZEChunk> sizes;
		std::vector<XYZIChunk> xyzis;
		std::vector<Instance> instances;

		int previous_index = -1;

		while (file_pointer < (&file_data_buffer[0] + main.chunk_children_chunk_bytes))
		{
			auto next_header = ChunkHeader(file_pointer);

			switch (next_header.id.as_enum)
			{
			case ChunkType::SIZE:
			{
				sizes.emplace_back(SIZEChunk(file_pointer));
				auto dummy_header = ChunkHeader(file_pointer);
				xyzis.emplace_back(XYZIChunk(file_pointer));
			} break;
			case ChunkType::XYZI: /* XYZI's should be paired with SIZE */ break;
			case ChunkType::RGBA:
				{
					scene.color_palette = RGBAChunk(file_pointer);
					break;
				}
			case ChunkType::nTRN:
			{
				auto trn_chunk = nTRNChunk(file_pointer);

				assert(previous_index + 1 == trn_chunk.id);
				previous_index = trn_chunk.id;

				auto transform = glm::mat4(0); // We will construct our own matrix

				/* TODO: Currently we don't support voxel animations
				 *  so we ignore _f
				*/
				for (int i = 0; i < trn_chunk.frame_attributes[0].key_value_pair_count; i += 1)
				{
					const auto& [key, value] = trn_chunk.frame_attributes[0].key_value_pairs[i];

					// looking for these values
					// _r
					// _t

					if (key.buffer_size != 2 || key.buffer[0] != '_' || (key.buffer[1] != 'r' && key.buffer[1] != 't'))
						continue;

					if (key.buffer[1] == 'r')
					{
						// Gets rotation from rotation_data
						// bits 0 and 1 are the index in the first row
						// buts 2 and 3 are the index in the second row
						// rotations are always guaranteed to be 90 degrees
						// so we can can figure out third row index by process of elimination
						// bits 4, 5 and 6 are signs

						char rotation_string[5] = {0, 0, 0, 0, '\n'};
						memcpy(&rotation_string[0], value.buffer.data(), value.buffer_size);
						u8 rotation_data = atoi(&rotation_string[0]);

						const i32 x_basis_index = (rotation_data >> 0) & 0b11;
						const i32 y_basis_index = (rotation_data >> 2) & 0b11;
						const i32 z_basis_index = 0b11 ^ x_basis_index ^ y_basis_index;

						// Should be correct even though it says row, GLM indexing is column major
						transform[0][x_basis_index] = (rotation_data & (1 << 4)) ? -1.0 : 1.0;
						transform[1][y_basis_index] = (rotation_data & (1 << 5)) ? -1.0 : 1.0;
						transform[2][z_basis_index] = (rotation_data & (1 << 6)) ? -1.0 : 1.0;
					}
					else if (key.buffer[1] == 't')
					{
						char translation_string[12] = {};
						memcpy(translation_string, value.buffer.data(), sizeof(i32) * 3); // Size according to spec

						i32 x { 0 };
						i32 y { 0 };
						i32 z { 0 };
						sscanf_s(translation_string, "%i %i %i", &x, &y, &z);
						transform[3] = glm::vec4(x, y, z, 1.0f);
					}
				}

				if (!unfinished_groups.empty())
				{
					GroupInfo& info = unfinished_groups[unfinished_groups.size() - 1];
					transform = transform * info.transform;

					info.num_children -= 1;

					if (info.num_children <= 0)
					{
						unfinished_groups.pop_back();
					}
				}

				next_header = ChunkHeader(file_pointer);

				switch (next_header.id.as_enum)
				{
					case ChunkType::nGRP:
					{
					auto grp_chunk = nGRPChunk(file_pointer);

					assert(previous_index + 1 == grp_chunk.id);
					previous_index = grp_chunk.id;

					GroupInfo info = {};

					info.num_children = grp_chunk.num_children;
					info.transform = transform;

					unfinished_groups.push_back(info);

					} break;
					case ChunkType::nSHP:
					{
						auto shp_chunk = nSHPChunk(file_pointer);

						assert(previous_index + 1 == shp_chunk.id);
						previous_index = shp_chunk.id;

						for (int i = 0; i < shp_chunk.num_models; i += 1)
						{
							Instance instance
							{
								.model_id = shp_chunk.models[i].id,
								.transform = transform,
							};
							instances.push_back(instance);
						}

					} break;
					default:
					{
					//stylelogerror("unexpected chunk type after nTRN chunk\n");
					} break;
				}
			} break;
			case ChunkType::nGRP:
			case ChunkType::nSHP:
			{
				file_pointer += next_header.chunk_children_chunk_bytes;
				file_pointer += next_header.chunk_content_bytes;

				//stylelogerror("Group or shape chunk should have been loaded in with previous transform\n");
			} break;
			case ChunkType::MATL:
			case ChunkType::LAYR:
			case ChunkType::rOBJ:
			case ChunkType::rCAM:
			case ChunkType::NOTE:
			case ChunkType::IMAP:
			{
				file_pointer += next_header.chunk_children_chunk_bytes;
				file_pointer += next_header.chunk_content_bytes;

				//styleloginfo(std::format("chunk type {}{}{}{} is not supported by current implementation\n", char(header.id.as_chars[0]), char(header.id.as_chars[1]), char(header.id.as_chars[2]), char(header.id.as_chars[3])));
			} break;
			default:
			{
				//stylelogerror("chunk type doesn't match any types in .vox file\n");
			} break;
			}

		//styleloginfo(std::format("Successfully parsed a .vox file {}.\n", scene_path.filename().string()));
		//return scene;
	}
		scene.xyzis = xyzis;
		scene.sizes = sizes;
		return scene;
	}
}