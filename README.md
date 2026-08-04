# PSPRecomp

> Static recompilation for PlayStation Portable games, with the PSP itself as
> the first target.

```text
PSP ISO, ELF or PRX
        |
        v
 Allegrex MIPS code
        |
        v
 portable generated C++
        |
        +--------------------+
        |                    |
        v                    v
 PSPSDK output       psprism host runtime
 PRX / EBOOT / ISO       native macOS app
```

PSPRecomp turns a PSP executable into a readable, self-contained C++ project.
That project can currently be built back into a PSP executable or into a native
macOS application through psprism.

The unusual part is intentional: PSP to C++ to PSP comes first. Running the
recompiled program on the original hardware gives us a clean way to validate
CPU translation, relocations, imports and memory behavior before blaming a new
platform backend. Once that path is correct, the same generated C++ can be
connected to other native targets.

PSPRecomp is active research software. It can boot and run substantial retail
game code, but it is not a universal one-click porting solution yet. Expect to
inspect traces, add missing imports and make game-specific psprism fixes.

## What works today

- ELF32 Allegrex loading, PSP relocations and import discovery
- PSP ISO 9660 input with `PARAM.SFO` title and disc ID detection
- Automatic decryption of retail `~PSP` executables through local PPSSPP
- Portable C++ generation for integer, FPU and a broad set of VFPU operations
- Correct delay-slot and branch-likely handling
- One generated `.cpp` file per mapped Guest function
- Original PSP address ranges documented above generated functions
- Native function calls for known static edges, with dispatcher fallback
- PSP PRX, `EBOOT.PBP`, rebuilt ISO and PPSSPP run-tree output
- Native optimized macOS applications using Metal and psprism
- Controller and keyboard input on macOS
- Self-contained exports with their own runtime, build files and documentation

## Compatibility at a glance

Legend: ✅ working in the current test path, 🟡 partial or experimental,
❌ not implemented.

### Accepted input

| Input | Status | Notes |
|---|:---:|---|
| Decrypted PSP ELF | ✅ | Recommended for low-level work |
| Decrypted PRX | ✅ | Relocations and imports are preserved |
| Uncompressed PSP ISO | ✅ | Disc metadata and files can be exported |
| Retail encrypted `EBOOT.BIN` | ✅ | Decrypted through a compatible local PPSSPP installation |
| `BOOT.BIN` inside an ISO | ✅ | Preferred automatically when it is usable |
| CSO or CHD | ❌ | Convert the image to ISO first |
| `EBOOT.PBP` as direct input | ❌ | Extract the contained executable first |

### PSPSDK output compatibility

| Output or toolchain | Status | Notes |
|---|:---:|---|
| Current PSPDEV PSPSDK | ✅ | Used by the automated PSP roundtrip test |
| `psp-g++` with C++20 | ✅ | Compiles the portable runtime and generated functions |
| PSP PRX | ✅ | Main recompiled executable format |
| `EBOOT.PBP` | ✅ | Packaged automatically by the generated Makefile |
| Rebuilt PSP ISO | ✅ | Uses `xorriso`, `mkisofs` or macOS `hdiutil` |
| PPSSPP run tree | ✅ | `make psp-run` builds and launches it |
| Physical PSP hardware | 🟡 | Tested projects run; memory and code size remain constraints |
| Older pre-C++20 PSPSDK toolchains | ❌ | A modern PSPDEV toolchain is required |

The PSP target calls real firmware APIs through PSPSDK. It does not use
psprism for those calls. This makes it the most accurate target for checking
the generated CPU code.

### psprism host targets

| Native target | Status | Graphics | Notes |
|---|:---:|---|---|
| macOS on Apple Silicon | ✅ | Metal | Primary native development target |
| macOS on Intel | 🟡 | Metal | Expected to work, not part of the regular test path |
| Linux | ❌ | Not selected | Host frontend and syscall layer still needed |
| Windows | ❌ | Not selected | Host frontend and syscall layer still needed |
| Android | ❌ | Not selected | Future target |

### psprism subsystem compatibility

