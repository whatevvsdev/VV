#include "magicavoxel_parser.h"
#include "../../common/types.h"

#include <fstream>
#include <format>
#include <string.h>

typedef std::filesystem::path filepath;

#define STYLE_LOG_TEMP_REPLACEMENT(string) printf("%s", string.c_str())

bool compare_id(u8* a, u8* b)
{
	bool is_true = true;

	for (int i = 0; i < 4; i++)
		is_true &= a[i] == b[i];

	return is_true;
}

std::vector<u8> read_file_as_bytes(const std::filesystem::path& file_path)
{
	std::ifstream input_file_stream(file_path.string(), std::ios::binary | std::ios::ate);

	if (!input_file_stream)
	{
		STYLE_LOG_TEMP_REPLACEMENT(std::format("File stream could not be opened! a .vox file {}.\n", file_path.string()));
		return {};
	}

	auto end = input_file_stream.tellg();
	input_file_stream.seekg(0, std::ios::beg);

	usize size = usize(end - input_file_stream.tellg());

	std::vector<u8> read_file_buffer(size);

	if (size == 0) // avoid undefined behavior
	{
		STYLE_LOG_TEMP_REPLACEMENT(std::format("Input model file has a size of 0! a .vox file {}.\n", file_path.filename().string()));
		return {};
	}

	if (!input_file_stream.read((char*)read_file_buffer.data(), read_file_buffer.size()))
	{
		STYLE_LOG_TEMP_REPLACEMENT(std::format("Could not read input file stream! a .vox file {}.\n", file_path.filename().string()));
		return {};
	}

	return read_file_buffer;
}

bool validate_vox_format_and_version(u8*& file_pointer)
{
	// Parse VOX ID
	u8 vox_id[4] = { 'V', 'O', 'X', ' ' };
	bool valid_vox_id = compare_id(vox_id, file_pointer);
	file_pointer += 4;

	if (!valid_vox_id)
		return false;

	// Parse VOX Version
	bool valid_vox_version = false;
	{
		i32 vox_version = *((int*)file_pointer);
		valid_vox_version = (vox_version >= 150);
		file_pointer += 4;
	}

	if (!valid_vox_version)
		return false;

	return true;
}

VoxModel VoxModels::load_model(const std::filesystem::path& model_path)
{
	std::vector<u8> file_data_buffer = read_file_as_bytes(model_path);

	u8* file_pointer = &file_data_buffer[0];

	if(!validate_vox_format_and_version(file_pointer))
		STYLE_LOG_TEMP_REPLACEMENT(std::format("Parsed file had invalid .vox version or header! a .vox file {}.\n", model_path.filename().string()));

	VoxModel model;

	VOXChunkHeader main = VOXChunkHeader::from(file_pointer);

	u8 pack[4] = { 'P', 'A', 'C', 'K' };
	VOXChunkHeader next_header = VOXChunkHeader::from(file_pointer);
	bool contains_pack_chunk_header = compare_id(pack, next_header.id);

	i32 num_models{ 0 };

	if (contains_pack_chunk_header)
	{
		model.pack = PACKChunk::from(file_pointer);
		num_models = model.pack.value().num_models;
	}
	else
	{
		// There is no pack header, we need the last header as a SIZE instead
		file_pointer -= 12;
		num_models = 1;
	}

	for (i32 i = 0; i < num_models; i++)
	{
		auto size_header = VOXChunkHeader::from(file_pointer);
		auto size_chunk = SIZEChunk::from(file_pointer);
		auto xyzi_header = VOXChunkHeader::from(file_pointer);
		auto xyzi_chunk = XYZIChunk::from(file_pointer);

		model.sizes.push_back(size_chunk);
		model.xyzis.push_back(xyzi_chunk);
	}


	// Skip all the dogshit, look for RGBA
	bool found_rgba{ false };
	while (!found_rgba && (file_pointer < ( &file_data_buffer[0] + main.chunk_children_chunk_bytes)))
	{
		auto possible_rgba_header = VOXChunkHeader::from(file_pointer);

		u8 rgba[4] = { 'R', 'G', 'B', 'A' };
		found_rgba = compare_id(rgba, possible_rgba_header.id);

		if (!found_rgba)
		{
			file_pointer += possible_rgba_header.chunk_children_chunk_bytes;
			file_pointer += possible_rgba_header.chunk_content_bytes;
		}
	}

	if (found_rgba)
	{
		model.color_palette = RGBAChunk::from(file_pointer);
	}
	else
	{
		memset(&model.color_palette, 0xFF, sizeof(u32) * 256);
	}

	STYLE_LOG_TEMP_REPLACEMENT(std::format("Successfully parsed a .vox file {}.\n", model_path.filename().string()));

	return model;
}