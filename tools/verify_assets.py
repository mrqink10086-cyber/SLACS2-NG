#!/usr/bin/env python3
"""Report the overlay texture profiles shipped in package/Data.

The diffuse maps are required for every configured profile. Normal and
specular maps are optional by design: the RaceMenu adapter clears a channel
when the file is absent, so a Mega install without specular maps remains
valid. UBE is reported as missing because the current package has no
textures/slacs2_ube tree.
"""

from __future__ import annotations

from pathlib import Path

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

    ube = ROOT / "package" / "Data" / "Textures" / "slacs2_ube"
    print(f"UBE profile: {'available' if ube.is_dir() else 'missing resources (expected)'}")
    return total, normal_count, specular_count


def main() -> None:
    total, normal_count, specular_count = verify_textures()
    print(f"overlay textures: {total} diffuse, {normal_count} normal, {specular_count} specular")
    print("asset check ok")


if __name__ == "__main__":
    main()
