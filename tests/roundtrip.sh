#!/bin/sh
set -eu

RECOMPILER=$1
PSP_GCC=$2
PSP_LD=$3
PSP_GXX=$4
HOST_CXX=$5
SOURCE_DIR=$6
ROUNDTRIP_TMP=$(mktemp -d "${TMPDIR:-/tmp}/psprecomp-roundtrip.XXXXXX")
trap 'rm -rf "$ROUNDTRIP_TMP"' EXIT HUP INT TERM

"$PSP_GCC" -O1 -G0 -mno-abicalls -fno-pic -c \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" -o "$ROUNDTRIP_TMP/arithmetic.o"
"$PSP_LD" -T "$SOURCE_DIR/tests/fixtures/linked.ld" \
    "$ROUNDTRIP_TMP/arithmetic.o" -o "$ROUNDTRIP_TMP/arithmetic.elf"
"$RECOMPILER" "$ROUNDTRIP_TMP/arithmetic.elf" \
    -o "$ROUNDTRIP_TMP/generated.cpp"

# This is the second PSP leg: the generated portable C++ must compile for PSP.
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -c "$ROUNDTRIP_TMP/generated.cpp" \
    -o "$ROUNDTRIP_TMP/generated-psp.o"

# Link the translated code into a real PSP PRX and package an EBOOT.PBP.
PSP_BIN_DIR=${PSP_GCC%/*}
PATH="$PSP_BIN_DIR:$PATH"
export PATH
make -C "$ROUNDTRIP_TMP" -f "$SOURCE_DIR/tests/psp/Makefile" \
    SOURCE_DIR="$SOURCE_DIR"
test -s "$ROUNDTRIP_TMP/roundtrip.prx"
test -s "$ROUNDTRIP_TMP/EBOOT.PBP"

# Execute the same generated C++ natively and compare it with the source function.
"$HOST_CXX" -std=c++20 -O2 -I"$SOURCE_DIR/include" \
    "$ROUNDTRIP_TMP/generated.cpp" "$SOURCE_DIR/tests/generated_runner.cpp" \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" -o "$ROUNDTRIP_TMP/runner"
"$ROUNDTRIP_TMP/runner"

# Exercise project mode too.  This is the performance-oriented path used by
# full games: a shard switch is entered once, then straight-line guest code
# follows direct C++ labels until control flow leaves the block.
"$PSP_LD" -T "$SOURCE_DIR/tests/fixtures/project.ld" \
    "$ROUNDTRIP_TMP/arithmetic.o" -o "$ROUNDTRIP_TMP/project.elf"
"$RECOMPILER" "$ROUNDTRIP_TMP/project.elf" \
    --output-dir "$ROUNDTRIP_TMP/project"
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
    -c "$ROUNDTRIP_TMP/project/dispatch.cpp" \
    -o "$ROUNDTRIP_TMP/project-dispatch-psp.o"
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
    -c "$ROUNDTRIP_TMP/project/shard_0000.cpp" \
    -o "$ROUNDTRIP_TMP/project-shard-psp.o"
"$HOST_CXX" -std=c++20 -O2 -DPSPRECOMP_TEST_ENTRY=0 \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
    "$ROUNDTRIP_TMP/project/dispatch.cpp" \
    "$ROUNDTRIP_TMP/project/shard_0000.cpp" \
    "$SOURCE_DIR/tests/generated_runner.cpp" \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" \
    -o "$ROUNDTRIP_TMP/project-runner"
"$ROUNDTRIP_TMP/project-runner"

# Exercise the beginner-facing, self-contained codebase exporter. The exported
# project must build without referring back to PSPRecomp's source tree.
"$RECOMPILER" init "$ROUNDTRIP_TMP/project.elf" \
    --display-name "Roundtrip Export" \
    --project-name roundtrip_export \
    --output "$ROUNDTRIP_TMP/exported" \
    --yes
test -s "$ROUNDTRIP_TMP/exported/project.toml"
test -s "$ROUNDTRIP_TMP/exported/include/psprecomp/runtime.hpp"
grep -q '^ppsspp:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q 'PPSSPP ?= ppsspp' "$ROUNDTRIP_TMP/exported/Makefile"
make -C "$ROUNDTRIP_TMP/exported" -j2
test -s "$ROUNDTRIP_TMP/exported/src/generated/roundtrip_export.prx"
test -s "$ROUNDTRIP_TMP/exported/src/generated/EBOOT.PBP"