| PSP subsystem | Status | Current coverage |
|---|:---:|---|
| Display and VBlank | ✅ | Framebuffer presentation, timing and frame counters |
| GE graphics | 🟡 | Geometry, textures, CLUT, depth, blending, color tests and transfers |
| Kernel threads | 🟡 | Thread creation, startup, waiting, termination and Guest stacks |
| Synchronization | 🟡 | Semaphores, event flags and fixed pools |
| Memory | 🟡 | Partition blocks, fixed pools, scratchpad and emulated VRAM |
| Controllers | ✅ | GameController devices plus keyboard fallback |
| File and directory I/O | 🟡 | Disc, memory-stick paths, sync and common async operations |
| Savedata | 🟡 | Core state flow and basic file handling, no complete PSP UI emulation |
| Message dialogs | 🟡 | Basic lifecycle stubs |
| UMD state | 🟡 | Common checks and drive-state behavior |
| Audio | ❌ | Major audio APIs are still unimplemented |
| MPEG and video decode | ❌ | Game MPEG playback is not implemented by psprism |
| Network and online | ❌ | WLAN, ad hoc, infrastructure and commerce are not implemented |

This table describes the repository version of psprism. Every generated project
receives its own copy under `psprism/`, so a game can carry compatibility fixes
without turning the shared runtime into a pile of title-specific conditions.

## Requirements

### Building PSPRecomp itself

- CMake 3.20 or newer
- A C++20 compiler
- Git

### Building generated PSP output

