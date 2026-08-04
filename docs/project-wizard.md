# Project wizard

The project wizard is the recommended entry point for PSPRecomp. It turns one
legally obtained PSP ISO, ELF or PRX into a complete source tree that can be
built independently from the recompiler repository.

## Interactive use

Build PSPRecomp, then launch it with no arguments:

```sh
cmake -S . -B build
cmake --build build -j
./build/psprecomp
```

You can also pass the input on the command line:

```sh
./build/psprecomp path/to/game.iso
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
psprecomp init game.iso \
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

The root `Makefile` is the normal entry point:

```sh
make psp         # build PRX, EBOOT.PBP and a rebuilt ISO
make psp-run     # build PSP output and launch its writable PPSSPP run tree
make macos       # build a native Release .app (-O3)
make macos-debug # build a native Debug .app
make macos-run   # build and execute the native Release app
make clean       # remove compiler products
```

For ISO exports, the PSP targets create a lightweight run tree under
`.psprecomp/run`. Game assets are symlinked from `disc/`, while the generated
PRX and `PARAM.SFO` are copied into place. This avoids duplicating the full
disc for every launch and enables the PSP high-memory layout required by large
recompiled executables. `make psp` additionally authors `dist/<project>.iso`;
`make psp-run` launches the writable run tree because some games open disc
assets with permissive flags that an immutable ISO cannot provide in PPSSPP.
Set `PPSSPP=/path/to/ppsspp` if the command is not in `PATH`.

`platform/platform.h` is the generated contract for every imported PSP call.
`platform/psp` satisfies it using the original SCE ABI and owns the PSP entry
point. For `make macos-run`, the generated `platform/macos` adapter sends those
imports to the statically linked `psprism/` engine. The Makefile also points
psprism at `disc/` and a private `.psprism/ms0/` writable tree. The translated
CPU code under `src/generated` therefore has no direct dependency on PSP SDK
headers or SCE functions.

Generated C++ lives in `src/generated`. With a code map, Guest functions are
written one per address-named `func_*.cpp` file, with the original binary range
documented above every definition. Without a map, address-based `shard_*.cpp`
files are emitted. The portable runtime is copied into `include/psprecomp`, and
the complete psprism
source is copied into `psprism/`, so moving or archiving the export does not
break its build. That psprism copy is meant to be edited when a game needs a
host compatibility quirk.

`disc/` and `original/` are deliberately ignored by the generated `.gitignore`
to make accidental publication of copyrighted data less likely. Generated C++,
configuration, runtime headers and documentation remain trackable.

## ISO notes

PSPRecomp currently reads uncompressed ISO 9660 images. CSO and CHD containers
must first be converted to ISO. Retail `EBOOT.BIN` files beginning with `~PSP`
are decrypted automatically using a compatible local PPSSPP installation. The
wizard performs decryption before extracting the complete disc, so a missing or
incompatible provider fails quickly without first copying a large ISO.

PPSSPP is discovered through `PATH`, common macOS application/Homebrew paths,
or an explicit environment variable:

```sh
PSPRECOMP_PPSSPP=/path/to/PPSSPPSDL psprecomp game.iso
```

`make psp` masters the rebuilt tree with `xorriso`, `mkisofs`, or macOS
`hdiutil`. On APFS, the hdiutil path uses a copy-on-write staging clone so
symlinked assets become real ISO files without permanently duplicating all
disc data. `disc-tree` remains available when an unpacked rebuilt tree is more
useful than an ISO.
