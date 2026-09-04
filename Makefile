# psprism - C++20 PSP static recompiler toolchain
# Pure Makefile build system

CXX ?= c++
OBJCXX ?= clang++
BUILD_DIR ?= build
BIN_DIR ?= $(BUILD_DIR)/bin
OBJ_DIR ?= $(BUILD_DIR)/obj

CXXFLAGS ?= -O2
COMMON_FLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS)
THIRD_PARTY_FLAGS := -std=c++20 -Wall -Wextra -O2

# Autodetect Homebrew / system paths
UNAME_S := $(shell uname -s)

FFMPEG_PREFIX ?= $(shell if [ -d /opt/homebrew/opt/ffmpeg ]; then echo /opt/homebrew/opt/ffmpeg; elif [ -d /usr/local/opt/ffmpeg ]; then echo /usr/local/opt/ffmpeg; fi)
ifneq ($(FFMPEG_PREFIX),)
FFMPEG_CFLAGS := -DREFRACT_HAS_FFMPEG=1 -I$(FFMPEG_PREFIX)/include
FFMPEG_LIBS := -L$(FFMPEG_PREFIX)/lib -lavcodec -lavformat -lavutil -lswscale
endif

QT_PREFIX ?= $(shell if [ -d /opt/homebrew/opt/qtbase ]; then echo /opt/homebrew/opt/qtbase; elif [ -d /usr/local/opt/qtbase ]; then echo /usr/local/opt/qtbase; fi)
ifneq ($(QT_PREFIX),)
QT_CFLAGS := -F$(QT_PREFIX)/lib -I$(QT_PREFIX)/lib/QtWidgets.framework/Headers -I$(QT_PREFIX)/lib/QtCore.framework/Headers -I$(QT_PREFIX)/lib/QtGui.framework/Headers
QT_DEFS := -DREFRACT_HAS_DESKTOP_DIALOGS=1 -DREFRACT_QT_PLATFORM_PLUGIN_PATH=\"$(QT_PREFIX)/share/qt/plugins/platforms\"
QT_LIBS := -L$(QT_PREFIX)/lib -F$(QT_PREFIX)/lib -framework QtWidgets -framework QtGui -framework QtCore
endif

# Toolchain detection for roundtrip tests
PSPDEV ?= /Users/anthony/pspdev
PSP_BIN ?= $(shell if [ -d "$(PSPDEV)/bin" ]; then echo "$(PSPDEV)/bin"; elif command -v psp-gcc >/dev/null 2>&1; then dirname "$$(command -v psp-gcc)"; fi)
PSP_GCC := $(if $(PSP_BIN),$(PSP_BIN)/psp-gcc,$(shell command -v psp-gcc 2>/dev/null))
PSP_GXX := $(if $(PSP_BIN),$(PSP_BIN)/psp-g++,$(shell command -v psp-g++ 2>/dev/null))
PSP_LD  := $(if $(PSP_BIN),$(PSP_BIN)/psp-ld,$(shell command -v psp-ld 2>/dev/null))
PPSSPP  := $(shell if command -v PPSSPPSDL >/dev/null 2>&1; then command -v PPSSPPSDL; elif [ -x /opt/homebrew/bin/PPSSPPSDL ]; then echo /opt/homebrew/bin/PPSSPPSDL; fi)

# psprism sources
PSPRISM_SRCS := \
    src/main.cpp \
    src/decrypt.cpp \
    src/elf/loader.cpp \
    src/elf/code_map.cpp \
    src/elf/image.cpp \
    src/emitter/relocations.cpp \
    src/emitter/format.cpp \
    src/emitter/analysis.cpp \
    src/emitter/instruction.cpp \
    src/emitter/cpp_output.cpp \
    src/emitter/project_output.cpp \
    src/emitter/coverage.cpp \
    src/iso.cpp \
    src/iso_patch.cpp \
    src/nids.cpp \
    src/project/input.cpp \
    src/project/templates.cpp \
    src/project/iso_patch_template.cpp \
    src/project/export.cpp

PSPRISM_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(PSPRISM_SRCS))

# ATRAC3 decoder sources
ATRAC_SRCS := \
    refract/third_party/at3_standalone/atrac.cpp \
    refract/third_party/at3_standalone/atrac3.cpp \
    refract/third_party/at3_standalone/atrac3plus.cpp \
    refract/third_party/at3_standalone/atrac3plusdec.cpp \
    refract/third_party/at3_standalone/atrac3plusdsp.cpp \
    refract/third_party/at3_standalone/get_bits.cpp \
    refract/third_party/at3_standalone/compat.cpp \
    refract/third_party/at3_standalone/fft.cpp \
    refract/third_party/at3_standalone/mem.cpp

ATRAC_OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(ATRAC_SRCS))

TARGET_PSPRISM := $(BIN_DIR)/psprism

# Test targets
TEST_RUNNERS := \
    $(BIN_DIR)/runtime_tests \
    $(BIN_DIR)/iso_tests \
    $(BIN_DIR)/elf_tests \
    $(BIN_DIR)/emitter_tests \
    $(BIN_DIR)/refract_utility_tests \
    $(BIN_DIR)/refract_audio_tests \
    $(BIN_DIR)/refract_atrac_tests \
    $(BIN_DIR)/refract_ge_tests \
    $(BIN_DIR)/refract_mpeg_tests

ifeq ($(UNAME_S),Darwin)
TEST_RUNNERS += $(BIN_DIR)/refract_frontend_tests
ifneq ($(QT_PREFIX),)
TEST_RUNNERS += $(BIN_DIR)/refract_dialog_tests
endif
endif

