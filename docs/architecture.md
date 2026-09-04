# Architecture

## Goal

PSPRecomp reproduces observable program behavior; byte-identical output is not
a goal. The first backend remains PSP so CPU semantics, imports and generated
control flow can be verified before introducing a second platform layer.

```text
ISO 9660 or ELF/PRX
    -> source discovery, SFO metadata and optional disc extraction
    -> ELF loader, segments, relocations and imports
    -> Allegrex instruction decoder
    -> address and function metadata
    -> portable C++ emitter
    -> CPU runtime and generated platform contract
    -> platform/psp (PSPSDK PRX/ISO) or psprism + platform/macos (native app)
```

## Loader and relocated image

The frontend accepts either a direct executable or an ISO 9660 disc image. The
ISO reader has no external runtime dependencies: it walks directory records,
normalizes PSP paths, discovers the usable `EBOOT.BIN`/`BOOT.BIN`, and reads
`TITLE` and `DISC_ID` from `PARAM.SFO`. Full-disc extraction streams data in
bounded chunks so movie and audio assets do not have to fit in host memory.
Retail executables using the `~PSP` container are decrypted before extraction
through a dynamically discovered local PPSSPP implementation. Keeping this a
runtime integration avoids copying PPSSPP's GPL-licensed cryptography sources
into PSPRecomp while still making the normal ISO path automatic.

The loader reads ELF32 MIPS program and section metadata, preserves load
segments, records PSP relocation entries and discovers import stubs. Generated
PSP projects embed both the Guest memory image and compact relocation records.
At startup the image receives an aligned runtime address, relocations are
applied against that address, and known import stubs are patched or bridged.

Relocated instruction fields cannot always be baked into generated C++ because
the final Guest image address is selected by the PSP loader. Those instruction
words are read from the relocated image when needed; non-relocated immediates
remain constants in generated code.

## CPU state and memory

`psprecomp::State` contains Guest GPRs, HI/LO, COP1 registers, VFPU registers,
control flags, the Guest PC and explicit fault information. Host tests use a
bounded byte buffer. PSP builds use native addresses for RAM, scratchpad and
VRAM so PSPSDK calls and Guest code observe the same pointers.

Guest thread stacks are separate allocations. Native PSP thread stacks remain
host/compiler stacks and are sized independently; this avoids treating a native
C++ stack pointer as a Guest-visible MIPS stack.

## Control flow

Every Allegrex instruction is translated ahead of time. Two project layouts are
available:

1. Address shards group code by PC range. Direct branches within a shard become
   C++ labels; exits return to a central dispatcher.
2. With a code map, each discovered Guest function becomes a distinct C++
   function. Static calls use native calls, static tail transfers can remain
   direct, and indirect or uncertain targets fall back to the dispatcher.

Each discovered Guest function is written to an address-named `func_*.cpp`
file. A comment directly above its definition records the original PSP binary
range. This keeps the physical source layout aligned with the semantic function
layout and makes individual functions easy to inspect or replace.

PSP output uses the complete translated C++ dispatcher by default, including
for relocatable input PRXs. The embedded Guest image supplies initialized data
and relocated instruction fields, but execution enters `generated::run` and
all generated shard/function objects are linked into the new PRX. Code-map
`overlay` entries explicitly select the optional hybrid mode: only their edited
`func_*.cpp` runners are compiled into the new PRX, and their Guest entries are
patched to generated Allegrex trampolines after relocation.
The trampolines preserve the Guest integer, GP, HI/LO, FPU and, when used,
VFPU state. A translated call into an unselected function restores that state,
runs the original Allegrex callee, and returns through an address-derived
continuation trampoline into the translated caller. Normal returns resume the
original caller. This hybrid mode keeps unmodified game logic byte-native while
making selected generated logic effective in PSP builds.

Delay slots remain explicit. A control instruction records the pending target,
the delay instruction executes, and only then does generated control flow apply
the target. Branch-likely instructions annul the delay slot on the untaken path.

## Imports and threads

Known PSP imports are resolved from code-map symbols and forwarded through a
small MIPS assembly bridge. The bridge transfers Guest argument registers,
stack arguments and relevant floating-point values to the native ABI, then
writes return values back into `State`.

Thread creation is intercepted so a native PSP thread receives its own
`State`, Guest stack, entry PC and return sentinel while sharing the relocated
Guest memory image.

## Portability boundary

Instruction semantics and generated CPU code must not depend on PSPSDK headers.
Every discovered import becomes an `ImportId` in `platform/platform.h`.
Generated dispatch code calls the small `configure_runtime`, `patch_imports`,
`dispatch_import` and logging interface from that header; it never names an SCE
function directly.

`platform/psp` owns module metadata, the MIPS ABI bridge, native thread
trampolines and bindings for every resolved SCE import. On native targets,
`platform/macos` is deliberately thin: the generated switch maps the game's
import addresses to names, then delegates behavior to `psprism`.

`psprism` is the PSP-to-host syscall engine. Its common runtime owns guest
pointer validation, PSP path translation, file handles and fallback reporting;
the first host module supplies macOS clocks and sleeping. Every exported
codebase receives the complete engine source in its root and links it as a
static library. This is an intentional compatibility boundary: a game may
carry local quirks while the repository copy remains the default for future
exports. Unimplemented calls are logged once and return a PSP error. Linux and
Windows host modules can later implement the same small host interface.

## Export boundary

The low-level emitter creates translated sources plus PSP and macOS platform
glue.
The project exporter wraps that output in a stable, beginner-facing codebase.
It records input identity and choices in `project.toml`, copies the optional
code map, and writes tracked build and patching files. Generated code, platform
glue, runtime copies, original input and disc files are Git-ignored.

In a clean clone, `psprism hydrate` verifies the user's dump against its disc
ID and cryptographic hashes, rebuilds those private trees in a sibling staging
directory, and publishes them atomically. The generated Makefile invokes this
step before PSP and macOS builds and caches the result using the manifest, code
map, input identity and toolchain revision. Failed validation or translation
therefore cannot leave a plausible-looking partial hydration.

## Verification strategy

- Unit-test instruction, relocation, FPU and VFPU helpers.
- Compile synthetic C with PSPSDK, recompile it to C++, and compare it with the
  native reference implementation.
- Compile the same generated C++ back to a PSP PRX and package an EBOOT.
- Hydrate a clean export and build it as a native optimized macOS application.
- Compare register, memory and import traces between original and recompiled
  programs.
- Treat PPSSPP and physical PSP hardware as independent validation targets.

## Next performance work

Function-oriented output removes many dispatcher roundtrips but still stores
Guest registers in `State` for almost every operation. The main optimization
path is function-level register caching or SSA-like locals with explicit spills
at imports, indirect exits and observable memory boundaries. Generated-code
size, relocation loads and VFPU lowering also require measurement-driven work.