- A current [PSPDEV](https://pspdev.github.io/) installation
- `psp-config`, `psp-gcc`, `psp-g++` and the PSPSDK tools in `PATH`
- `xorriso`, `mkisofs` or `hdiutil` when producing an ISO

### Decrypting or launching games

- A compatible [PPSSPP](https://www.ppsspp.org/) installation
- The `ppsspp` CLI in `PATH`, or `PPSSPP=/path/to/ppsspp`
- `PSPRECOMP_PPSSPP=/path/to/PPSSPPSDL` when PSPRecomp cannot find the
  decryption provider automatically

### Building the native macOS target

- macOS
- Xcode Command Line Tools or Xcode
- CMake and Apple Clang
- A Metal-capable Mac

Ghidra is optional, but strongly recommended for real games. A code map gives
PSPRecomp reliable function boundaries and names.

## Build PSPRecomp

```sh
git clone https://github.com/anthonyhfm/psp-recomp.git
cd psp-recomp

cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The host tool and unit tests build without PSPSDK. When PSPDEV is available,
CMake also enables the complete `psp_cpp_psp_roundtrip` test. That test compiles
a PSP fixture, recompiles it to C++, runs it natively, compiles it back to PSP
and packages a real `EBOOT.PBP`.

## Quickstart with a game ISO

The interactive wizard is the easiest path:

```sh
./build/psprecomp game.iso
```

You can also start it without an argument:

```sh
./build/psprecomp
```

The wizard inspects the input and asks for:

1. A display name
2. A build-safe project name
3. An output directory
4. An optional Ghidra code map
5. Whether to export the complete disc filesystem

It shows a summary before writing anything. Existing output directories are not
silently overwritten.

For automation, pass every value explicitly:

```sh
./build/psprecomp init game.iso \
  --display-name "My Game Recompiled" \
  --project-name my_game \
  --output ./my_game \
  --code-map ./game.map \
  --extract-disc \
  --yes
```

For an already decrypted executable:

```sh
./build/psprecomp init BOOT.BIN \
  --display-name "My Game" \
  --project-name my_game \
  --output ./my_game \
  --code-map ./game.map \
  --yes
```

## Build the generated project

Enter the exported directory. It is completely independent from the
PSPRecomp repository.

```sh
cd my_game
```

### Build for PSP and PPSSPP

```sh
make psp
```

For an ISO export this produces:

```text
src/generated/<project>.prx
src/generated/EBOOT.PBP
dist/<project>.iso
```

Build and launch the writable PPSSPP run tree with:

```sh
make psp-run
```

If PPSSPP is not in `PATH`:

```sh
make psp-run PPSSPP=/path/to/ppsspp
```

The run tree lives under `.psprecomp/run`. Disc assets are linked from `disc/`
instead of being copied for every launch.

### Build the native macOS app

Release builds are the default and use the performance-oriented CMake
configuration:

```sh
make macos
make macos-run
```

Use a Debug build when working on psprism itself:

```sh
make macos-debug
```

Runtime logs are quiet by default. Enable import, thread, GE and Guest traces
with:

```sh
make macos-run MACOS_RUN_ARGS=--verbose
```

You can pass the flag to the app directly as well:

```sh
./build/macos/my_game.app/Contents/MacOS/my_game --verbose
```

## What gets generated

```text
my_game/
├── Makefile
├── CMakeLists.txt
├── README.md
├── project.toml
├── config/
│   └── code.map
├── include/psprecomp/
├── psprism/
├── platform/
│   ├── platform.h
│   ├── psp/
│   └── macos/
├── src/generated/
│   ├── dispatch.cpp
│   ├── func_00000000.cpp
│   ├── func_000000bc.cpp
│   ├── generated.hpp
│   └── generated_sources.cmake
├── disc/
└── original/
```

With a code map, every discovered Guest function gets its own address-named
source file:

```cpp
// Original PSP binary range: [0x000000bc, 0x00000360)
bool run_function_000000bc(State& state, std::uint32_t entry_pc) {
  // generated Allegrex behavior
}
```

Known static calls can become native C++ calls. Indirect calls, unusual entry
points and uncertain control flow still go through `dispatch.cpp`.

Without a code map, PSPRecomp cannot safely claim to know every function
boundary. In that mode it emits address-based `shard_*.cpp` files instead.

## Code maps and Ghidra

A code map is a small text file containing the module entry, discovered
functions and optional excluded ranges:

```text
entry 0x00000000
function 0x00000000 module_start
function 0x000000bc game_main
exclude 0x00100000 0x00101000
```

Function metadata improves generated structure, direct-call lowering and human
navigability. Names are also used when resolving known import symbols.

See [docs/code-maps.md](docs/code-maps.md) for the format and Ghidra export
workflow.

## Low-level commands

Inspect executable coverage:

```sh
./build/psprecomp input.elf --analyze --code-map input.map
```

Emit one standalone C++ file for instruction testing:

```sh
./build/psprecomp input.elf -o generated.cpp
```

Emit the lower-level multi-file project without the guided wrapper:

```sh
./build/psprecomp input.elf \
  --output-dir out/generated \
  --code-map input.map
```

Run `./build/psprecomp --help` for the complete command list.

## Troubleshooting

### The ISO contains an encrypted `EBOOT.BIN`

Install PPSSPP or point PSPRecomp at it:

```sh
PSPRECOMP_PPSSPP=/path/to/PPSSPPSDL ./build/psprecomp game.iso
```

### `psp-config` cannot be found

Add PSPDEV to your environment:

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
```

### A native game starts but an API is missing

Run the native target with `--verbose`, find the one-time `unimplemented`
message, then add the behavior to the exported project's `psprism/` copy. The
PSP target can help establish what the original firmware call should do.

### A game is black or visually corrupted

Start by comparing the PSP build in PPSSPP with the native psprism build. If the
PSP build is also wrong, investigate generated CPU behavior and the code map. If
only the native build is wrong, investigate GE state, texture decoding and the
Metal frontend.

### Native performance is unexpectedly poor

Use `make macos`, not `make macos-debug`. The Release target enables compiler
optimization and IPO when supported. Profiling should be done in Release mode.

## Repository layout

```text
include/psprecomp/  Portable CPU, relocation and VFPU runtime
psprism/            PSP-to-host runtime copied into every export
src/                CLI, ISO/ELF loading, decryption and C++ emitter
tests/              Host tests and PSP roundtrip fixtures
tools/              Reverse-engineering and metadata helpers
docs/               Architecture, wizard and code-map documentation
```

For the design behind the CPU state, dispatcher, import boundary and target
split, read [docs/architecture.md](docs/architecture.md).

## Legal note

This repository contains no game code, keys or copyrighted game assets. Use
PSPRecomp only with dumps and files you are legally entitled to inspect. Keep
commercial disc images, decrypted executables and extracted assets out of
public repositories.
