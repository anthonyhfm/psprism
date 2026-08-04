# PSPRecomp

PSPRecomp is an experimental static recompiler for PSP/Allegrex executables.
Its first validation path deliberately targets the PSP again:

```text
PSP ELF/PRX (Allegrex MIPS)
    -> generated portable C++
    -> PSPSDK
    -> PSP PRX / EBOOT.PBP
```

Recompiling back to the original platform separates CPU translation bugs from
platform-porting bugs. The generated C++ and runtime interfaces are intended to
support additional host backends later.

The project can boot substantial real-world code, but it is still research
software. Compatibility, generated-code size and gameplay performance are not
yet production-ready.

## Repository policy

This repository contains only the recompiler, its runtime and synthetic test
fixtures. Game dumps, extracted disc trees, decrypted commercial executables,
Ghidra projects, generated code and build artifacts are intentionally ignored.
Only use executables and assets you are legally entitled to inspect.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler
- PSPSDK in `PATH` or referenced by `PSPDEV` for PSP roundtrip tests and PRX
  generation
- PPSSPP in `PATH` for automatic decryption of retail `~PSP` executables
- Optional: Ghidra plus a compatible local API endpoint for exporting code maps

## Build and test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Without PSPSDK, CMake still builds the host tools and runtime tests. When the
PSP compilers are available, `psp_cpp_psp_roundtrip` additionally compiles a
synthetic PSP fixture, recompiles it to C++, executes it natively, and packages
the generated code as a real PRX and `EBOOT.PBP`.

## Command-line usage

### Guided project export

Start the interactive wizard with no arguments, or give it an input file right
away:

```sh
./build/psprecomp
./build/psprecomp game.iso
```

The wizard accepts PSP ISO images as well as decrypted ELF/PRX executables. For
an ISO it discovers `EBOOT.BIN`/`BOOT.BIN`, reads the display name and disc ID
from `PARAM.SFO`, and can extract the complete disc filesystem. It then asks for
the remaining project choices and produces a self-contained codebase:

```text
my_game/
├── Makefile                 one-command build entry point
├── CMakeLists.txt           native macOS app build
├── README.md                generated project guide
├── project.toml             reproducible project metadata
├── config/code.map          optional copied function metadata
├── include/psprecomp/       vendored portable runtime
├── psprism/                 vendored PSP-to-host syscall engine
├── platform/platform.h      imported PSP API contract
├── platform/psp/            PSP entry point and SCE implementations
├── platform/macos/          native entry point and host implementations
├── src/generated/           platform-independent generated C++ functions
├── disc/                    complete extracted ISO filesystem
└── original/                input executable for non-ISO exports
```

Build it without referring back to this repository:

```sh
cd my_game
make psp-run
```

Use `make psp` to produce the PSP PRX, EBOOT and rebuilt ISO without launching
the emulator. `make macos` builds a performance-oriented Release `.app` with
`-O3`; `make macos-debug` remains available for debugging, and `make macos-run`
builds and executes the Release app. Runtime diagnostics are quiet by default;
use `make macos-run MACOS_RUN_ARGS=--verbose` to enable them. The PSP target
calls the firmware through `platform/psp`. Native targets use the vendored
`psprism/` engine; `platform/macos` is the generated adapter between the game's
import table and psprism. Every export owns its copy, so title-specific syscall
quirks stay local to that game.

For scripts and automation, use the same workflow without prompts:

```sh
./build/psprecomp init game.iso \
  --display-name "My Recompilation" \
  --project-name my_recomp \
  --output out/my_recomp \
  --code-map path/to/program.map \
  --yes
```

Run `psprecomp --help` for all wizard options. ISO extraction is enabled by
default and can be disabled with `--no-extract-disc`. Encrypted retail
`EBOOT.BIN` files are detected and decrypted automatically through a compatible
local PPSSPP installation. `PSPRECOMP_PPSSPP` can point to the PPSSPP executable
when it cannot be discovered through `PATH` or a standard installation path.

See [docs/project-wizard.md](docs/project-wizard.md) for the complete workflow
and generated layout.

### Low-level commands

Inspect an executable and report loader/translation coverage:

```sh
./build/psprecomp input.elf --analyze
```

Generate one portable C++ dispatcher, useful for small fixtures and semantic
tests:

```sh
./build/psprecomp input.elf -o generated.cpp
```

Generate a complete multi-file PSP project:

```sh
./build/psprecomp input.elf \
  --output-dir out/recompiled \
  --code-map path/to/program.map
make -C out/recompiled -j
```

The code map is optional. With one, the project emitter creates individual C++
functions for discovered Guest functions, writes each function into its own
address-named source file and preserves the central PC dispatcher as a fallback
for indirect or uncertain entry points. Every definition includes its original
PSP binary address range. Without a map, it emits address-based code shards.

See [docs/code-maps.md](docs/code-maps.md) for the map format and Ghidra export
workflow.

## Current implementation

- ELF32 little-endian MIPS loading with load segments and PSP imports
- PSP PRX relocation parsing and runtime relocation application
- Allegrex integer, branch, jump, HI/LO, aligned and unaligned memory operations
- COP1/FPU state and a broad VFPU execution layer
- Correct MIPS delay-slot and branch-likely control flow
- Portable `State` model with explicit stop and fault reasons
- PSP-native direct memory access and native PSPSDK import bridge
- Guest thread wrappers with independent Guest stacks
- Generated image/relocation embedding and PSPSDK Makefile output
- Guided ISO/ELF project wizard with `PARAM.SFO` metadata discovery
- Automatic retail `~PSP` executable decryption through local PPSSPP
- Streaming ISO 9660 extraction and complete self-contained codebase exports
- Vendored psprism engine with an initial macOS syscall and filesystem backend
- Address-sharded and function-oriented project emitters
- Runtime unit tests plus native and PSP roundtrip coverage

Important remaining work includes broader instruction validation, better
function/control-flow recovery, smaller generated code, register-local code
generation, faster indirect dispatch, robust platform abstraction and extensive
differential testing on PPSSPP and physical hardware.

## Project layout

```text
include/psprecomp/   Portable CPU, relocation and VFPU runtime
psprism/             PSP-to-host runtime template vendored into every export
src/                 ISO/ELF loaders, wizard, CLI and C++ project emitter
tests/               Synthetic fixtures and host/PSP roundtrip tests
tools/               Optional reverse-engineering metadata exporters
docs/                Architecture and code-map documentation
```

The architectural boundaries and next implementation stages are described in
[docs/architecture.md](docs/architecture.md).
