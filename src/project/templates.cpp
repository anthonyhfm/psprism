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
		const auto input_instruction =
			kind == InputKind::iso
				? "Place the supported ISO at `original/disc.iso`, or pass "
				  "`GAME_INPUT=/path/to/game.iso`."
				: "Place the supported executable at the `GAME_INPUT` path shown in "
				  "the Makefile, or pass `GAME_INPUT=/path/to/game.elf`.";
		out << "# " << config.display_name
			<< "\n\n"
			   "This is a bring-your-own-game PSPRecomp project. Copyrighted game "
			   "code and assets are not distributed; they are generated locally from "
			   "your legally obtained input.\n\n"
			   "## Build\n\n"
			<< input_instruction << " Then use one command per target platform. "
			   "The first build hydrates private files "
			   "automatically; later builds use the local cache.\n\n"
			   "```sh\n"
			   "make psp        # build PSP output (plus ISO for disc exports)\n"
			   "make psp-run    # build and launch through the PPSSPP CLI\n"
			   "make macos      # build a native Release .app (-O3)\n"
			   "make macos-debug # build a native Debug .app\n"
			   "make macos-run  # build and run the native Release app\n"
			   "cmake -S . -B build/windows -A x64  # configure Windows x86-64\n"
			   "cmake --build build/windows --config Release # build Windows\n"
			   "```\n\n"
			   "Hydration needs `psprism` in `PATH`, `PSPRISM=/path/to/psprism`, or a "
			   "checkout at `toolchain/psprism`. The PSP targets require PSPSDK and "
			   "`psp-config` in `PATH`. The "
			   "macOS target requires CMake, Apple Clang and Qt 6 base for desktop "
			   "system dialogs. Homebrew users can install it with "
			   "`brew install qtbase`. Windows requires Visual Studio with the x64 "
			   "C++ workload, a Windows SDK, Qt 6.5+ and matching FFmpeg development "
			   "libraries. Qt dialogs and FFmpeg DLLs are deployed beside the game.\n\n"
			   "## Controls\n\n"
			   "Native macOS and Windows builds support standard game controllers and a "
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
			   "- `platform/windows/`: Windows entry point using the same native adapter\n"
			   "- `refract/`: hydrated PSP-to-host runtime engine\n"
			   "- `include/psprecomp/`: portable runtime used by generated code\n"
			   "- `config/`: the optional code map used for this export\n"
			   "- `disc/`: extracted original disc filesystem (ISO inputs only)\n"
			   "- `original/`: private input location\n"
			   "- `project.toml`: reproducible project metadata\n\n"
			   "Input type: `"
			<< (kind == InputKind::iso ? "iso" : "executable") << "`  \n"
			<< "Selected executable: `" << executable_source
			<< "`\n\n"
			   "Generated code, runtime copies, disc data and original inputs are "
			   "Git-ignored. Keep project-specific changes in `patches/`; hydration "
			   "may replace the private generated trees when the toolchain changes. "
			   "Publish the Git tree rather than an archive of this hydrated working "
			   "directory.\n\n"
			   "## License and proprietary game content\n\n"
			   "The original project skeleton, build integration and patches are "
			   "licensed under GNU GPL version 3 or later. See `LICENSE`, "
			   "`LICENSING.md` and `THIRD_PARTY_NOTICES.md`. Those terms do not "
			   "license the original game, generated translations, disc images, "
			   "decrypted executables, assets or built game output. Do not publish "
			   "hydrated files or compiled game binaries.\n\n"
			   "This project is unofficial and is not affiliated with or endorsed by "
			   "Sony Interactive Entertainment or any game publisher or "
			   "rightsholder.\n\n"
			   "For the first public commit, initialize Git inside a clean export, run "
			   "`git add .`, and inspect both `git status --short --ignored` and "
			   "`git ls-files`. The generated ignore rules keep hydration and build "
			   "trees out of a normal add, but they cannot undo files that were "
			   "previously committed or added with `--force`.\n\n"
			   "> Keep copyrighted game data private and only work with dumps you "
			   "are legally entitled to use.\n";
		return out.str();
	}

	std::string root_makefile(const ExportConfig &config, bool has_disc,
							  std::string_view disc_executable,
							  std::string_view sfo_path,
							  std::string_view psp_recompile_mode,
							  std::string_view hydration_input)
	{
		std::ostringstream out;
		out << "PPSSPP ?= ppsspp\n"
			   "GAME_INPUT ?= " << hydration_input << "\n"
			   "PSPRISM ?= psprism\n"
			   "PSPRISM_SOURCE ?= toolchain/psprism\n"
			   "PSPRISM_LOCAL := $(PSPRISM_SOURCE)/build/bin/psprism\n"
			   "HYDRATE_FLAGS ?=\n"
			   "JOBS ?= 8\n"
			   "CXX ?= clang++\n"
			   "OBJCXX ?= clang++\n"
			   "PSP_RECOMPILE_MODE := "
			<< psp_recompile_mode
			<< "\n"
			   "MACOS_BUILD_TYPE ?= Release\n"
			   "MACOS_RUN_ARGS ?=\n\n"
			   "MACOS_APP_DIR := build/macos/"
			<< config.project_name
			<< ".app\n"
			   "MACOS_BIN_DIR := $(MACOS_APP_DIR)/Contents/MacOS\n"
			   "MACOS_RES_DIR := $(MACOS_APP_DIR)/Contents/Resources\n"
			   "MACOS_OBJ_DIR := build/macos/obj\n\n"
			   "ifeq ($(MACOS_BUILD_TYPE),Debug)\n"
			   "  MACOS_CXXFLAGS := -std=c++20 -g -O0 -Wno-tautological-compare\n"
			   "else\n"
			   "  MACOS_CXXFLAGS := -std=c++20 -O3 -Wno-tautological-compare\n"
			   "endif\n\n"
			   "# Auto-detect multimedia dependencies\n"
			   "FFMPEG_PREFIX ?= $(shell if [ -d /opt/homebrew/opt/ffmpeg ]; then echo /opt/homebrew/opt/ffmpeg; elif [ -d /usr/local/opt/ffmpeg ]; then echo /usr/local/opt/ffmpeg; fi)\n"
			   "ifneq ($(FFMPEG_PREFIX),)\n"
			   "  FFMPEG_CFLAGS := -DREFRACT_HAS_FFMPEG=1 -I$(FFMPEG_PREFIX)/include\n"
			   "  FFMPEG_LIBS := -L$(FFMPEG_PREFIX)/lib -lavcodec -lavformat -lavutil -lswscale\n"
			   "endif\n\n"
			   "QT_PREFIX ?= $(shell if [ -d /opt/homebrew/opt/qtbase ]; then echo /opt/homebrew/opt/qtbase; elif [ -d /usr/local/opt/qtbase ]; then echo /usr/local/opt/qtbase; fi)\n"
			   "ifneq ($(QT_PREFIX),)\n"
			   "  QT_CFLAGS := -F$(QT_PREFIX)/lib -I$(QT_PREFIX)/lib/QtWidgets.framework/Headers -I$(QT_PREFIX)/lib/QtCore.framework/Headers -I$(QT_PREFIX)/lib/QtGui.framework/Headers\n"
			   "  QT_DEFS := -DREFRACT_HAS_DESKTOP_DIALOGS=1 -DREFRACT_QT_PLATFORM_PLUGIN_PATH=\\\"$(QT_PREFIX)/share/qt/plugins/platforms\\\"\n"
			   "  QT_LIBS := -L$(QT_PREFIX)/lib -F$(QT_PREFIX)/lib -framework QtWidgets -framework QtGui -framework QtCore\n"
			   "  REFRACT_DIALOG_SRCS := refract/src/host/desktop_dialogs.cpp\n"
			   "endif\n\n"
			   "REFRACT_SRCS := \\\n"
			   "  refract/src/runtime.cpp \\\n"
			   "  refract/src/utility_data.cpp \\\n"
			   "  refract/src/psp_sdk_bridge.cpp \\\n"
			   "  refract/src/host/audio_engine.cpp \\\n"
			   "  refract/src/host/common.cpp \\\n"
			   "  refract/src/host/macos.cpp \\\n"
			   "  refract/src/stubs/mpeg/media_engine.cpp \\\n"
			   "  refract/third_party/at3_standalone/atrac.cpp \\\n"
			   "  refract/third_party/at3_standalone/atrac3.cpp \\\n"
			   "  refract/third_party/at3_standalone/atrac3plus.cpp \\\n"
			   "  refract/third_party/at3_standalone/atrac3plusdec.cpp \\\n"
			   "  refract/third_party/at3_standalone/atrac3plusdsp.cpp \\\n"
			   "  refract/third_party/at3_standalone/get_bits.cpp \\\n"
			   "  refract/third_party/at3_standalone/compat.cpp \\\n"
			   "  refract/third_party/at3_standalone/fft.cpp \\\n"
			   "  refract/third_party/at3_standalone/mem.cpp \\\n"
			   "  $(REFRACT_DIALOG_SRCS)\n\n"
			   "NATIVE_UNITY_SRCS := $(wildcard src/generated/native_unity_*.cpp)\n"
			   "ifneq ($(NATIVE_UNITY_SRCS),)\n"
			   "  GENERATED_SRCS := $(NATIVE_UNITY_SRCS) src/generated/dispatch.cpp\n"
			   "else\n"
			   "  GENERATED_SRCS := $(wildcard src/generated/func_*.cpp src/generated/shard_*.cpp) src/generated/dispatch.cpp\n"
			   "endif\n"
			   "PATCH_SRCS := $(wildcard patches/*.cpp)\n"
			   "PLATFORM_SRCS := platform/macos/main.cpp platform/macos/platform.cpp\n\n"
			   "MACOS_CPP_SRCS := $(GENERATED_SRCS) $(PATCH_SRCS) $(PLATFORM_SRCS) $(REFRACT_SRCS)\n"
			   "MACOS_CPP_OBJS := $(patsubst %.cpp,$(MACOS_OBJ_DIR)/%.o,$(MACOS_CPP_SRCS))\n"
			   "MACOS_FRONTEND_OBJ := $(MACOS_OBJ_DIR)/refract/src/host/macos_frontend.o\n\n"
			   "MACOS_INCLUDES := -I. -Iinclude -Isrc/generated -Ipatches -Irefract/include -Irefract/include/pspsdk -Irefract/src -Irefract/third_party/at3_standalone $(FFMPEG_CFLAGS) $(QT_CFLAGS) $(QT_DEFS)\n\n"
			   "MACOS_LIBS := -framework AppKit -framework AudioToolbox -framework GameController -framework Metal -framework MetalKit $(FFMPEG_LIBS) $(QT_LIBS)\n\n"
			   ".PHONY: all hydrate psp-recompile-check psp-binary psp-build psp psp-run macos-build macos macos-debug macos-run clean rebuild ppsspp disc-tree help\n\n"
			   "all: psp\n\n"
			   "hydrate:\n"
			   "\t@set -e; \\\n"
			   "\tif [ ! -f \"$(GAME_INPUT)\" ]; then \\\n"
			   "\t  echo \"Missing game input: $(GAME_INPUT)\" >&2; \\\n"
			   "\t  echo \"Place your legally obtained ISO at original/disc.iso or run make GAME_INPUT=/path/to/game.iso.\" >&2; \\\n"
			   "\t  exit 1; \\\n"
			   "\telif command -v \"$(PSPRISM)\" >/dev/null 2>&1; then \\\n"
			   "\t  tool=\"$(PSPRISM)\"; \\\n"
			   "\telif [ -f \"$(PSPRISM_SOURCE)/Makefile\" ]; then \\\n"
			   "\t  $(MAKE) -C \"$(PSPRISM_SOURCE)\" psprism; \\\n"
			   "\t  tool=\"$(PSPRISM_LOCAL)\"; \\\n"
			   "\telse \\\n"
			   "\t  echo \"psprism not found. Install it, set PSPRISM=/path/to/psprism, or initialize toolchain/psprism.\" >&2; \\\n"
			   "\t  exit 1; \\\n"
			   "\tfi; \\\n"
			   "\t\"$$tool\" hydrate --project \"$(CURDIR)\" --input \"$(GAME_INPUT)\" $(HYDRATE_FLAGS)\n\n"
			   "psp-recompile-check:\n"
			   "\t@echo \"PSP recompile mode: $(PSP_RECOMPILE_MODE)\"\n\n"
			   "psp-binary: psp-recompile-check\n"
			   "\t$(MAKE) -C src/generated\n\n";
		if (has_disc)
		{
			out << "psp: hydrate\n"
				   "\t@$(MAKE) --no-print-directory psp-build\n\n"
				   "psp-build: psp-binary\n"
				<< "\tmkdir -p dist .psprecomp\n"
				   "\t$(CXX) -std=c++20 -O2 -o .psprecomp/iso_patch "
				   "tools/iso_patch.cpp\n"
				   "\t.psprecomp/iso_patch \"$(abspath $(GAME_INPUT))\" "
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
			out << "psp: hydrate\n"
				   "\t@$(MAKE) --no-print-directory psp-build\n\n"
				   "psp-build: psp-binary\n"
				   "\t@echo \"Built src/generated/EBOOT.PBP (an ISO input is "
				   "required to create a disc image).\"\n\n"
				   "psp-run: psp\n"
				   "\t$(PPSSPP) \"$(CURDIR)/src/generated/EBOOT.PBP\"\n\n";
		}
		out << "macos: hydrate\n"
			   "\t@$(MAKE) --no-print-directory -j$(JOBS) macos-build\n\n"
			   "macos-build: $(MACOS_BIN_DIR)/" << config.project_name << "\n\n"
			   "$(MACOS_BIN_DIR)/" << config.project_name << ": $(MACOS_CPP_OBJS) $(MACOS_FRONTEND_OBJ)\n"
			   "\t@mkdir -p $(MACOS_BIN_DIR) $(MACOS_RES_DIR)\n"
			   "\t@echo \"Compiling native macOS application (" << config.project_name << ")...\"\n"
			   "\t@$(CXX) $(MACOS_CXXFLAGS) $(MACOS_CPP_OBJS) $(MACOS_FRONTEND_OBJ) $(MACOS_LIBS) -o $@\n"
			   "\t@cp -f src/generated/guest_image.bin src/generated/relocations.bin $(MACOS_RES_DIR)/ 2>/dev/null || true\n"
			   "\t@printf '<?xml version=\"1.0\" encoding=\"UTF-8\"?>\\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\\n<plist version=\"1.0\">\\n<dict>\\n  <key>CFBundleExecutable</key>\\n  <string>"
			<< config.project_name
			<< "</string>\\n  <key>CFBundleIdentifier</key>\\n  <string>dev.psprecomp."
			<< config.project_name
			<< "</string>\\n  <key>CFBundleName</key>\\n  <string>"
			<< toml_string(config.display_name)
			<< "</string>\\n  <key>CFBundlePackageType</key>\\n  <string>APPL</string>\\n</dict>\\n</plist>\\n' > $(MACOS_APP_DIR)/Contents/Info.plist\n"
			   "\t@echo \"Built $(MACOS_APP_DIR)\"\n\n"
			   "$(MACOS_OBJ_DIR)/%.o: %.cpp\n"
			   "\t@mkdir -p $(dir $@)\n"
			   "\t@$(CXX) $(MACOS_CXXFLAGS) -MMD -MP $(MACOS_INCLUDES) -c $< -o $@\n\n"
			   "$(MACOS_FRONTEND_OBJ): refract/src/host/macos_frontend.mm\n"
			   "\t@mkdir -p $(dir $@)\n"
			   "\t@$(OBJCXX) $(MACOS_CXXFLAGS) -MMD -MP -fobjc-arc $(MACOS_INCLUDES) -c $< -o $@\n\n"
			   "-include $(MACOS_CPP_OBJS:.o=.d) $(MACOS_FRONTEND_OBJ:.o=.d)\n\n"
			   "macos-debug:\n"
			   "\t$(MAKE) macos MACOS_BUILD_TYPE=Debug\n\n"
			   "macos-run: macos\n"
			   "\tREFRACT_DISC_ROOT=\"$(CURDIR)/disc\" \\\n"
			   "\tREFRACT_DISC_IMAGE=\"$(abspath $(GAME_INPUT))\" \\\n"
			   "\tREFRACT_WRITABLE_ROOT=\"$(CURDIR)/.refract/ms0\" \\\n"
			   "\t\"$(CURDIR)/$(MACOS_BIN_DIR)/"
			<< config.project_name
			<< "\" $(MACOS_RUN_ARGS)\n\n"
			   "ppsspp: psp-run\n\n"
			   "clean:\n"
			   "\t@if [ -f src/generated/Makefile ]; then $(MAKE) -C src/generated clean; fi\n"
			   "\trm -rf build/macos dist .psprecomp/run .psprecomp/iso_patch\n\n"
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
			   "\t@echo \"make hydrate    Generate private code/assets from the user's ISO\"\n"
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

	std::string native_cmake(const ExportConfig &config)
	{
		std::ostringstream out;
		out << "cmake_minimum_required(VERSION 3.20)\n"
			   "project(" << config.project_name << " LANGUAGES CXX)\n\n"
			   "set(CMAKE_CXX_STANDARD 20)\n"
			   "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
			   "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
			   "file(GLOB PSPRECOMP_NATIVE_UNITY CONFIGURE_DEPENDS\n"
			   "  \"${CMAKE_CURRENT_SOURCE_DIR}/src/generated/native_unity_*.cpp\")\n"
			   "if(PSPRECOMP_NATIVE_UNITY)\n"
			   "  set(PSPRECOMP_GENERATED ${PSPRECOMP_NATIVE_UNITY}\n"
			   "    src/generated/dispatch.cpp)\n"
			   "else()\n"
			   "  file(GLOB PSPRECOMP_FUNCTIONS CONFIGURE_DEPENDS\n"
			   "    \"${CMAKE_CURRENT_SOURCE_DIR}/src/generated/func_*.cpp\"\n"
			   "    \"${CMAKE_CURRENT_SOURCE_DIR}/src/generated/shard_*.cpp\")\n"
			   "  set(PSPRECOMP_GENERATED ${PSPRECOMP_FUNCTIONS}\n"
			   "    src/generated/dispatch.cpp)\n"
			   "endif()\n"
			   "file(GLOB PSPRECOMP_PATCHES CONFIGURE_DEPENDS\n"
			   "  \"${CMAKE_CURRENT_SOURCE_DIR}/patches/*.cpp\")\n\n"
			   "if(WIN32)\n"
			   "  set(PSPRECOMP_PLATFORM\n"
			   "    platform/windows/main.cpp\n"
			   "    platform/windows/platform.cpp)\n"
			   "elseif(APPLE)\n"
			   "  set(PSPRECOMP_PLATFORM\n"
			   "    platform/macos/main.cpp\n"
			   "    platform/macos/platform.cpp)\n"
			   "else()\n"
			   "  message(FATAL_ERROR \"No native platform adapter for ${CMAKE_SYSTEM_NAME}\")\n"
			   "endif()\n\n"
			   "add_subdirectory(refract)\n"
			   "add_executable(" << config.project_name << "\n"
			   "  ${PSPRECOMP_GENERATED}\n"
			   "  ${PSPRECOMP_PATCHES}\n"
			   "  ${PSPRECOMP_PLATFORM})\n"
			   "target_include_directories(" << config.project_name << " PRIVATE\n"
			   "  . include src/generated patches platform)\n"
			   "target_link_libraries(" << config.project_name << " PRIVATE refract)\n"
			   "if(WIN32)\n"
			   "  set_target_properties(" << config.project_name
			<< " PROPERTIES WIN32_EXECUTABLE TRUE)\n"
			   "endif()\n"
			   "if(MSVC)\n"
			   "  target_compile_options(" << config.project_name
			<< " PRIVATE /W4 /EHsc /permissive-)\n"
			   "  target_compile_definitions(" << config.project_name
			<< " PRIVATE _CRT_SECURE_NO_WARNINGS)\n"
			   "  if(WIN32)\n"
			   "    target_link_options(" << config.project_name
			<< " PRIVATE /ENTRY:mainCRTStartup)\n"
			   "  endif()\n"
			   "else()\n"
			   "  target_compile_options(" << config.project_name
			<< " PRIVATE -Wall -Wextra -Wpedantic)\n"
			   "endif()\n\n"
			   "if(APPLE)\n"
			   "  set(PSPRECOMP_RESOURCES\n"
			   "    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/guest_image.bin\n"
			   "    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/relocations.bin)\n"
			   "  set_source_files_properties(${PSPRECOMP_RESOURCES} PROPERTIES\n"
			   "    MACOSX_PACKAGE_LOCATION Resources)\n"
			   "  target_sources(" << config.project_name
			<< " PRIVATE ${PSPRECOMP_RESOURCES})\n"
			   "  set_target_properties(" << config.project_name << " PROPERTIES\n"
			   "    MACOSX_BUNDLE TRUE\n"
			   "    MACOSX_BUNDLE_BUNDLE_NAME " << toml_string(config.display_name) << "\n"
			   "    MACOSX_BUNDLE_GUI_IDENTIFIER dev.psprecomp."
			<< config.project_name << ")\n"
			   "else()\n"
			   "  add_custom_command(TARGET " << config.project_name << " POST_BUILD\n"
			   "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
			   "      ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/guest_image.bin\n"
			   "      $<TARGET_FILE_DIR:" << config.project_name << ">\n"
			   "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
			   "      ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/relocations.bin\n"
			   "      $<TARGET_FILE_DIR:" << config.project_name << ">)\n"
			   "endif()\n\n"
			   "refract_deploy_runtime(" << config.project_name << ")\n";
		return out.str();
	}

	std::string patch_template_source()
	{
		return
			"// SPDX-FileCopyrightText: 2026 Anthony Hofmeister\n"
			"// SPDX-License-Identifier: GPL-3.0-or-later\n"
			"//\n"
			"// PSP game-function patches\n"
			"//\n"
			"// Write C++ replacements and reverse-engineered game declarations here.\n"
			"// Any .cpp files in patches/ are compiled and linked for PSP and macOS.\n"
			"// See patches/README.md for detailed documentation and examples.\n"
			"//\n"
			"#include <psprecomp/patch.hpp>\n\n"
			"namespace {\n\n"
			"// A default Ghidra name carries its image-relative address.\n"
			"// void FUN_00053ba8(int param_1, int param_2) {\n"
			"//     // This body replaces the original game function.\n"
			"// }\n"
			"// RECOMP_PATCH_GHIDRA(FUN_00053ba8);\n\n"
			"// Descriptive names resolve through symbols in the project's code map.\n"
			"// void DaxRenderWorld_Initialize() {\n"
			"// }\n"
			"// RECOMP_PATCH_GHIDRA(DaxRenderWorld_Initialize);\n\n"
			"} // namespace\n";
	}

	std::string patch_tutorial_readme()
	{
		return
			"# PSP Game Patching Framework\n\n"
			"All `.cpp` files in `patches/` are automatically compiled into both target builds:\n"
			"- PSP PRX / EBOOT (`make psp`)\n"
			"- Native macOS executable (`make macos`)\n\n"
			"## Patch a function copied from Ghidra\n\n"
			"Keep the return type and parameters from the recovered prototype. A `FUN_` name contains its own image offset:\n\n"
			"```cpp\n"
			"#include <psprecomp/patch.hpp>\n\n"
			"void FUN_00053ba8(int param_1, int param_2) {\n"
			"    // replacement\n"
			"}\n\n"
			"RECOMP_PATCH_GHIDRA(FUN_00053ba8);\n"
			"```\n\n"
			"For a descriptive name, export that symbol in the code map and register the same name:\n\n"
			"```cpp\n"
			"void DaxRenderWorld_Initialize() {\n"
			"    // replacement\n"
			"}\n\n"
			"RECOMP_PATCH_GHIDRA(DaxRenderWorld_Initialize);\n"
			"```\n\n"
			"You can always register explicitly. Use `image_offset()` for relocatable PRXs and `absolute_address()` for fixed PSP addresses:\n\n"
			"```cpp\n"
			"int calculate_damage(int type, int amount) { return amount * 3; }\n"
			"RECOMP_PATCH_FUNCTION(psprecomp::patch::image_offset(0x4200), calculate_damage);\n"
			"```\n\n"
			"The typed bridge follows the PSP EABI: eight integer registers (`$a0`-`$a3`, `$t0`-`$t3`), eight float registers (`$f12`-`$f19`), aligned 64-bit values, stack arguments, and the correct integer/float/double return registers. Data pointers are translated between guest and host memory. Represent guest callback/function pointers as `uint32_t` addresses.\n\n"
			"## Call reverse-engineered game code and the original\n\n"
			"Declare a strongly typed callable. The parameter list contains types only:\n\n"
			"```cpp\n"
			"DEFINE_GAME_FUNCTION(original_FUN_00053ba8,\n"
			"                     psprecomp::patch::image_offset(0x53ba8),\n"
			"                     void, int, int);\n\n"
			"void FUN_00053ba8(int param_1, int param_2) {\n"
			"    // Bypasses this hook and runs the translated original until it returns.\n"
			"    original_FUN_00053ba8.original(param_1, param_2);\n"
			"}\n\n"
			"RECOMP_PATCH_GHIDRA(FUN_00053ba8);\n"
			"```\n\n"
			"Calling `original_FUN_00053ba8(...)` normally honors patches. Calling `.original(...)` bypasses the patch at that address. Inside any replacement, `psprecomp::patch::call_original<Return>(args...)` is the shorthand for the currently replaced function. Check `last_call_error()` when a defensive failure path matters; unmapped non-null pointers are rejected instead of being passed as null.\n\n"
			"## Globals and startup overwrites\n\n"
			"```cpp\n"
			"DEFINE_GAME_GLOBAL(g_player_health,\n"
			"                   psprecomp::patch::image_offset(0x195010),\n"
			"                   std::uint32_t);\n\n"
			"void apply_initial_values() {\n"
			"    g_player_health = 999;\n"
			"}\n"
			"RECOMP_PATCH_INITIALIZER(apply_initial_values);\n"
			"```\n\n"
			"A `GameGlobal<T>` supports `get()`, `set()`, assignment, and `pointer()`. For `GameGlobal<T*>`, use `get()`/`set()`; `pointer()` cannot expose a 32-bit PSP pointer slot as a native pointer-to-pointer. Initializers run after the guest image is relocated and imports are patched. Low-level access remains available through `read_guest<T>`, `write_guest<T>`, `guest_to_host`, and `host_to_guest`.\n\n"
			"## Raw hooks\n\n"
			"Use `RECOMP_PATCH_RAW(address, hook)` only when the recovered prototype is unknown and direct `State` register access is required. Set the result registers and `state.pc = state.gpr[31]` yourself. Typed hooks are safer for normal game functions.\n\n"
			"Patches replace function entries. Full original calls and startup initializers require normal full-recompile mode; hybrid PSP overlay mode currently supports replacement hooks only.\n";
	}

} // namespace psprecomp::project_detail
