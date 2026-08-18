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

	std::string toml_string(std::string_view value)
	{
		std::string result{"\""};
		for (const auto character : value)
		{
			switch (character)
			{
			case '\\':
				result += "\\\\";
				break;
			case '"':
				result += "\\\"";
				break;
			case '\n':
				result += "\\n";
				break;
			case '\r':
				break;
			default:
				result.push_back(character);
				break;
			}
		}
		result.push_back('"');
		return result;
	}

	std::string generated_readme(const ExportConfig &config, InputKind kind,
								 std::string_view executable_source)
	{
		std::ostringstream out;
		out << "# " << config.display_name
			<< "\n\n"
			   "This is a complete PSPRecomp export. It contains the generated C++ "
			   "code, a vendored runtime, project metadata and build helpers.\n\n"
			   "## Build\n\n"
			   "The generated codebase has one command per target platform:\n\n"
			   "```sh\n"
			   "make psp        # build PSP output (plus ISO for disc exports)\n"
			   "make psp-run    # build and launch through the PPSSPP CLI\n"
			   "make macos      # build a native Release .app (-O3)\n"
			   "make macos-debug # build a native Debug .app\n"
			   "make macos-run  # build and run the native Release app\n"
			   "```\n\n"
			   "The PSP targets require PSPSDK and `psp-config` in `PATH`. The "
			   "macOS target requires CMake, Apple Clang and Qt 6 base for desktop "
			   "system dialogs. Homebrew users can install it with "
			   "`brew install qtbase`.\n\n"
			   "## Controls\n\n"
			   "Native macOS builds support standard game controllers and a "
			   "keyboard fallback: arrows = D-pad, WASD = analog stick, I/J/K/L = "
			   "Triangle/Square/Cross/Circle, Q/E = L/R, Return = Start and Right "
			   "Shift = Select.\n\n"
			   "The resulting `EBOOT.PBP` and PRX are written below "
			   "`src/generated/`. Run `make disc-tree` to create a copy of the "
			   "extracted disc with its executable replaced by the recompiled "
			   "PRX.\n\n"
			   "## Layout\n\n"
			   "- `src/generated/`: platform-independent functions and dispatcher\n"
			   "- `platform/platform.h`: complete imported-API contract\n"
			   "- `platform/psp/`: PSP entry point, ABI bridge and real SCE calls\n"
			   "- `platform/macos/`: native entry point and psprism adapter\n"
			   "- `refract/`: vendored, game-editable PSP-to-host runtime engine\n"
			   "- `include/psprecomp/`: portable runtime used by generated code\n"
			   "- `config/`: the optional code map used for this export\n"
			   "- `disc/`: extracted original disc filesystem (ISO inputs only)\n"
			   "- `original/`: selected input executable when no disc was extracted\n"
			   "- `project.toml`: reproducible project metadata\n\n"
			   "Input type: `"
			<< (kind == InputKind::iso ? "iso" : "executable") << "`  \n"
			<< "Selected executable: `" << executable_source
			<< "`\n\n"
			   "> Keep copyrighted game data private and only work with dumps you "
			   "are legally entitled to use.\n";
		return out.str();
	}

	std::string root_makefile(const ExportConfig &config, bool has_disc,
							  std::string_view disc_executable,
							  std::string_view sfo_path,
							  std::string_view psp_recompile_mode)
	{
		std::ostringstream out;
		out << "PPSSPP ?= ppsspp\n"
			   "CMAKE ?= cmake\n"
			   "CXX ?= c++\n"
			   "PSP_RECOMPILE_MODE := "
			<< psp_recompile_mode
			<< "\n"
			   "MACOS_BUILD_TYPE ?= Release\n"
			   "MACOS_RUN_ARGS ?=\n"
			   "\n"
			   ".PHONY: all psp-recompile-check psp-binary psp psp-run macos "
			   "macos-debug macos-run "
			   "clean rebuild "
			   "ppsspp "
			   "disc-tree help\n\n"
			   "all: psp\n\n"
			   "psp-recompile-check:\n"
			   "\t@echo \"PSP recompile mode: $(PSP_RECOMPILE_MODE)\"\n\n"
			   "psp-binary: psp-recompile-check\n"
			   "\t$(MAKE) -C src/generated\n\n";
		if (has_disc)
		{
			out << "psp: psp-binary\n"
				<< "\tmkdir -p dist .psprecomp\n"
				   "\t$(CXX) -std=c++20 -O2 -o .psprecomp/iso_patch "
				   "tools/iso_patch.cpp\n"
				   "\t.psprecomp/iso_patch \"$(CURDIR)/original/disc.iso\" "
				   "\"$(CURDIR)/dist/"
				<< config.project_name << ".iso\""
				<< " \"" + std::string(disc_executable) +
					   "\"=\"$(CURDIR)/src/generated/" + config.project_name +
					   ".prx\"" +
					   (!sfo_path.empty()
							? " \"" + std::string(sfo_path) +
								  "\"=\"$(CURDIR)/src/generated/PARAM.SFO\"\n"
							: "\n")
				<< "\n"
				   "psp-run: psp\n"
				   "\t$(PPSSPP) \"$(CURDIR)/dist/"
				<< config.project_name << ".iso\"\n\n";
		}
		else
		{
			out << "psp: psp-binary\n"
				   "\t@echo \"Built src/generated/EBOOT.PBP (an ISO input is "
				   "required to create a disc image).\"\n\n"
				   "psp-run: psp\n"
				   "\t$(PPSSPP) \"$(CURDIR)/src/generated/EBOOT.PBP\"\n\n";
		}
		out << "macos:\n"
			   "\t$(CMAKE) -S . -B build/macos "
			   "-DCMAKE_BUILD_TYPE=$(MACOS_BUILD_TYPE)\n"
			   "\t$(CMAKE) --build build/macos -j\n\n"
			   "macos-debug:\n"
			   "\t$(MAKE) macos MACOS_BUILD_TYPE=Debug\n\n"
			   "macos-run: macos\n"
			   "\tREFRACT_DISC_ROOT=\"$(CURDIR)/disc\" "
			   "REFRACT_DISC_IMAGE=\"$(CURDIR)/original/disc.iso\" "
			   "REFRACT_WRITABLE_ROOT=\"$(CURDIR)/.refract/ms0\" "
			   "\"$(CURDIR)/build/macos/"
			<< config.project_name << ".app/Contents/MacOS/" << config.project_name
			<< "\" $(MACOS_RUN_ARGS)\n\n"
			   "ppsspp: psp-run\n\n"
			   "clean:\n"
			   "\t$(MAKE) -C src/generated clean\n"
			   "\t$(CMAKE) -E rm -rf build/macos\n\n"
			   "rebuild: clean all\n\n";
		if (has_disc)
		{
			out << "disc-tree: psp-binary\n"
				   "\trm -rf dist/disc\n"
				   "\tmkdir -p dist\n"
				   "\tcp -R disc dist/disc\n"
				<< "\tcp src/generated/" + config.project_name +
					   ".prx dist/disc/" + std::string(disc_executable) +
					   (!sfo_path.empty()
							? "\n\tcp src/generated/PARAM.SFO dist/disc/" +
								  std::string(sfo_path) + "\n"
							: "\n") +
					   "\t@echo \"Prepared dist/disc with the recompiled executable ($(PSP_RECOMPILE_MODE)).\"\n\n";
		}
		else
		{
			out << "disc-tree:\n"
				   "\t@echo \"disc-tree requires an ISO export.\"\n"
				   "\t@false\n\n";
		}
		out << "help:\n"
			   "\t@echo \"make psp        Build the PSP PRX and EBOOT"
			<< (has_disc ? " plus ISO" : "")
			<< "\"\n"
			   "\t@echo \"make psp-run    Build PSP and launch it in PPSSPP\"\n"
			   "\t@echo \"make macos      Build a native Release .app (-O3)\"\n"
			   "\t@echo \"make macos-debug Build a native Debug .app\"\n"
			   "\t@echo \"make macos-run  Build and launch the Release .app\"\n"
			   "\t@echo \"make clean      Remove compiler output\"\n";
		return out.str();
	}

	//
	// Source for a small, dependency-free host tool that rebuilds the PSP disc
	// image while preserving the logical block address (LBA) of every file that
	// is not being replaced.
	//
	// A naive from-scratch ISO repack (mkisofs/xorriso/hdiutil on the extracted
	// tree) lays every file out again using its own ordering rules. Several PSP
	// games (observed with Need for Speed: Most Wanted) resolve large assets
	// through raw sector addressing baked into their own code
	// (`disc0:/sce_lbnXXXXX_sizeYYY`), bypassing the filesystem entirely. A
	// repack silently relocates that data, so those games load garbage/corrupted
	// modules instead of the real ones and crash or hang instead of booting.
	// Copying the original image and only relocating the handful of files that
	// changed (patching just their directory record) keeps every untouched
	// asset's LBA byte-identical to the retail disc.

	std::string macos_cmake(const ExportConfig &config)
	{
		std::ostringstream out;
		out << "cmake_minimum_required(VERSION 3.20)\n"
			   "project("
			<< config.project_name
			<< " LANGUAGES CXX)\n\n"
			   "set(CMAKE_CXX_STANDARD 20)\n"
			   "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
			   "set(CMAKE_CXX_EXTENSIONS OFF)\n"
			   "include(CheckIPOSupported)\n"
			   "check_ipo_supported(RESULT PSPRECOMP_IPO_SUPPORTED)\n"
			   "include(src/generated/generated_sources.cmake)\n\n"
			   "add_subdirectory(refract)\n\n"
			   "add_executable("
			<< config.project_name
			<< " MACOSX_BUNDLE\n"
			   "  ${PSPRECOMP_GENERATED_SOURCES}\n"
			   "  platform/macos/main.cpp\n"
			   "  platform/macos/platform.cpp\n"
			   "  src/generated/guest_image.bin\n"
			   "  src/generated/relocations.bin\n"
			   ")\n"
			   "target_include_directories("
			<< config.project_name
			<< " PRIVATE . include src/generated)\n"
			   "target_link_libraries("
			<< config.project_name
			<< " PRIVATE refract)\n"
			   "target_compile_options("
			<< config.project_name
			<< " PRIVATE -Wno-tautological-compare)\n"
			   "if(PSPRECOMP_IPO_SUPPORTED)\n"
			   "  set_property(TARGET refract PROPERTY "
			   "INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)\n"
			   "  set_property(TARGET "
			<< config.project_name
			<< " PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)\n"
			   "endif()\n"
			   "set_source_files_properties(\n"
			   "  src/generated/guest_image.bin src/generated/relocations.bin\n"
			   "  PROPERTIES MACOSX_PACKAGE_LOCATION Resources\n"
			   ")\n"
			   "set_target_properties("
			<< config.project_name
			<< " PROPERTIES\n"
			   "  MACOSX_BUNDLE_BUNDLE_NAME "
			<< toml_string(config.display_name)
			<< "\n"
			   "  MACOSX_BUNDLE_GUI_IDENTIFIER \"dev.psprecomp."
			<< config.project_name
			<< "\"\n"
			   ")\n";
		return out.str();
	}

} // namespace psprecomp::project_detail