BENCHMARK_RUNNER := $(BIN_DIR)/cpu_benchmarks

.PHONY: all psprism test check benchmarks clean help

all: $(TARGET_PSPRISM)

psprism: $(TARGET_PSPRISM)

$(TARGET_PSPRISM): $(PSPRISM_OBJS) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_FLAGS) $^ -ldl -o $@

# Object file pattern rules
$(OBJ_DIR)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_FLAGS) -Isrc -Iinclude \
	    -DPSPRECOMP_SOURCE_INCLUDE_DIR=\"$(CURDIR)/include\" \
	    -DREFRACT_REFRACT_DIR=\"$(CURDIR)/refract\" \
	    -c $< -o $@

$(OBJ_DIR)/refract/third_party/at3_standalone/%.o: refract/third_party/at3_standalone/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(THIRD_PARTY_FLAGS) -Irefract/third_party/at3_standalone -c $< -o $@

$(OBJ_DIR)/refract/src/%.o: refract/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/include -Irefract/include/pspsdk -Irefract/src $(FFMPEG_CFLAGS) -c $< -o $@

# Test executables
$(BIN_DIR)/runtime_tests: tests/runtime_tests.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -DPSPRECOMP_PROFILE_CPU $< -o $@

$(BIN_DIR)/iso_tests: tests/iso_tests.cpp src/iso.cpp src/decrypt.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Isrc -Iinclude $^ -ldl -o $@

$(BIN_DIR)/elf_tests: tests/elf_tests.cpp src/elf/loader.cpp src/elf/code_map.cpp src/elf/image.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Isrc -Iinclude $^ -o $@

$(BIN_DIR)/emitter_tests: tests/emitter_tests.cpp \
    src/emitter/relocations.cpp src/emitter/format.cpp src/emitter/analysis.cpp \
    src/emitter/instruction.cpp src/emitter/cpp_output.cpp src/emitter/project_output.cpp \
    src/emitter/coverage.cpp src/elf/loader.cpp src/elf/code_map.cpp src/elf/image.cpp \
    src/nids.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Isrc -Iinclude $^ -o $@

$(BIN_DIR)/refract_utility_tests: tests/refract_utility_tests.cpp refract/src/utility_data.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/src $^ -o $@

$(BIN_DIR)/refract_audio_tests: tests/refract_audio_tests.cpp refract/src/host/audio_engine.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/src $^ -o $@

$(BIN_DIR)/refract_atrac_tests: tests/refract_atrac_tests.cpp $(ATRAC_OBJS) | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/src -Irefract/third_party/at3_standalone $^ -o $@

$(BIN_DIR)/refract_ge_tests: tests/refract_ge_tests.cpp refract/src/runtime.cpp refract/src/utility_data.cpp refract/src/stubs/mpeg/media_engine.cpp $(ATRAC_OBJS) | $(BIN_DIR)
	$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude -Irefract/include -Irefract/include/pspsdk -Irefract/src $(FFMPEG_CFLAGS) $^ $(FFMPEG_LIBS) -o $@

$(BIN_DIR)/refract_mpeg_tests: tests/refract_mpeg_tests.cpp refract/src/stubs/mpeg/media_engine.cpp $(ATRAC_OBJS) | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/src $(FFMPEG_CFLAGS) $^ $(FFMPEG_LIBS) -o $@

$(BIN_DIR)/refract_frontend_tests: tests/refract_frontend_tests.mm | $(BIN_DIR)
	$(OBJCXX) -std=c++20 -Wall -Wextra -Wpedantic -fobjc-arc -Iinclude -Irefract/include -Irefract/include/pspsdk -Irefract/src $< \
	    -framework AppKit -framework GameController -framework Metal -framework MetalKit -o $@

$(BIN_DIR)/refract_dialog_tests: tests/refract_dialog_tests.cpp refract/src/host/desktop_dialogs.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude -Irefract/src $(QT_CFLAGS) $(QT_DEFS) $^ $(QT_LIBS) -o $@

$(BIN_DIR)/cpu_benchmarks: tests/cpu_benchmarks.cpp | $(BIN_DIR)
	$(CXX) $(COMMON_FLAGS) -Iinclude $< -o $@

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Test runner rule
test: check

check: $(TARGET_PSPRISM) $(TEST_RUNNERS)
	@echo "=== Running psprism Test Suite ==="
	@for test_bin in $(TEST_RUNNERS); do \
	    echo "--- Running $$(basename $$test_bin) ---"; \
	    $$test_bin || exit 1; \
	done
	@if [ -n "$(PSP_GCC)" ] && [ -n "$(PSP_LD)" ] && [ -n "$(PSP_GXX)" ]; then \
	    echo "--- Running PSP Roundtrip Test ---"; \
	    bash tests/roundtrip.sh $(TARGET_PSPRISM) $(PSP_GCC) $(PSP_LD) $(PSP_GXX) $(CXX) $(CURDIR) $(PPSSPP) || exit 1; \
	fi
	@echo "=== All Tests Passed 100%! ==="

benchmarks: $(BENCHMARK_RUNNER)
	@echo "=== Running CPU Benchmarks ==="
	@$(BENCHMARK_RUNNER)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "psprism Build System"
	@echo "  make              Build psprism executable"
	@echo "  make test         Build and execute entire test suite"
	@echo "  make benchmarks   Build and run CPU microbenchmarks"
	@echo "  make clean        Remove all build output"
