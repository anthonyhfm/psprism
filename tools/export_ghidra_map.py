#!/usr/bin/env python3
"""Export a stable PSPRecomp code map from a live Ghidra MCP instance."""

from __future__ import annotations

import argparse
import json
import urllib.parse
import urllib.request
from pathlib import Path


def get_json(server: str, path: str, **params: object) -> object:
    query = urllib.parse.urlencode(params)
    with urllib.request.urlopen(f"{server.rstrip('/')}/{path}?{query}") as response:
        return json.load(response)


def address_value(value: object) -> int:
    """Accept the hex strings used by Ghidra MCP and ordinary JSON numbers."""
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if ":" in text:
        text = text.rsplit(":", 1)[1]
    return int(text, 16 if not text.lower().startswith("0x") else 0)


def function_end(function: dict[str, object], begin: int) -> int | None:
    """Read enhanced-function response variants without depending on one MCP build."""
    for key in ("bodyEnd", "endAddress", "end"):
        if function.get(key) not in (None, ""):
            end = address_value(function[key]) + 1
            return (end + 3) & ~3
    for key in ("bodySize", "size", "length"):
        if function.get(key) not in (None, ""):
            size = int(str(function[key]), 0)
            return (begin + size + 3) & ~3
    return None


def nested_addresses(value: object) -> set[int]:
    result: set[int] = set()
    if isinstance(value, list):
        for item in value:
            result.update(nested_addresses(item))
    elif isinstance(value, dict):
        for key in ("address", "start", "entry", "target"):
            if key in value:
                try:
                    result.add(address_value(value[key]))
                except (TypeError, ValueError):
                    pass
    else:
        try:
            result.add(address_value(value))
        except (TypeError, ValueError):
            pass
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default="http://127.0.0.1:8089")
    parser.add_argument("--program", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    functions_doc = get_json(
        args.server,
        "list_functions_enhanced",
        program=args.program,
        offset=0,
        limit=100000,
    )
    if not isinstance(functions_doc, dict):
        raise RuntimeError("unexpected Ghidra function response")
    functions = functions_doc.get("functions", [])

    gaps: list[dict[str, object]] = []
    offset = 0
    while True:
        page = get_json(
            args.server,
            "find_code_gaps",
            program=args.program,
            min_size=1,
            offset=offset,
            limit=1000,
        )
        if not isinstance(page, dict):
            raise RuntimeError("unexpected Ghidra gap response")
        items = page.get("gaps", [])
        if not isinstance(items, list):
            raise RuntimeError("unexpected Ghidra gap list")
        gaps.extend(items)
        offset += len(items)
        if offset >= int(page.get("total", offset)) or not items:
            break

    entry_text = urllib.request.urlopen(
        f"{args.server.rstrip('/')}/get_entry_points?"
        + urllib.parse.urlencode({"program": args.program})
    ).read().decode("utf-8")
    entry_address = address_value(
        entry_text.split("@", 1)[1].split("[", 1)[0].strip()
    )

    lines = [
        "# psprecomp-ghidra-map-v2",
        f"# program {args.program}",
        "version 2",
        f"entry 0x{entry_address:08x}",
    ]
    block_entries: set[int] = set()
    for function in functions:
        if not isinstance(function, dict) or function.get("isExternal"):
            continue
        begin = address_value(function["address"])
        name = str(function.get("name", ""))
        end = function_end(function, begin)
        if end is not None and end > begin:
            lines.append(
                f"function_range 0x{begin:08x} 0x{end:08x} {name}".rstrip()
            )
        else:
            lines.append(f"function 0x{begin:08x} {name}".rstrip())
        # Allegrex PIC calls conventionally enter with t9 equal to the
        # function address.  Enhanced MCP builds may additionally expose a
        # recovered gp value.
        lines.append(f"t9 0x{begin:08x} 0x{begin:08x}")
        for key in ("gpValue", "gp"):
            if function.get(key) not in (None, ""):
                lines.append(
                    f"gp 0x{begin:08x} 0x{address_value(function[key]):08x}"
                )
                break
        block_entries.add(begin)
        for key in ("basicBlocks", "blockEntries", "indirectTargets"):
            block_entries.update(nested_addresses(function.get(key, [])))

    for address in sorted(block_entries):
        lines.append(f"block 0x{address:08x}")

    excluded = 0
    orphaned = 0
    for gap in gaps:
        if bool(gap.get("has_orphaned_instructions")):
            orphaned += 1
            continue
        begin = int(str(gap["start"]), 16)
        end = int(str(gap["end"]), 16) + 1
        if begin % 4 != 0 or end % 4 != 0:
            continue
        lines.append(f"exclude 0x{begin:08x} 0x{end:08x}")
        excluded += 1

    lines.insert(2, f"# functions {len(functions)} blocks {len(block_entries)}")
    lines.insert(3, f"# excluded_gaps {excluded} orphaned_code_gaps {orphaned}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(
        f"wrote {args.output}: {len(functions)} functions, "
        f"{excluded} excluded gaps, {orphaned} orphaned-code gaps kept"
    )


if __name__ == "__main__":
    main()
