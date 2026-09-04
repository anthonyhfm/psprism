# Project wizard

The project wizard is the recommended entry point for psprism. It turns one
legally obtained PSP ISO, ELF or PRX into a bring-your-own-game project. A
public Git repository contains only metadata, build files and authored
patches; copyrighted code and assets are recreated locally during hydration.

## Interactive use

Build psprism, then launch it with no arguments:

```sh
cmake -S . -B build
cmake --build build -j
./build/psprism
```

You can also pass the input on the command line:

```sh
./build/psprism path/to/game.iso
```

The wizard asks for:

1. The input, unless it was passed on the command line.
2. The display name shown in the PSP menu. ISO titles are read from
   `PSP_GAME/PARAM.SFO` and offered as the default.
3. A build-safe project name. It is derived from the display name by default.
4. The output directory.
5. An optional Ghidra code map. Function metadata produces the best project
   structure and is strongly recommended for large games.
6. Whether a complete ISO filesystem should be extracted.

Before writing anything, the wizard displays a summary and asks for one final
confirmation. Existing output directories are never silently overwritten.
Generation uses a temporary sibling directory and publishes it only after the
entire export succeeds.

## Automated use

Every answer is also available as an option:

```sh
psprism init game.iso \
  --display-name "Example Game Recompiled" \
  --project-name example_game \
  --output ./example_game \
  --code-map ./example.map \
  --extract-disc \
  --yes
```

`--yes` disables all prompts and chooses inferred defaults for unspecified
values. Use `--no-extract-disc` when only the executable is needed.

## Generated project

In a fresh clone, first place the exact supported dump at
`original/disc.iso` (or pass `GAME_INPUT=/path/to/game.iso`). The normal build
targets invoke hydration automatically through the root `Makefile`:

```sh
make psp         # build PRX, EBOOT.PBP and a rebuilt ISO
make psp-run     # build PSP output and launch its writable PPSSPP run tree
make macos       # build a native Release .app (-O3)
make macos-debug # build a native Debug .app
make macos-run   # build and execute the native Release app
make clean       # remove compiler products
```

`make hydrate` validates the input kind, disc ID, complete input SHA-256 and
decrypted executable SHA-256 recorded in `project.toml`. It then extracts the
private disc tree, statically recompiles the executable, generates platform
glue, and copies the runtime headers and `refract` implementation belonging to
the selected psprism toolchain. Use `PSPRISM=/path/to/psprism`, install
`psprism` in `PATH`, or put its source checkout at `toolchain/psprism`.

Successful hydration is cached using the input path, size and modification
time together with the manifest, code map and toolchain revision. Repeated
builds therefore avoid hashing a multi-gigabyte ISO. Set
`HYDRATE_FLAGS=--force` to regenerate all private output.

`make psp` and `make psp-run` never fall back to packaging an untouched PSP
executable. Fixed-address executables and relocatable PRX files both use the
complete generated C++ dispatcher by default. Code-map `overlay` entries opt
relocatable PRXs into the hybrid mode for the explicitly selected translated
functions. The generated Makefile prints `PSP recompile mode: full` or
`PSP recompile mode: overlays` before compiling. PSP C and C++ translation
units are optimized with `-O2`.

The native macOS build discovers FFmpeg from the system and common Homebrew
prefixes. Install it with `brew install ffmpeg`; the generated project links
`avcodec`, `avformat`, `avutil` and `swscale` for PSMF/H.264 cutscenes. CMake
fails during configuration when `REFRACT_USE_FFMPEG=ON` and those development
libraries are unavailable, instead of producing a build with silently broken
video. Use `-DREFRACT_USE_FFMPEG=OFF` only when cutscene decoding is not needed.

For ISO projects, the PSP targets create a lightweight run tree under
`.psprecomp/run`. Game assets are symlinked from `disc/`, while the generated
PRX and `PARAM.SFO` are copied into place. This avoids duplicating the full
disc for every launch and enables the PSP high-memory layout required by large
recompiled executables. `make psp` additionally authors `dist/<project>.iso`;
`make psp-run` launches the writable run tree because some games open disc
assets with permissive flags that an immutable ISO cannot provide in PPSSPP.
Set `PPSSPP=/path/to/ppsspp` if the command is not in `PATH`.

`platform/platform.h` is the hydrated contract for every imported PSP call.
`platform/psp` satisfies it using the original SCE ABI and owns the PSP entry
point. For `make macos-run`, the generated `platform/macos` adapter sends those
imports to the statically linked `refract/` engine. The Makefile also points
refract at the user's ISO and a private `.refract/ms0/` writable tree. The
translated CPU code under `src/generated` therefore has no direct dependency
on PSP SDK headers or SCE functions.

Generated C++ lives in `src/generated`. With a code map, Guest functions are
written one per address-named `func_*.cpp` file, with the original binary range
documented above every definition. Without a map, address-based `shard_*.cpp`
files are emitted. Without overlay selections, all emitted shards/functions
are compiled into the full PSP PRX. Functions marked with `overlay ADDRESS`
switch a relocatable input to the hybrid PSP mode, so edits to those selected
generated bodies affect `make psp` while unselected functions continue using
Guest code.
Generated continuation trampolines let an overlay call an unchanged direct or
indirect Guest function and resume the edited body afterward. Unsupported
dynamic non-return jumps are reported during export.

`src/generated/`, `platform/`, `include/psprecomp/`, `refract/`, `disc/` and
`original/` are deliberately ignored by the generated `.gitignore`. A clean
clone retains only `.gitkeep` placeholders and recreates those trees from the
verified input. Keep game-specific source changes in tracked `patches/`; make
shared compatibility fixes in psprism/refract itself so a later hydration can
safely refresh the runtime. Publish the Git tree, not an archive of your
hydrated working directory.

## Publishing a clean repository

Every export includes `LICENSE`, `LICENSING.md` and
`THIRD_PARTY_NOTICES.md`. Keep all three files in the public repository. They
license the authored project skeleton and patches under GPL version 3 or
later, while explicitly excluding the original game and generated translation
output.

The generated `.gitignore` is sufficient for the normal workflow. Before the
first push, stage and inspect the repository from the exported project root:

```sh
git init
git add .
git status --short --ignored
git ls-files
```

Only the skeleton, patch sources, metadata, documentation and `.gitkeep`
placeholders should be tracked. In particular, `git ls-files` must not show an
ISO, ELF, PRX, PBP, extracted disc file, translated source file, or hydrated
runtime file. `.gitignore` does not remove a file from existing history and
can be bypassed with `git add --force`, so publish from the clean skeleton
history rather than reusing a repository that ever tracked private output.

## ISO notes

psprism currently reads uncompressed ISO 9660 images. CSO and CHD containers
must first be converted to ISO. Retail `EBOOT.BIN` files beginning with `~PSP`
are decrypted automatically using a compatible local PPSSPP installation. The
wizard performs decryption before extracting the complete disc, so a missing or
incompatible provider fails quickly without first copying a large ISO.

PPSSPP is discovered through `PATH`, common macOS application/Homebrew paths,
or an explicit environment variable:

```sh
PSPRECOMP_PPSSPP=/path/to/PPSSPPSDL psprism game.iso
```

`make psp` masters the rebuilt tree with `xorriso`, `mkisofs`, or macOS
`hdiutil`. On APFS, the hdiutil path uses a copy-on-write staging clone so
symlinked assets become real ISO files without permanently duplicating all
disc data. `disc-tree` remains available when an unpacked rebuilt tree is more
useful than an ISO.
