#!/usr/bin/env python3
"""Clean the SLACS2 plugin copy for the NG runtime.

The original P+ 0.7 plugin is used as an input only.  This tool preserves
records that S1/S2 still uses and replaces SLACS_Main_Q's old VMAD with a
property-free SLACS_Main bridge.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER = 24
QUEST_FORM_ID = 0x04000D62
QUEST_LOCAL_ID = 0x00000D62
MEGA_RACES_FORM_ID = 0x04004E66
REMOVED_QUESTS = {0x04000D63, 0x04002E19}
REMOVED_FLST = {0x04000D64}
VMAD_SCRIPT = b"SLACS_Main"
MINIMAL_VMAD = (
    struct.pack("<HHH", 5, 2, 1)
    + struct.pack("<H", len(VMAD_SCRIPT))
    + VMAD_SCRIPT
    + b"\x00"
    + struct.pack("<H", 0)
)


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def record_edid(record: bytes) -> str:
    flags = u32(record, 8)
    if flags & 0x00040000:
        raise ValueError("compressed record encountered; refusing to guess")
    size = u32(record, 4)
    payload = record[HEADER : HEADER + size]
    offset = 0
    while offset + 6 <= len(payload):
        kind = payload[offset : offset + 4]
        length = struct.unpack_from("<H", payload, offset + 4)[0]
        value = payload[offset + 6 : offset + 6 + length]
        if kind == b"EDID":
            return value.rstrip(b"\x00").decode("utf-8", "replace")
        offset += 6 + length
    return ""


def with_minimal_vmad(record: bytes) -> bytes:
    size = u32(record, 4)
    payload = bytearray(record[HEADER : HEADER + size])
    offset = 0
    replaced = False
    while offset + 6 <= len(payload):
        kind = bytes(payload[offset : offset + 4])
        length = struct.unpack_from("<H", payload, offset + 4)[0]
        if kind == b"VMAD":
            payload[offset : offset + 6 + length] = b"VMAD" + struct.pack("<H", len(MINIMAL_VMAD)) + MINIMAL_VMAD
            replaced = True
            break
        offset += 6 + length
    if not replaced:
        payload += b"VMAD" + struct.pack("<H", len(MINIMAL_VMAD)) + MINIMAL_VMAD
    header = bytearray(record[:HEADER])
    struct.pack_into("<I", header, 4, len(payload))
    return bytes(header) + bytes(payload)


def clean(source: bytes) -> tuple[bytes, dict[str, int]]:
    if source[:4] != b"TES4":
        raise ValueError("input is not a TES4 plugin")
    output = bytearray()
    removed = {"MGEF": 0, "QUST": 0, "FLST": 0}
    kept = {"MGEF": 0, "SPEL": 0, "QUST": 0, "EFSH": 0, "FLST": 0}
    vmad_replaced = False

    position = 0
    while position < len(source):
        kind = source[position : position + 4]
        size = u32(source, position + 4)
        if kind != b"GRUP":
            output += source[position : position + HEADER + size]
            position += HEADER + size
            continue

        group_end = position + size
        label = source[position + 8 : position + 12]
        records = bytearray()
        cursor = position + HEADER
        while cursor < group_end:
            rec_kind = source[cursor : cursor + 4]
            rec_size = u32(source, cursor + 4)
            rec_end = cursor + HEADER + rec_size
            record = source[cursor:rec_end]
            fid = u32(source, cursor + 12)
            edid = record_edid(record)
            drop = (
                (label == b"MGEF" and edid.startswith("SexLab"))
                or (label == b"QUST" and fid in REMOVED_QUESTS)
                or (label == b"FLST" and fid in REMOVED_FLST)
            )
            if drop:
                removed[label.decode("ascii", "replace")] = removed.get(label.decode("ascii", "replace"), 0) + 1
            else:
                if label == b"QUST" and fid == QUEST_FORM_ID:
                    record = with_minimal_vmad(record)
                    vmad_replaced = True
                records += record
                kept_key = label.decode("ascii", "replace")
                kept[kept_key] = kept.get(kept_key, 0) + 1
            cursor = rec_end

        if cursor != group_end:
            raise ValueError(f"group {label!r} ended at {cursor}, expected {group_end}")
        header = bytearray(source[position : position + HEADER])
        struct.pack_into("<I", header, 4, HEADER + len(records))
        output += header + records
        position = group_end

    if not vmad_replaced:
        raise ValueError("SLACS_Main_Q was not found")
    if removed["MGEF"] != 33:
        raise ValueError(f"expected 33 removed SexLab MGEFs, got {removed['MGEF']}")
    if removed["QUST"] != 2:
        raise ValueError(f"expected 2 removed quests, got {removed['QUST']}")
    if removed["FLST"] != 1:
        raise ValueError(f"expected 1 removed form list, got {removed['FLST']}")
    if kept.get("QUST") != 1 or kept.get("MGEF") != 26 or kept.get("SPEL") != 26 or kept.get("EFSH") != 26:
        raise ValueError(f"unexpected kept record counts: {kept}")
    return bytes(output), removed


def verify(plugin: bytes) -> None:
    if plugin[:4] != b"TES4":
        raise ValueError("not a TES4 plugin")
    found_quest = False
    found_mega = False
    position = 0
    while position < len(plugin):
        kind = plugin[position : position + 4]
        size = u32(plugin, position + 4)
        if kind != b"GRUP":
            position += HEADER + size
            continue
        label = plugin[position + 8 : position + 12]
        cursor = position + HEADER
        end = position + size
        while cursor < end:
            rec_size = u32(plugin, cursor + 4)
            fid = u32(plugin, cursor + 12)
            record = plugin[cursor : cursor + HEADER + rec_size]
            edid = record_edid(record)
            if label == b"MGEF" and edid.startswith("SexLab"):
                raise ValueError(f"SexLab MGEF still present: {edid}")
            if label == b"QUST":
                if fid in REMOVED_QUESTS:
                    raise ValueError(f"removed quest still present: {fid:08X}")
                if fid == QUEST_FORM_ID:
                    found_quest = True
                    payload = record[HEADER:]
                    offset = 0
                    while offset + 6 <= len(payload):
                        sub = payload[offset : offset + 4]
                        length = struct.unpack_from("<H", payload, offset + 4)[0]
                        if sub == b"VMAD":
                            value = payload[offset + 6 : offset + 6 + length]
                            if value != MINIMAL_VMAD:
                                raise ValueError("SLACS_Main_Q VMAD is not the minimal SLACS_Main bridge")
                            break
                        offset += 6 + length
                    else:
                        raise ValueError("SLACS_Main_Q has no VMAD")
            if label == b"FLST" and fid in REMOVED_FLST:
                raise ValueError(f"removed form list still present: {fid:08X}")
            if label == b"FLST" and fid == MEGA_RACES_FORM_ID:
                found_mega = True
            cursor += HEADER + rec_size
        position += size
    if not found_quest:
        raise ValueError("SLACS_Main_Q missing")
    if not found_mega:
        raise ValueError("SLACS_MegaCumRaces missing")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify.read_bytes())
        print(f"verified {args.verify}")
        return
    if not args.input or not args.output:
        parser.error("--input and --output are required unless --verify is used")
    cleaned, removed = clean(args.input.read_bytes())
    verify(cleaned)
    args.output.write_bytes(cleaned)
    print(f"wrote {args.output} ({len(cleaned)} bytes), removed={removed}")


if __name__ == "__main__":
    main()
