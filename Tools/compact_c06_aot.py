#!/usr/bin/env python3
"""Compact the checked-in C06 AOT wrappers into one exact-block dispatcher."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path


BLOCK_ROW = re.compile(
    r"^ \{(?P<identity>\d+)u,0x(?P<pc>[0-9A-F]{4})u,"
    r"0x(?P<opcode>[0-9A-F]{2})u,(?P<length>\d+)u,",
    re.MULTILINE,
)
FUNCTION = re.compile(
    r"bt_aot_result bt_c06_block_(?P<name>\d{5})"
    r"\(bt_aot_context \*ctx\)\{\n(?P<body>.*?)\n\}",
    re.DOTALL,
)
FUNCTION_TABLE = re.compile(
    r"typedef bt_aot_result \(\*bt_c06_fn\)\(bt_aot_context\*\); "
    r"static const bt_c06_fn fns\[\d+\]=\{\n"
    r"(?: bt_c06_block_\d{5},\n)+\};\n"
)
OLD_EXECUTE = (
    "bt_aot_result bt_c06_execute_index(bt_aot_context *ctx,uint32_t i){ "
    "bt_aot_result r={0}; if(i>=bt_c06_block_count())"
    "{r.stop_reason=BT_AOT_STOP_TARGET_REJECTED;return r;} return fns[i](ctx);}\n"
)
NEW_EXECUTE = """bt_aot_result bt_c06_execute_index(bt_aot_context *ctx,uint32_t i){
 bt_aot_result r={0};
 const bt_c06_block_meta *m;
 if(i>=bt_c06_block_count()){r.stop_reason=BT_AOT_STOP_TARGET_REJECTED;return r;}
 m=&blocks[i];
 r=bt_aot_execute_fixed(ctx,i,m->identity_key,m->pc,m->opcode);
 if(r.stop_reason!=BT_AOT_STOP_NONE)return r;
 if(m->opcode==0x20u){
  r=bt_aot_note_shadow_push(ctx,r,(uint16_t)(m->pc+m->length),BT_AOT_SHADOW_CALL);
  if(r.stop_reason!=BT_AOT_STOP_NONE)return r;
 }
 return bt_aot_finish_target(ctx,r,bt_c06_classify_target(i,r.next_pc));
}
"""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def expected_body(index: int, identity: int, pc: int, opcode: int, length: int) -> str:
    lines = [
        f" bt_aot_result r=bt_aot_execute_fixed(ctx,{index}u,{identity}u,"
        f"0x{pc:04X}u,0x{opcode:02X}u);",
        " if(r.stop_reason!=BT_AOT_STOP_NONE)return r;",
    ]
    if opcode == 0x20:
        lines.extend(
            [
                f" r=bt_aot_note_shadow_push(ctx,r,0x{(pc + length) & 0xFFFF:04X}u,"
                "BT_AOT_SHADOW_CALL);",
                " if(r.stop_reason!=BT_AOT_STOP_NONE)return r;",
            ]
        )
    lines.append(
        f" return bt_aot_finish_target(ctx,r,bt_c06_classify_target({index}u,r.next_pc));"
    )
    return "\n".join(lines)


def compact(source_dir: Path) -> dict[str, object]:
    catalog_path = source_dir / "aot_catalog.c"
    header_path = source_dir / "aot_blocks.h"
    block_paths = sorted(source_dir.glob("blocks_*.c"))
    if not catalog_path.is_file() or not header_path.is_file() or not block_paths:
        raise RuntimeError("Expected uncompacted C06 catalog, header and block shards")

    catalog_bytes = catalog_path.read_bytes()
    catalog = catalog_bytes.decode("utf-8")
    rows = [
        (
            int(match.group("identity")),
            int(match.group("pc"), 16),
            int(match.group("opcode"), 16),
            int(match.group("length")),
        )
        for match in BLOCK_ROW.finditer(catalog)
    ]
    if not rows:
        raise RuntimeError("No C06 block metadata rows were found")

    seen: dict[int, Path] = {}
    shard_bytes = 0
    for path in block_paths:
        data = path.read_bytes()
        shard_bytes += len(data)
        text = data.decode("utf-8")
        matches = list(FUNCTION.finditer(text))
        if not matches:
            raise RuntimeError(f"No AOT wrappers found in {path}")
        for match in matches:
            index = int(match.group("name"))
            if index >= len(rows):
                raise RuntimeError(f"Out-of-range wrapper {index} in {path}")
            if index in seen:
                raise RuntimeError(f"Duplicate wrapper {index} in {path} and {seen[index]}")
            identity, pc, opcode, length = rows[index]
            if match.group("body") != expected_body(index, identity, pc, opcode, length):
                raise RuntimeError(f"Unexpected wrapper body for block {index} in {path}")
            seen[index] = path

    expected_indexes = set(range(len(rows)))
    if set(seen) != expected_indexes:
        missing = sorted(expected_indexes - set(seen))
        raise RuntimeError(f"Wrapper coverage mismatch; first missing indexes: {missing[:10]}")

    compacted = catalog.replace('#include "aot_blocks.h"\n', "", 1)
    compacted, table_count = FUNCTION_TABLE.subn("", compacted, count=1)
    if table_count != 1:
        raise RuntimeError("Expected exactly one generated wrapper function table")
    if compacted.count(OLD_EXECUTE) != 1:
        raise RuntimeError("Expected exactly one generated wrapper dispatcher")
    compacted = compacted.replace(OLD_EXECUTE, NEW_EXECUTE, 1)
    compacted = (
        "/* AUTO-GENERATED compact exact-block AOT catalog. */\n"
        "/* Every original block key remains in the immutable metadata table. */\n"
        + compacted
    )

    compacted_bytes = compacted.encode("utf-8")
    temporary = catalog_path.with_suffix(".c.tmp")
    temporary.write_bytes(compacted_bytes)
    os.replace(temporary, catalog_path)
    header_bytes = header_path.stat().st_size
    header_path.unlink()
    for path in block_paths:
        path.unlink()

    receipt = {
        "schema": "battletech-c64-c06-compact-aot-v1",
        "status": "COMPACT_EXACT_BLOCK_AUTHORITY",
        "block_count": len(rows),
        "call_shadow_block_count": sum(row[2] == 0x20 for row in rows),
        "removed_wrapper_shard_count": len(block_paths),
        "removed_wrapper_source_bytes": shard_bytes,
        "removed_wrapper_header_bytes": header_bytes,
        "original_catalog_bytes": len(catalog_bytes),
        "compact_catalog_bytes": len(compacted_bytes),
        "original_catalog_sha256": sha256(catalog_bytes),
        "compact_catalog_sha256": sha256(compacted_bytes),
        "coverage_policy": (
            "All exact block metadata, identity/view guards, successors and fail-closed "
            "target classifications are retained; only redundant wrapper bodies and "
            "their function-pointer table are folded into one dispatcher."
        ),
    }
    receipt_path = source_dir / "aot_compaction.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "source_dir",
        type=Path,
        help="Path to Generated/C06/Source in an uncompacted source tree",
    )
    arguments = parser.parse_args()
    source_dir = arguments.source_dir.resolve()
    if source_dir.name != "Source" or source_dir.parent.name != "C06":
        raise RuntimeError(f"Refusing unexpected C06 source path: {source_dir}")
    receipt = compact(source_dir)
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
