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

		u8* file_pointer = &file_data_buffer[0];

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
}