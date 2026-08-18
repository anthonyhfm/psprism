#include "../decrypt.hpp"
#include "../elf.hpp"
#include "../iso.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace psprecomp::project_detail
{

	std::vector<std::uint8_t> read_binary(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream)
		{
			throw std::runtime_error("cannot open input: " + path.string());
		}
		const auto size = stream.tellg();
		if (size < 0)
		{
			throw std::runtime_error("cannot determine input size: " + path.string());
		}
		std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
		stream.seekg(0);
		if (!result.empty())
		{
			stream.read(reinterpret_cast<char *>(result.data()),
						static_cast<std::streamsize>(result.size()));
		}
		if (!stream)
		{
			throw std::runtime_error("failed while reading input: " + path.string());
		}
		return result;
	}

	bool has_psp_executable_magic(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::uint8_t magic[4]{};
		stream.read(reinterpret_cast<char *>(magic), sizeof(magic));
		if (stream.gcount() != 4)
		{
			return false;
		}
		const std::vector<std::uint8_t> prefix(std::begin(magic), std::end(magic));
		return is_elf_data(prefix) || is_encrypted_psp_data(prefix);
	}

	InputKind detect_kind(const std::filesystem::path &input)
	{
		if (!std::filesystem::is_regular_file(input))
		{
			throw std::runtime_error("input does not exist or is not a file: " +
									 input.string());
		}
		if (has_psp_executable_magic(input))
		{
			return InputKind::executable;
		}
		std::ifstream stream(input, std::ios::binary);
		stream.seekg(16LL * 2048LL + 1LL);
		char identifier[5]{};
		stream.read(identifier, sizeof(identifier));
		if (stream.gcount() == 5 && std::string_view(identifier, 5) == "CD001")
		{
			return InputKind::iso;
		}
		throw std::runtime_error(
			"unsupported input; expected a PSP ELF/PRX or ISO 9660 image");
	}

	void write_bytes(const std::filesystem::path &path,
					 const std::vector<std::uint8_t> &data)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			throw std::runtime_error("cannot create file: " + path.string());
		}
		if (!data.empty())
		{
			stream.write(reinterpret_cast<const char *>(data.data()),
						 static_cast<std::streamsize>(data.size()));
		}
		if (!stream)
		{
			throw std::runtime_error("failed while writing file: " + path.string());
		}
	}

	void write_text(const std::filesystem::path &path, std::string_view value)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			throw std::runtime_error("cannot create file: " + path.string());
		}
		stream << value;
		if (!stream)
		{
			throw std::runtime_error("failed while writing file: " + path.string());
		}
	}

} // namespace psprecomp::project_detail
