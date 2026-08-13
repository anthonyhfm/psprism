# Code maps

A code map supplies function starts, names, the program entry point and ranges
that analysis has identified as non-code. It improves project-mode output but
is never treated as infallible: generated dispatch remains available for valid
indirect entry points.

## Format

The format is line-oriented. Addresses accept the usual C-style `0x` prefix.
Blank lines and lines beginning with `#` are ignored.

```text
# psprecomp-ghidra-map-v2
entry 0x00000000
function_range 0x00000000 0x00000120 module_start
function_range 0x00000120 0x00000400 update_scene
block 0x00000180
gp 0x00000120 0x08900000
t9 0x00000120 0x00000120
overlay 0x00000120
exclude 0x00000400 0x00000440
```

- `entry ADDRESS` sets the initial Guest PC.
- `function_range BEGIN END [NAME]` records a half-open function range and
  optional symbol used for PSP import resolution. Version-1
  `function ADDRESS [NAME]` records remain accepted for backward compatibility.
- `block ADDRESS` records a valid control-flow entry. Indirect dispatch can use
  these entries without treating every aligned word as a block start.
- `gp FUNCTION_ADDRESS VALUE` and `t9 FUNCTION_ADDRESS VALUE` preserve
  entry-register facts discovered by analysis.
- `overlay ADDRESS` selects a mapped function for the hybrid PSP build. Its
  `func_ADDRESS.cpp` body is compiled back to Allegrex and detours only that
  Guest entry point; unselected functions continue executing from the original
  relocated image.
- `exclude BEGIN END` describes a half-open, four-byte-aligned non-code range.

Direct calls, indirect link calls and branch-and-link instructions leave the
translated function through the original Allegrex target. Their return address
is replaced with a generated continuation trampoline, which captures the
resulting ABI state and resumes the translated caller immediately after its
delay slot. Internal branches, normal returns and direct tail transfers remain
valid. Dynamic non-return `jr` transfers are currently rejected during export,
and a continuation must remain within its mapped function. The entry's first
two instructions must not carry loader relocations because the detour occupies
those words.

After export, edit the selected `src/generated/func_ADDRESS.cpp` and run
`make psp`. The generated bridge saves the Guest integer, GP, HI/LO and FPU
state; VFPU registers and control state are additionally preserved when the
selected function contains VFPU instructions. It then restores the translated
result and resumes either an unchanged Guest callee or the original caller.

## Exporting from Ghidra

`tools/export_ghidra_map.py` talks to a compatible local Ghidra HTTP endpoint.
It exports internal functions, entry-point metadata and confirmed non-code gaps
while retaining gaps that contain orphaned instructions.

```sh
python3 tools/export_ghidra_map.py \
  --server http://127.0.0.1:8089 \
  --program YOUR_PROGRAM_NAME \
  --output program.map
```

Map files normally describe a specific executable and are ignored by Git. Keep
commercial symbols, executables and analysis databases outside this repository.
