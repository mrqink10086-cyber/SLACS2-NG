"""Verify that every non-ASCII character used by the Chinese UI exists in the
bundled CJK font. Usage: py tools/verify_chinese_font.py [--font PATH] [--strict]

The cmap reader is intentionally dependency-free so the packaging step works
with any Python 3 installation.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

DEFAULT_FONT = ROOT / "package" / "Data" / "SKSE" / "Plugins" / "Fonts" / "NotoSansCJKsc-Regular.otf"

SOURCES = [
    ROOT / "src" / "menu" / "Menu.cpp",
    ROOT / "src" / "menu" / "MenuText.cpp",
    ROOT / "package" / "Data" / "SKSE" / "Plugins" / "SKSEMenuFrameworkStrings.json",
    ROOT / "package" / "README.zh-CN.md",
]


def _parse_format4(data: bytes, offset: int) -> set[int]:
    (seg_count_x2,) = struct.unpack_from(">H", data, offset + 6)
    seg_count = seg_count_x2 // 2
    end_codes = offset + 14
    start_codes = end_codes + seg_count_x2 + 2
    deltas = start_codes + seg_count_x2
    range_offsets = deltas + seg_count_x2
    codepoints: set[int] = set()
    for index in range(seg_count):
        end = struct.unpack_from(">H", data, end_codes + index * 2)[0]
        start = struct.unpack_from(">H", data, start_codes + index * 2)[0]
        delta = struct.unpack_from(">h", data, deltas + index * 2)[0]
        range_offset = struct.unpack_from(">H", data, range_offsets + index * 2)[0]
        if start > end or start == 0xFFFF:
            continue
        for code in range(start, end + 1):
            if range_offset == 0:
                glyph = (code + delta) & 0xFFFF
            else:
                glyph_offset = range_offsets + index * 2 + range_offset + (code - start) * 2
                if glyph_offset + 2 > len(data):
                    continue
                (glyph,) = struct.unpack_from(">H", data, glyph_offset)
                if glyph != 0:
                    glyph = (glyph + delta) & 0xFFFF
            if glyph != 0:
                codepoints.add(code)
    return codepoints


def _parse_format12(data: bytes, offset: int) -> set[int]:
    (group_count,) = struct.unpack_from(">I", data, offset + 12)
    codepoints: set[int] = set()
    for index in range(group_count):
        group = offset + 16 + index * 12
        if group + 12 > len(data):
            break
        start, end, start_glyph = struct.unpack_from(">III", data, group)
        if end < start or end - start > 0x10FFFF:
            continue
        for code in range(start, end + 1):
            if start_glyph + (code - start) != 0:
                codepoints.add(code)
    return codepoints


def _parse_format6(data: bytes, offset: int) -> set[int]:
    first, count = struct.unpack_from(">HH", data, offset + 6)
    codepoints: set[int] = set()
    for index in range(count):
        (glyph,) = struct.unpack_from(">H", data, offset + 10 + index * 2)
        if glyph != 0:
            codepoints.add(first + index)
    return codepoints


def load_codepoints(font_path: pathlib.Path) -> set[int]:
    data = font_path.read_bytes()
    if data[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
        raise SystemExit(f"unexpected font signature: {data[:4].hex()}")
    (table_count,) = struct.unpack_from(">H", data, 4)
    cmap_offset = None
    for index in range(table_count):
        record = 12 + index * 16
        tag, _checksum, offset, _length = struct.unpack_from(">4sIII", data, record)
        if tag == b"cmap":
            cmap_offset = offset
            break
    if cmap_offset is None:
        raise SystemExit(f"font has no cmap table: {font_path}")

    (subtable_count,) = struct.unpack_from(">H", data, cmap_offset + 2)
    codepoints: set[int] = set()
    for index in range(subtable_count):
        record = cmap_offset + 4 + index * 8
        platform, encoding, subtable = struct.unpack_from(">HHI", data, record)
        if platform != 0 and not (platform == 3 and encoding in (1, 10)):
            continue
        absolute = cmap_offset + subtable
        if absolute + 2 > len(data):
            continue
        (format_number,) = struct.unpack_from(">H", data, absolute)
        if format_number == 4:
            codepoints |= _parse_format4(data, absolute)
        elif format_number == 12:
            codepoints |= _parse_format12(data, absolute)
        elif format_number == 6:
            codepoints |= _parse_format6(data, absolute)
    return codepoints


def missing_codepoints(font_path: pathlib.Path) -> dict[int, set[str]]:
    codepoints = load_codepoints(font_path)
    missing: dict[int, set[str]] = {}
    for source in SOURCES:
        if not source.exists():
            continue
        text = source.read_text(encoding="utf-8")
        for char in set(text):
            if ord(char) < 0x80:
                continue
            if ord(char) not in codepoints:
                missing.setdefault(ord(char), set()).add(source.name)
    return missing


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=pathlib.Path, default=DEFAULT_FONT)
    parser.add_argument("--strict", action="store_true", help="fail on missing glyphs")
    args = parser.parse_args()

    if not args.font.exists():
        print(f"font missing: {args.font}")
        return 1

    missing = missing_codepoints(args.font)
    if not missing:
        print(f"Chinese glyph coverage OK ({args.font.name}, {len(SOURCES)} sources)")
        return 0

    print(f"missing glyphs in {args.font}:")
    for codepoint in sorted(missing):
        files = ", ".join(sorted(missing[codepoint]))
        print(f"  U+{codepoint:04X} {chr(codepoint)!r} ({files})")
    return 2 if args.strict else 0


if __name__ == "__main__":
    sys.exit(main())
