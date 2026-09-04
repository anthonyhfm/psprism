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
			   "GENERATED_SRCS := $(wildcard src/generated/func_*.cpp src/generated/shard_*.cpp) src/generated/dispatch.cpp\n"
			   "PATCH_SRCS := $(wildcard patches/*.cpp)\n"
			   "PLATFORM_SRCS := platform/macos/main.cpp platform/macos/platform.cpp\n\n"
			   "MACOS_INCLUDES := -I. -Iinclude -Isrc/generated -Ipatches -Irefract/include -Irefract/include/pspsdk -Irefract/src -Irefract/third_party/at3_standalone $(FFMPEG_CFLAGS) $(QT_CFLAGS) $(QT_DEFS)\n\n"
			   "MACOS_LIBS := -framework AppKit -framework AudioToolbox -framework GameController -framework Metal -framework MetalKit $(FFMPEG_LIBS) $(QT_LIBS)\n\n"
			   ".PHONY: all psp-recompile-check psp-binary psp psp-run macos macos-debug macos-run clean rebuild ppsspp disc-tree help\n\n"
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
		out << "macos: $(MACOS_BIN_DIR)/" << config.project_name << "\n\n"
			   "$(MACOS_BIN_DIR)/" << config.project_name << ": $(GENERATED_SRCS) $(PATCH_SRCS) $(PLATFORM_SRCS) $(REFRACT_SRCS) refract/src/host/macos_frontend.mm\n"
			   "\t@mkdir -p $(MACOS_BIN_DIR) $(MACOS_RES_DIR) $(MACOS_OBJ_DIR)\n"
			   "\t@echo \"Compiling native macOS application (" << config.project_name << ")...\"\n"
			   "\t@$(OBJCXX) $(MACOS_CXXFLAGS) -fobjc-arc $(MACOS_INCLUDES) -c refract/src/host/macos_frontend.mm -o $(MACOS_OBJ_DIR)/macos_frontend.o\n"
			   "\t@$(CXX) $(MACOS_CXXFLAGS) $(MACOS_INCLUDES) $(GENERATED_SRCS) $(PATCH_SRCS) $(PLATFORM_SRCS) $(REFRACT_SRCS) $(MACOS_OBJ_DIR)/macos_frontend.o $(MACOS_LIBS) -o $@\n"
			   "\t@cp -f src/generated/guest_image.bin src/generated/relocations.bin $(MACOS_RES_DIR)/ 2>/dev/null || true\n"
			   "\t@printf '<?xml version=\"1.0\" encoding=\"UTF-8\"?>\\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\\n<plist version=\"1.0\">\\n<dict>\\n  <key>CFBundleExecutable</key>\\n  <string>"
			<< config.project_name
			<< "</string>\\n  <key>CFBundleIdentifier</key>\\n  <string>dev.psprecomp."
			<< config.project_name
			<< "</string>\\n  <key>CFBundleName</key>\\n  <string>"
			<< toml_string(config.display_name)
			<< "</string>\\n  <key>CFBundlePackageType</key>\\n  <string>APPL</string>\\n</dict>\\n</plist>\\n' > $(MACOS_APP_DIR)/Contents/Info.plist\n"
			   "\t@echo \"Built $(MACOS_APP_DIR)\"\n\n"
			   "macos-debug:\n"
			   "\t$(MAKE) macos MACOS_BUILD_TYPE=Debug\n\n"
			   "macos-run: macos\n"
			   "\tREFRACT_DISC_ROOT=\"$(CURDIR)/disc\" \\\n"
			   "\tREFRACT_DISC_IMAGE=\"$(CURDIR)/original/disc.iso\" \\\n"
			   "\tREFRACT_WRITABLE_ROOT=\"$(CURDIR)/.refract/ms0\" \\\n"
			   "\t\"$(CURDIR)/$(MACOS_BIN_DIR)/"
			<< config.project_name
			<< "\" $(MACOS_RUN_ARGS)\n\n"
			   "ppsspp: psp-run\n\n"
			   "clean:\n"
			   "\t$(MAKE) -C src/generated clean\n"
			   "\trm -rf build/macos dist .psprecomp\n\n"
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

	std::string patch_template_source()
	{
		return
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
