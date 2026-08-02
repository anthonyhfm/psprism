# Code maps

A code map supplies function starts, names, the program entry point and ranges
that analysis has identified as non-code. It improves project-mode output but
is never treated as infallible: generated dispatch remains available for valid
indirect entry points.

## Format

The format is line-oriented. Addresses accept the usual C-style `0x` prefix.
Blank lines and lines beginning with `#` are ignored.

```text
# psprecomp-ghidra-map-v1
entry 0x00000000
function 0x00000000 module_start
function 0x00000120 update_scene
exclude 0x00000400 0x00000440
```

- `entry ADDRESS` sets the initial Guest PC.
- `function ADDRESS [NAME]` records a discovered function start and optional
  symbol used for PSP import resolution.
- `exclude BEGIN END` describes a half-open, four-byte-aligned non-code range.

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
