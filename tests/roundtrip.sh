#!/bin/sh
set -eu

RECOMPILER=$1
PSP_GCC=$2
PSP_LD=$3
PSP_GXX=$4
HOST_CXX=$5
SOURCE_DIR=$6
ROUNDTRIP_TMP=$(mktemp -d "${TMPDIR:-/tmp}/psprecomp-roundtrip.XXXXXX")
PPSSPP_PID=
cleanup() {
    if [ -n "$PPSSPP_PID" ]; then
        kill "$PPSSPP_PID" 2>/dev/null || true
        wait "$PPSSPP_PID" 2>/dev/null || true
    fi
    rm -rf "$ROUNDTRIP_TMP"
}
trap cleanup EXIT HUP INT TERM

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
# Direct function files: each discovered guest function gets its own well-named file.
"$PSP_LD" -T "$SOURCE_DIR/tests/fixtures/project.ld" \
    "$ROUNDTRIP_TMP/arithmetic.o" -o "$ROUNDTRIP_TMP/project.elf"
"$RECOMPILER" "$ROUNDTRIP_TMP/project.elf" \
    --output-dir "$ROUNDTRIP_TMP/project"
post_delay_entry=$(${PSP_GCC%/*}/psp-nm "$ROUNDTRIP_TMP/project.elf" |
    awk '$3 == "recomp_post_delay_entry" { printf "%08x", ("0x" $1) + 8 }')
grep -q "case 0x${post_delay_entry}U" "$ROUNDTRIP_TMP/project/"func_*.cpp
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
    -c "$ROUNDTRIP_TMP/project/dispatch.cpp" \
    -o "$ROUNDTRIP_TMP/project-dispatch-psp.o"
for f in "$ROUNDTRIP_TMP/project/"func_*.cpp; do
    "$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
        -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
        -c "$f" -o "$ROUNDTRIP_TMP/$(basename "$f" .cpp)-psp.o"
done
"$HOST_CXX" -std=c++20 -O2 -DPSPRECOMP_TEST_ENTRY=0 \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project" \
    "$ROUNDTRIP_TMP/project/dispatch.cpp" \
    "$ROUNDTRIP_TMP/project/"func_*.cpp \
    "$SOURCE_DIR/tests/generated_runner.cpp" \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" \
    -o "$ROUNDTRIP_TMP/project-runner"
"$ROUNDTRIP_TMP/project-runner"

# A function map produces one well-named source file per discovered
# function with symbol names included in the filenames.
printf 'entry 0\nfunction 0 recomp_test\nfunction 0x20 recomp_loop\n' \
    > "$ROUNDTRIP_TMP/project.map"
"$RECOMPILER" "$ROUNDTRIP_TMP/project.elf" \
    --output-dir "$ROUNDTRIP_TMP/project-functions" \
    --code-map "$ROUNDTRIP_TMP/project.map"
test -s "$ROUNDTRIP_TMP/project-functions/func_00000000_recomp_test.cpp"
test -s "$ROUNDTRIP_TMP/project-functions/func_00000020_recomp_loop.cpp"
function_file_count=$(find "$ROUNDTRIP_TMP/project-functions" \
    -name 'func_*.cpp' | wc -l | tr -d ' ')
test "$function_file_count" = 2
if find "$ROUNDTRIP_TMP/project-functions" -name 'unit_*.cpp' -o -name 'shard_*.cpp' | grep -q .; then
    echo "function-mode export still contains clustered unit or shard files" >&2
    exit 1
fi
grep -q '// Original PSP binary range: \[0x00000000, 0x' \
    "$ROUNDTRIP_TMP/project-functions/func_00000000_recomp_test.cpp"
grep -q '// Original PSP binary range: \[0x00000020, 0x' \
    "$ROUNDTRIP_TMP/project-functions/func_00000020_recomp_loop.cpp"
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project-functions" \
    -c "$ROUNDTRIP_TMP/project-functions/func_00000000_recomp_test.cpp" \
    -o "$ROUNDTRIP_TMP/project-function-psp.o"
"$PSP_GXX" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project-functions" \
    -c "$ROUNDTRIP_TMP/project-functions/func_00000020_recomp_loop.cpp" \
    -o "$ROUNDTRIP_TMP/project-function-loop-psp.o"
"$HOST_CXX" -std=c++20 -O2 -DPSPRECOMP_TEST_ENTRY=0 \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project-functions" \
    "$ROUNDTRIP_TMP/project-functions/dispatch.cpp" \
    "$ROUNDTRIP_TMP/project-functions/func_00000000_recomp_test.cpp" \
    "$ROUNDTRIP_TMP/project-functions/func_00000020_recomp_loop.cpp" \
    "$SOURCE_DIR/tests/generated_runner.cpp" \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" \
    -o "$ROUNDTRIP_TMP/project-function-runner"
"$ROUNDTRIP_TMP/project-function-runner"

# Execute a real named game-function patch. The replacement writes a typed
# guest global and calls the translated original at the same address.
"$HOST_CXX" -std=c++20 -O2 \
    -I"$SOURCE_DIR/include" -I"$ROUNDTRIP_TMP/project-functions" \
    "$ROUNDTRIP_TMP/project-functions/dispatch.cpp" \
    "$ROUNDTRIP_TMP/project-functions/func_00000000_recomp_test.cpp" \
    "$ROUNDTRIP_TMP/project-functions/func_00000020_recomp_loop.cpp" \
    "$SOURCE_DIR/tests/generated_patch_runner.cpp" \
    "$SOURCE_DIR/tests/fixtures/arithmetic.c" \
    -o "$ROUNDTRIP_TMP/project-patch-runner"
"$ROUNDTRIP_TMP/project-patch-runner"

# Exercise the beginner-facing, self-contained codebase exporter. The exported
# project must build without referring back to PSPRecomp's source tree.
# Use the fixed-address fixture here so the beginner-facing target also covers
# that full-C++ loader; the relocatable full loader is exercised below.
"$RECOMPILER" init "$ROUNDTRIP_TMP/arithmetic.elf" \
    --display-name "Roundtrip Export" \
    --project-name roundtrip_export \
    --output "$ROUNDTRIP_TMP/exported" \
    --yes
test -s "$ROUNDTRIP_TMP/exported/project.toml"
test -s "$ROUNDTRIP_TMP/exported/include/psprecomp/runtime.hpp"
test -s "$ROUNDTRIP_TMP/exported/platform/platform.h"
test -s "$ROUNDTRIP_TMP/exported/platform/psp/main.cpp"
test -s "$ROUNDTRIP_TMP/exported/platform/psp/platform.cpp"
test -s "$ROUNDTRIP_TMP/exported/platform/macos/main.cpp"
test -s "$ROUNDTRIP_TMP/exported/platform/macos/platform.cpp"
test -s "$ROUNDTRIP_TMP/exported/include/psprecomp/patch.hpp"
test ! -e "$ROUNDTRIP_TMP/exported/patches/patch.hpp"
test -s "$ROUNDTRIP_TMP/exported/patches/patches.cpp"
test -s "$ROUNDTRIP_TMP/exported/patches/README.md"
test -s "$ROUNDTRIP_TMP/exported/refract/include/refract/refract.hpp"
test -s "$ROUNDTRIP_TMP/exported/refract/src/runtime.cpp"
test ! -f "$ROUNDTRIP_TMP/exported/CMakeLists.txt"
grep -q 'refract::Runtime::instance' \
    "$ROUNDTRIP_TMP/exported/platform/macos/platform.cpp"
grep -q '^psp:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^psp-run:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^PSP_RECOMPILE_MODE := full$' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^psp: psp-binary$' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^CFLAGS = -O2 ' \
    "$ROUNDTRIP_TMP/exported/src/generated/Makefile"
if grep -q '^CFLAGS = -Os ' \
    "$ROUNDTRIP_TMP/exported/src/generated/Makefile"; then
    echo "generated PSP build still optimizes for size" >&2
    exit 1
fi
grep -q '^macos:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^macos-debug:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q '^macos-run:' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q 'MACOS_BUILD_TYPE ?= Release' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q 'MACOS_RUN_ARGS ?=' "$ROUNDTRIP_TMP/exported/Makefile"
grep -q 'set_verbose(true)' \
    "$ROUNDTRIP_TMP/exported/platform/macos/main.cpp"
if grep -E 'sce[A-Z]|pspkernel[.]h' "$ROUNDTRIP_TMP/exported/src/generated/"*.cpp; then
    echo "portable generated core contains a direct PSP API dependency" >&2
    exit 1
fi
printf '%s\n' \
    '#include <psprecomp/patch.hpp>' \
    'extern "C" int psprism_patch_link_probe(int value) { return value + 1; }' \
    'RECOMP_PATCH_FUNCTION(psprecomp::patch::image_offset(0x007ffffcU), psprism_patch_link_probe);' \
    > "$ROUNDTRIP_TMP/exported/patches/link_probe.cpp"
make -C "$ROUNDTRIP_TMP/exported" psp -j2
test -s "$ROUNDTRIP_TMP/exported/src/generated/roundtrip_export.prx"
test -s "$ROUNDTRIP_TMP/exported/src/generated/EBOOT.PBP"
test -s "$ROUNDTRIP_TMP/exported/src/generated/user_patch_link_probe.o"
"$PSP_BIN_DIR/psp-nm" "$ROUNDTRIP_TMP/exported/src/generated/roundtrip_export.elf" | \
    grep -q 'psprism_patch_link_probe'
if [ "$(uname -s)" = Darwin ]; then
    make -C "$ROUNDTRIP_TMP/exported" macos
    test -x "$ROUNDTRIP_TMP/exported/build/macos/roundtrip_export.app/Contents/MacOS/roundtrip_export"
fi

# Exercise the native PSP bridge used for relocatable retail-style PRX files.
# This path embeds the original Allegrex image, applies its loader relocations,
# reconstructs the import tables, and enters module_start without translating
# the CPU instructions through C++ first.
PSP_SDK_DIR=$("$PSP_BIN_DIR/psp-config" --pspsdk-path)
"$PSP_GCC" -O1 -G0 -D_PSP_FW_VERSION=600 -I"$PSP_SDK_DIR/include" -c \
    "$SOURCE_DIR/tests/fixtures/relocatable_prx.c" \
    -o "$ROUNDTRIP_TMP/relocatable-fixture.o"
"$PSP_GCC" -L"$PSP_SDK_DIR/../lib" -L"$PSP_SDK_DIR/lib" \
    -specs="$PSP_SDK_DIR/lib/prxspecs" -Wl,-q \
    -T"$PSP_SDK_DIR/lib/linkfile.prx" \
    "$ROUNDTRIP_TMP/relocatable-fixture.o" \
    "$PSP_SDK_DIR/lib/prxexports.o" -lpspdebug -lpspkernel \
    -o "$ROUNDTRIP_TMP/relocatable-fixture.elf"
"$PSP_BIN_DIR/psp-fixup-imports" \
    "$ROUNDTRIP_TMP/relocatable-fixture.elf"
"$PSP_BIN_DIR/psp-prxgen" "$ROUNDTRIP_TMP/relocatable-fixture.elf" \
    "$ROUNDTRIP_TMP/relocatable-fixture.prx"
"$PSP_BIN_DIR/psp-nm" -n --defined-only \
    "$ROUNDTRIP_TMP/relocatable-fixture.elf" | \
    awk '$2 == "T" || $2 == "t" { print "function 0x" $1 " " $3 }' \
    > "$ROUNDTRIP_TMP/relocatable.map"
overlay_address=$("$PSP_BIN_DIR/psp-nm" \
    "$ROUNDTRIP_TMP/relocatable-fixture.elf" | \
    awk '$3 == "overlay_target" { print $1 }')
callee_address=$("$PSP_BIN_DIR/psp-nm" \
    "$ROUNDTRIP_TMP/relocatable-fixture.elf" | \
    awk '$3 == "unchanged_callee" { print $1 }')
test -n "$overlay_address"
test -n "$callee_address"
overlay_resume=$("$PSP_BIN_DIR/psp-objdump" -d \
    "$ROUNDTRIP_TMP/relocatable-fixture.elf" | \
    awk '/<overlay_target>:/ { in_overlay = 1; next }
         in_overlay && /jal.*<unchanged_callee>/ {
             sub(":", "", $1)
             printf "%08x", ("0x" $1) + 8
             exit
         }
         in_overlay && /^$/ { in_overlay = 0 }')
test -n "$overlay_resume"
# A relocatable PRX without explicit overlays must use the complete translated
# dispatcher rather than silently packaging its untouched Allegrex image.
"$RECOMPILER" init "$ROUNDTRIP_TMP/relocatable-fixture.prx" \
    --display-name "Relocatable Original Guard" \
    --project-name relocatable_original_guard \
    --output "$ROUNDTRIP_TMP/relocatable-original-guard" \
    --code-map "$ROUNDTRIP_TMP/relocatable.map" \
    --yes
grep -q '^PSP_RECOMPILE_MODE := full$' \
    "$ROUNDTRIP_TMP/relocatable-original-guard/Makefile"
grep -q '^OBJS = main[.]o platform[.]o imports[.]o dispatch[.]o' \
    "$ROUNDTRIP_TMP/relocatable-original-guard/src/generated/Makefile"
make -C "$ROUNDTRIP_TMP/relocatable-original-guard" psp -j2
printf 'overlay 0x%s\n' "$overlay_address" \
    >> "$ROUNDTRIP_TMP/relocatable.map"
"$RECOMPILER" init "$ROUNDTRIP_TMP/relocatable-fixture.prx" \
    --display-name "Relocatable Fixture" \
    --project-name relocatable_fixture \
    --output "$ROUNDTRIP_TMP/relocatable-exported" \
    --code-map "$ROUNDTRIP_TMP/relocatable.map" \
    --yes
grep -q '^PSP_RECOMPILE_MODE := overlays$' \
    "$ROUNDTRIP_TMP/relocatable-exported/Makefile"
overlay_source=$(ls "$ROUNDTRIP_TMP/relocatable-exported/src/generated/func_${overlay_address}"*.cpp | head -n 1)
test -s "$overlay_source"
grep -q 'state[.]gpr\[2\] = state[.]gpr\[2\] + 0x00000007U;' \
    "$overlay_source"
sed 's/+ 0x00000007U;/+ 0x00000064U;/' "$overlay_source" \
    > "$ROUNDTRIP_TMP/modified-overlay.cpp"
mv "$ROUNDTRIP_TMP/modified-overlay.cpp" "$overlay_source"
grep -q 'state[.]gpr\[2\] = state[.]gpr\[2\] + 0x00000064U;' \
    "$overlay_source"
"$HOST_CXX" -std=c++20 -O2 -I"$SOURCE_DIR/include" \
    -I"$ROUNDTRIP_TMP/relocatable-exported/src/generated" \
    -DPSPRECOMP_PSP_OVERLAY \
    -DPSPRECOMP_OVERLAY_FUNCTION=run_function_${overlay_address} \
    -DPSPRECOMP_OVERLAY_START=0x${overlay_address}U \
    -DPSPRECOMP_OVERLAY_CALLEE=0x${callee_address}U \
    -DPSPRECOMP_OVERLAY_RESUME=0x${overlay_resume}U \
    "$overlay_source" "$SOURCE_DIR/tests/overlay_runner.cpp" \
    -o "$ROUNDTRIP_TMP/overlay-runner"
"$ROUNDTRIP_TMP/overlay-runner"
grep -q '^OBJS = .*overlay[.]o .*psp_overlays[.]o .*native_guest_image[.]o' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/Makefile"
grep -q 'apply_compact_psp_relocations' \
    "$ROUNDTRIP_TMP/relocatable-exported/platform/psp/main.cpp"
grep -q 'native_guest_image_start' \
    "$ROUNDTRIP_TMP/relocatable-exported/platform/psp/main.cpp"
grep -q 'psprecomp_install_overlays' \
    "$ROUNDTRIP_TMP/relocatable-exported/platform/psp/main.cpp"
grep -q 'mfvc.*[$]128' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -q '^psprecomp_overlay_stub_1:' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'addiu $k0, $zero, 1' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'sw $gp, 112($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'lw $gp, 112($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'mfhi $k0' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'mthi $k0' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'swc1 $f0, 136($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'lwc1 $f31, 260($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'sv.q C000, 272($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'lv.q C730, 768($sp)' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -Fq 'mtvc $k0, $143' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/psp_overlays.S"
grep -q -- '-DPSPRECOMP_PSP_OVERLAY' \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/Makefile"
test "$(wc -c < "$ROUNDTRIP_TMP/relocatable-exported/src/generated/native_guest_image.bin")" \
    = "$(wc -c < "$ROUNDTRIP_TMP/relocatable-exported/src/generated/guest_image.bin")"
make -C "$ROUNDTRIP_TMP/relocatable-exported" psp -j2
test -s "$ROUNDTRIP_TMP/relocatable-exported/src/generated/relocatable_fixture.prx"
overlay_native_address=$("$PSP_BIN_DIR/psp-nm" \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/relocatable_fixture.elf" | \
    awk -v target="run_function_${overlay_address}" \
    '$3 ~ target { print $1; exit }')
overlay_native_size=$("$PSP_BIN_DIR/psp-nm" -S \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/relocatable_fixture.elf" | \
    awk -v target="run_function_${overlay_address}" \
    '$4 ~ target { print $2; exit }')
test -n "$overlay_native_address"
test -n "$overlay_native_size"
overlay_native_end=$(printf '%x' \
    "$((0x$overlay_native_address + 0x$overlay_native_size))")
"$PSP_BIN_DIR/psp-objdump" -d \
    --start-address="0x$overlay_native_address" \
    --stop-address="0x$overlay_native_end" \
    "$ROUNDTRIP_TMP/relocatable-exported/src/generated/relocatable_fixture.prx" | \
    grep -q 'addiu.*100'

# When PPSSPP is installed, execute the generated PRX as part of the
# regression. The expected 124 crosses both directions of the hybrid bridge:
# translated 8 -> unchanged Allegrex callee (24) -> translated +100.
if command -v PPSSPPSDL >/dev/null 2>&1; then
    ppsspp_log="$ROUNDTRIP_TMP/ppsspp-overlay.log"
    PPSSPPSDL -d -i --windowed --log="$ppsspp_log" \
        "$ROUNDTRIP_TMP/relocatable-exported/src/generated/EBOOT.PBP" \
        >"$ppsspp_log" 2>&1 &
    PPSSPP_PID=$!
    overlay_executed=false
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        if grep -q 'PSPRECOMP_OVERLAY_RESULT=124' "$ppsspp_log"; then
            overlay_executed=true
            break
        fi
        if ! kill -0 "$PPSSPP_PID" 2>/dev/null; then
            break
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    kill "$PPSSPP_PID" 2>/dev/null || true
    wait "$PPSSPP_PID" 2>/dev/null || true
    PPSSPP_PID=
    if [ "$overlay_executed" != true ]; then
        tail -100 "$ppsspp_log" >&2
        exit 1
    fi
fi
