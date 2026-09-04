#include "../decrypt.hpp"
#include "../elf.hpp"
#include "../hash.hpp"
#include "../iso.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "../emitter.hpp"

namespace psprecomp
{
	using namespace project_detail;
	namespace
	{

		std::filesystem::path
		unique_staging_path(const std::filesystem::path &destination)
		{
			const auto nonce =
				std::chrono::steady_clock::now().time_since_epoch().count();
			return destination.parent_path() / ("." + destination.filename().string() +
												".psprecomp-" + std::to_string(nonce));
		}

	} // namespace

	SourceInfo inspect_source(const std::filesystem::path &input)
	{
		SourceInfo result;
		result.kind = detect_kind(input);
		if (result.kind == InputKind::executable)
		{
			result.suggested_display_name = input.stem().string();
			result.executable_path = input.filename().string();
			result.executable_encrypted = is_encrypted_psp_data(read_binary(input));
			return result;
		}
		const IsoImage image(input);
		const auto executable = find_psp_executable(image);
		if (!executable)
		{
			throw std::runtime_error(
				"ISO does not contain PSP_GAME/SYSDIR/EBOOT.BIN or BOOT.BIN");
		}
		const auto metadata = read_psp_disc_metadata(image);
		result.suggested_display_name = metadata.title;
		result.disc_id = metadata.disc_id;
		result.executable_path = executable->path.generic_string();
		result.sfo_path = metadata.sfo_path;
		result.executable_encrypted = is_encrypted_psp_data(image.read(*executable));
		result.disc_entries = image.entries().size();
		return result;
	}

