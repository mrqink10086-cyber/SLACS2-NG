#!/usr/bin/env python3
"""Stage the installable SLACS2 NG tree and produce release artifacts.

Run after ``xmake build -y "SLACS2 NG"`` (package.py runs as an after-build
step and refreshes ``package/Data``). The output is intentionally reproducible
in layout: a staging directory, a zip archive and SHA-256 manifests.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

from clean_esp import verify as verify_esp
from verify_assets import verify_textures
from verify_chinese_font import DEFAULT_FONT, missing_codepoints

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "package" / "Data"
README = ROOT / "package" / "README.zh-CN.md"
RELEASE_NOTES = ROOT / "docs" / "release" / "RELEASE-NOTES.zh-CN.md"
DEFAULT_OUTPUT = ROOT / "artifacts"
DEFAULT_NAME = "SLACS2NG-1.0.5"

REQUIRED_FILES = (
    "SKSE/Plugins/SLACS2 NG.dll",
    "SKSE/Plugins/SLACS2NG.json",
    "SKSE/Plugins/SKSEMenuFramework.ini",
    "SKSE/Plugins/SKSEMenuFrameworkStrings.json",
    "SKSE/Plugins/Fonts/NotoSansCJKsc-Regular.otf",
    "SKSE/Plugins/Fonts/NotoSansCJKsc-OFL.txt",
    "Scripts/SLACS_Main.pex",
    "SLACS_2_Plus_SCO.esp",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_package() -> None:
    missing = [relative for relative in REQUIRED_FILES if not (PACKAGE / relative).is_file()]
    if missing:
        raise SystemExit("package files missing: " + ", ".join(missing))

    verify_esp((PACKAGE / "SLACS_2_Plus_SCO.esp").read_bytes())
    verify_textures()
    if not DEFAULT_FONT.is_file():
        raise SystemExit(f"Chinese font missing: {DEFAULT_FONT}")
    missing_glyphs = missing_codepoints(DEFAULT_FONT)
    if missing_glyphs:
        detail = ", ".join(f"U+{code:04X}" for code in sorted(missing_glyphs))
        raise SystemExit(f"Chinese font is missing glyphs used by the UI: {detail}")
    if not README.is_file():
        raise SystemExit(f"package README missing: {README}")
    if not RELEASE_NOTES.is_file():
        raise SystemExit(f"release notes missing: {RELEASE_NOTES}")


def collect(staging: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted(item for item in staging.rglob("*") if item.is_file()):
        relative = path.relative_to(staging).as_posix()
        if relative in {"FILE-MANIFEST.json", "FILE-MANIFEST.txt"}:
            continue
        rows.append({
            "path": relative,
            "size": path.stat().st_size,
            "sha256": sha256(path),
        })
    return rows


def write_manifest(staging: Path, name: str, rows: list[dict[str, object]]) -> None:
    manifest = {
        "name": name,
        "generatedAtUtc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "fileCount": len(rows),
        "totalBytes": sum(int(row["size"]) for row in rows),
        "files": rows,
    }
    (staging / "FILE-MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = [
        f"# {name}",
        f"# files: {manifest['fileCount']}  bytes: {manifest['totalBytes']}",
        "# sha256  size  path",
    ]
    lines.extend(f"{row['sha256']}  {row['size']:>12}  {row['path']}" for row in rows)
    (staging / "FILE-MANIFEST.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_zip(staging: Path, archive: Path) -> None:
    with zipfile.ZipFile(archive, "w", allowZip64=True) as bundle:
        for path in sorted(item for item in staging.rglob("*") if item.is_file()):
            relative = path.relative_to(staging).as_posix()
            compression = zipfile.ZIP_STORED if path.suffix.lower() == ".dds" else zipfile.ZIP_DEFLATED
            bundle.write(path, relative, compress_type=compression)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default=DEFAULT_NAME)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--no-zip", action="store_true")
    args = parser.parse_args()

    verify_package()

    staging = args.output / args.name
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    shutil.copytree(PACKAGE, staging / "Data")
    shutil.copy2(README, staging / "README.zh-CN.md")
    shutil.copy2(RELEASE_NOTES, staging / "RELEASE-NOTES.zh-CN.md")

    rows = collect(staging)
    write_manifest(staging, args.name, rows)

    archive = args.output / f"{args.name}.zip"
    if archive.exists():
        archive.unlink()
    if not args.no_zip:
        write_zip(staging, archive)

    total = sum(int(row["size"]) for row in rows)
    print(f"staged {len(rows)} files, {total} bytes -> {staging}")
    if not args.no_zip:
        print(f"archive {archive.stat().st_size} bytes -> {archive}")
    print(f"manifest {staging / 'FILE-MANIFEST.txt'}")


if __name__ == "__main__":
    main()
