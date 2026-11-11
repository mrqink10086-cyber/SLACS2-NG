#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import struct

from clean_esp import HEADER, u32

ROOT = Path(__file__).resolve().parent.parent
TEXTURES = ROOT / "package" / "Data" / "Textures" / "slacs2"

# region: (folder, prefix, texture_count, normal, specular)
PROFILES = {
    "BodyVaginal": ("Vaginal", "body", 12, True, True),
    "BodyAnal": ("Anal", "body", 12, True, True),
    "BodyBreast": ("Breast", "chest", 12, True, True),
    "BodyLegs": ("Footjob", "legs", 4, False, False),
    "Face": ("Oral", "head", 12, True, False),
    "Hands": ("Handjob", "hands", 4, False, False),
    "Feet": ("Footjob", "feet", 4, False, False),
}


def effect_shader_texture_paths(plugin: bytes) -> set[str]:
    paths: set[str] = set()
    position = 0
    while position < len(plugin):
        kind = plugin[position : position + 4]
        size = u32(plugin, position + 4)
        if kind != b"GRUP":
            position += HEADER + size
            continue
        end = position + size
        if plugin[position + 8 : position + 12] == b"EFSH":
            cursor = position + HEADER
            while cursor < end:
                record_end = cursor + HEADER + u32(plugin, cursor + 4)
                if record_end > end or u32(plugin, cursor + 8) & 0x00040000:
                    raise ValueError("invalid or compressed EFSH record")
                offset = cursor + HEADER
                while offset < record_end:
                    sub = plugin[offset : offset + 4]
                    length = struct.unpack_from("<H", plugin, offset + 4)[0]
                    value_end = offset + 6 + length
                    if value_end > record_end:
                        raise ValueError("invalid EFSH subrecord")
                    if sub in {b"ICON", b"ICO2", b"NAM7", b"NAM8", b"NAM9"}:
                        path = plugin[offset + 6 : value_end].rstrip(b"\0").decode("utf-8")
                        if path:
                            paths.add(path.replace("\\", "/"))
                    offset = value_end
                cursor = record_end
        position = end
    return paths


def verify_effect_shader_textures(package: Path) -> int:
    paths = effect_shader_texture_paths((package / "SLACS_2_Plus_SCO.esp").read_bytes())
    if not paths:
        raise SystemExit("ESP has no effect-shader texture references")
    missing = [path for path in sorted(paths) if not (package / "Textures" / path).is_file()]
    if missing:
        raise SystemExit("missing required effect-shader textures:\n  " + "\n  ".join(missing))
    print(f"effect-shader textures: {len(paths)} ESP-referenced files")
    return len(paths)


def verify_textures() -> tuple[int, int, int]:
    missing: list[str] = []
    normal_count = 0
    specular_count = 0
    total = 0
    for region, (folder, prefix, count, has_normal, has_specular) in PROFILES.items():
        directory = TEXTURES / folder
        for index in range(1, count + 1):
            for variant_prefix in ("", "m_"):
                diffuse = directory / f"{variant_prefix}{prefix}{index}.dds"
                if not diffuse.is_file():
                    missing.append(str(diffuse.relative_to(ROOT)))
                    continue
                total += 1
                normal = directory / f"{variant_prefix}{prefix}{index}_n.dds"
                if normal.is_file():
                    normal_count += 1
                elif has_normal and not variant_prefix:
                    print(f"optional normal missing: {normal.relative_to(ROOT)}")
                if not variant_prefix and has_specular:
                    specular = directory / f"{prefix}{index}_s.dds"
                    if specular.is_file():
                        specular_count += 1
                    else:
                        print(f"optional specular missing: {specular.relative_to(ROOT)}")
    if missing:
        raise SystemExit("missing required overlay textures:\n  " + "\n  ".join(missing))

    verify_effect_shader_textures(ROOT / "package" / "Data")
    ube = ROOT / "package" / "Data" / "Textures" / "slacs2_ube"
    print(f"UBE profile: {'available' if ube.is_dir() else 'missing resources (expected)'}")
    return total, normal_count, specular_count


def main() -> None:
    total, normal_count, specular_count = verify_textures()
    print(f"overlay textures: {total} diffuse, {normal_count} normal, {specular_count} specular")
    print("asset check ok")


if __name__ == "__main__":
    main()