	std::string project_slug(std::string_view value)
	{
		std::string result;
		bool separator = false;
		for (const auto character : value)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (std::isalnum(byte))
			{
				if (separator && !result.empty())
				{
					result.push_back('_');
				}
				result.push_back(static_cast<char>(std::tolower(byte)));
				separator = false;
			}
			else
			{
				separator = true;
			}
		}
		if (result.empty())
		{
			result = "psp_recompiled";
		}
		if (std::isdigit(static_cast<unsigned char>(result.front())))
		{
			result.insert(0, "game_");
		}
		return result;
	}

	ExportSummary export_codebase(const ExportConfig &config)
	{
		if (config.display_name.empty() || config.project_name.empty())
		{
			throw std::runtime_error("display name and project name cannot be empty");
		}
		if (std::filesystem::exists(config.output_directory))
		{
			throw std::runtime_error("output already exists: " +
									 config.output_directory.string());
		}
		const auto runtime_source = config.runtime_include_directory / "psprecomp";
		const auto source_root = config.runtime_include_directory.parent_path();
		if (!std::filesystem::is_directory(runtime_source))
		{
			throw std::runtime_error("cannot find PSPRecomp runtime headers at: " +
									 runtime_source.string());
		}
		if (!std::filesystem::is_directory(config.refract_directory))
		{
			throw std::runtime_error("cannot find refract engine at: " +
									 config.refract_directory.string());
		}
		for (const auto *name : {"LICENSE", "LICENSING.md",
								 "THIRD_PARTY_NOTICES.md"})
		{
			if (!std::filesystem::is_regular_file(source_root / name))
			{
				throw std::runtime_error("cannot find required licensing file: " +
									 (source_root / name).string());
			}
		}

		const auto info = inspect_source(config.input);
		auto staging = unique_staging_path(config.output_directory);
		std::filesystem::create_directories(config.output_directory.parent_path());
		std::filesystem::create_directories(staging);
		try
		{
			std::filesystem::path executable;
			std::string decryption_backend;
			if (info.kind == InputKind::iso)
			{
				const IsoImage iso(config.input);
				const auto iso_executable = find_psp_executable(iso);
				if (!iso_executable)
				{
					throw std::runtime_error("PSP executable disappeared from ISO");
				}
				const auto executable_data = iso.read(*iso_executable);
				std::filesystem::create_directories(staging / "original");
				std::filesystem::copy_file(config.input,
									   staging / "original" / "disc.iso");
				if (is_encrypted_psp_data(executable_data))
				{
					if (config.progress)
					{
						config.progress("Decrypting the ~PSP executable with PPSSPP");
					}
					auto decrypted = decrypt_psp_executable(executable_data);
					decryption_backend = std::move(decrypted.backend);
					executable = staging / "original" / "decrypted.elf";
					write_bytes(executable, decrypted.bytes);
				}
				else if (!is_elf_data(executable_data))
				{
					throw std::runtime_error(
						"PSP_GAME/SYSDIR contains neither an ELF nor a supported ~PSP "
						"encrypted executable");
				}
				if (config.extract_disc)
				{
					std::uintmax_t extracted_size = 0;
					for (const auto &entry : iso.entries())
					{
						if (!entry.directory)
						{
							extracted_size += entry.size;
						}
					}
					const auto space = std::filesystem::space(staging);
					if (extracted_size > space.available)
					{
						throw std::runtime_error(
							"not enough free space to extract the ISO (requires " +
							std::to_string(extracted_size) + " bytes)");
					}
					if (config.progress)
					{
						config.progress("Extracting the disc filesystem");
					}
					iso.extract_all(staging / "disc");
					std::ofstream sectors(staging / "original" / "disc-sectors.tsv",
										  std::ios::binary | std::ios::trunc);
					if (!sectors)
					{
						throw std::runtime_error("cannot create disc sector metadata");
					}
					for (const auto &entry : iso.entries())
					{
						if (!entry.directory)
						{
							sectors << entry.extent << '\t' << entry.path.generic_string()
									<< '\n';
						}
					}
					if (!sectors)
					{
						throw std::runtime_error("cannot write disc sector metadata");
					}
					if (executable.empty())
					{
						executable = staging / "disc" / iso_executable->path;
					}
				}
				else if (executable.empty())
				{
					executable = staging / "original" / iso_executable->path.filename();
					write_bytes(executable, executable_data);
				}
			}
			else
			{
				const auto executable_data = read_binary(config.input);
				if (is_encrypted_psp_data(executable_data))
				{
					if (config.progress)
					{
						config.progress("Decrypting the ~PSP executable with PPSSPP");
					}
					auto decrypted = decrypt_psp_executable(executable_data);
					decryption_backend = std::move(decrypted.backend);
					executable = staging / "original" / "decrypted.elf";
					write_bytes(executable, decrypted.bytes);
				}
				else
				{
					executable = staging / "original" / config.input.filename();
					std::filesystem::create_directories(executable.parent_path());
					std::filesystem::copy_file(config.input, executable);
				}
			}

			if (config.progress)
			{
				config.progress("Loading and validating the PSP executable");
			}
			ElfImage elf;
			try
			{
				elf = load_elf(executable);
			}
			catch (const std::exception &error)
			{
				throw std::runtime_error(
					std::string("selected PSP executable cannot be recompiled: ") +
					error.what());
			}
			std::optional<CodeMap> map;
			if (config.code_map)
			{
				if (config.progress)
				{
					config.progress("Loading function metadata");
				}
				map = load_code_map(*config.code_map);
				std::filesystem::create_directories(staging / "config");
				std::filesystem::copy_file(*config.code_map,
										   staging / "config" / "code.map");
			}

			std::filesystem::create_directories(staging / "include");
			std::filesystem::copy(runtime_source, staging / "include" / "psprecomp",
								  std::filesystem::copy_options::recursive);
			std::filesystem::copy(config.refract_directory, staging / "refract",
								  std::filesystem::copy_options::recursive);
			GeneratedProjectOptions emitter_options;
			emitter_options.display_name = config.display_name;
			// PspModuleInfo::modname is char[27]. In C++ translation units
			// PSP_MODULE_INFO stringifies its already-quoted name argument, so the
			// literal gains two escaped quote characters on top of the NUL
			// terminator (len + 2 quotes + 1 NUL <= 27 => len <= 24). Names longer
			// than this silently failed to build ("initializer-string ... too
			// long") for any game whose project name exceeded ~24-27 characters.
			emitter_options.module_name = config.project_name.substr(0, 24U);
			emitter_options.target_name = config.project_name;
			emitter_options.include_path = "../../include";
			emitter_options.platform_directory = staging / "platform";
			if (config.progress)
			{
				config.progress("Translating Allegrex code to C++");
			}
			emit_project(elf, staging / "src" / "generated", map ? &*map : nullptr,
						 emitter_options);

			if (config.progress)
			{
				config.progress("Writing project files");
			}
			for (const auto *name : {"LICENSE", "LICENSING.md",
									 "THIRD_PARTY_NOTICES.md"})
			{
				std::filesystem::copy_file(source_root / name, staging / name);
			}
			const bool has_disc = info.kind == InputKind::iso && config.extract_disc;
			const auto psp_recompile_mode =
				map && !map->overlay_starts.empty() ? std::string_view("overlays")
													: std::string_view("full");
			write_text(staging / "Makefile",
					   root_makefile(config, has_disc, info.executable_path,
								 info.sfo_path, psp_recompile_mode,
								 info.kind == InputKind::iso
									 ? "original/disc.iso"
									 : std::filesystem::relative(executable, staging)
										   .generic_string()));
			std::filesystem::create_directories(staging / "patches");
			write_text(staging / "patches" / "patches.cpp", patch_template_source());
			write_text(staging / "patches" / "README.md", patch_tutorial_readme());
			if (has_disc)
			{
				write_text(staging / "tools" / "iso_patch.cpp", iso_patch_tool_source());
			}
			write_text(staging / ".gitignore",
					   "/src/generated/*\n!/src/generated/.gitkeep\n"
					   "/platform/*\n!/platform/.gitkeep\n"
					   "/include/psprecomp/*\n!/include/psprecomp/.gitkeep\n"
					   "/refract/*\n!/refract/.gitkeep\n"
					   "/disc/*\n!/disc/.gitkeep\n"
					   "/original/*\n!/original/.gitkeep\n"
					   "/dist/\n/build/\n/.psprecomp/\n/.refract/\n"
					   "*.iso\n*.ISO\n*.cso\n*.CSO\n*.chd\n*.CHD\n"
					   "*.elf\n*.ELF\n*.prx\n*.PRX\n*.pbp\n*.PBP\n.DS_Store\n");
			write_text(staging / "src/generated/.gitkeep", "");
			write_text(staging / "platform/.gitkeep", "");
			write_text(staging / "include/psprecomp/.gitkeep", "");
			write_text(staging / "refract/.gitkeep", "");
			write_text(staging / "disc/.gitkeep", "");
			write_text(staging / "original/.gitkeep", "");
			write_text(
				staging / "README.md",
				generated_readme(
					config, info.kind,
					std::filesystem::relative(executable, staging).generic_string()));
			std::ostringstream manifest;
			const auto input_sha256 = sha256_file(config.input);
			const auto executable_sha256 = sha256_file(executable);
			manifest << "format_version = 1\n"
						"display_name = "
					 << toml_string(config.display_name)
					 << "\nproject_name = " << toml_string(config.project_name)
					 << "\ndisc_id = " << toml_string(config.disc_id)
					 << "\ninput_kind = "
					 << toml_string(info.kind == InputKind::iso ? "iso" : "executable")
					 << "\nexecutable = " << toml_string(info.executable_path)
					 << "\nsfo = " << toml_string(info.sfo_path)
					 << "\ndecryption_backend = " << toml_string(decryption_backend)
					 << "\ncode_map = "
					 << toml_string(config.code_map ? "config/code.map" : "")
					 << "\npsp_recompile_mode = " << toml_string(psp_recompile_mode)
					 << "\ndisc_extracted = " << (has_disc ? "true" : "false")
					 << "\ninput_sha256 = " << toml_string(input_sha256)
					 << "\nexecutable_sha256 = " << toml_string(executable_sha256)
					 << "\n";
			write_text(staging / "project.toml", manifest.str());
			write_text(staging / ".psprecomp/export-hydrated",
					   input_sha256 + "\n" + executable_sha256 + "\n");

			std::size_t translation_units = 0;
			for (const auto &entry :
				 std::filesystem::directory_iterator(staging / "src" / "generated"))
			{
				if (entry.path().extension() == ".cpp")
				{
					++translation_units;
				}
			}
			if (config.progress)
			{
				config.progress("Publishing the completed project");
			}
			std::filesystem::rename(staging, config.output_directory);
			return {info.kind, config.output_directory, info.executable_path,
					decryption_backend, info.disc_entries, translation_units};
		}
		catch (...)
		{
			std::error_code ignored;
			std::filesystem::remove_all(staging, ignored);
			throw;
		}
	}

} // namespace psprecomp
