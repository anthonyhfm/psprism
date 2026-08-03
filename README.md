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
functions for discovered Guest functions, bundles them into manageable
translation units and preserves the central PC dispatcher as a fallback for
indirect or uncertain entry points. Without a map, it emits address-based code
shards.

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
- Address-sharded and function-oriented project emitters
- Runtime unit tests plus native and PSP roundtrip coverage

Important remaining work includes broader instruction validation, better
function/control-flow recovery, smaller generated code, register-local code
generation, faster indirect dispatch, robust platform abstraction and extensive
differential testing on PPSSPP and physical hardware.

## Project layout

```text
include/psprecomp/   Portable CPU, relocation and VFPU runtime
src/                 ELF/PRX loader, CLI and C++ project emitter
tests/               Synthetic fixtures and host/PSP roundtrip tests
tools/               Optional reverse-engineering metadata exporters
docs/                Architecture and code-map documentation
```

The architectural boundaries and next implementation stages are described in
[docs/architecture.md](docs/architecture.md).
