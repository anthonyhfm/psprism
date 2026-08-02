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
    entry_address = entry_text.split("@", 1)[1].split("[", 1)[0].strip()

    lines = [
        "# psprecomp-ghidra-map-v1",
        f"# program {args.program}",
        f"entry 0x{entry_address}",
    ]
    for function in functions:
        if not isinstance(function, dict) or function.get("isExternal"):
            continue
        lines.append(
            f"function 0x{function['address']} {function.get('name', '')}".rstrip()
        )

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

    lines.insert(2, f"# functions {len(functions)}")
    lines.insert(3, f"# excluded_gaps {excluded} orphaned_code_gaps {orphaned}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(
        f"wrote {args.output}: {len(functions)} functions, "
        f"{excluded} excluded gaps, {orphaned} orphaned-code gaps kept"
    )


if __name__ == "__main__":
    main()
