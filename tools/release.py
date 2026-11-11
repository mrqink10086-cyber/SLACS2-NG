#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

from clean_esp import verify as verify_esp
from verify_assets import verify_textures
from verify_chinese_font import DEFAULT_FONT, missing_codepoints

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "package" / "Data"
README = ROOT / "package" / "README.zh-CN.md"
TEXTURE_README = ROOT / "package" / "TEXTURES.zh-CN.md"
RELEASE_NOTES = ROOT / "docs" / "release" / "RELEASE-NOTES.zh-CN.md"
DEFAULT_OUTPUT = ROOT / "artifacts"
DEFAULT_NAME = "SLACS2NG-Main-1.0.7"
TEXTURES_NAME = "SLACS2NG-Textures-1.0"

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
    if not TEXTURE_README.is_file():
        raise SystemExit(f"texture package README missing: {TEXTURE_README}")
    if not RELEASE_NOTES.is_file():
        raise SystemExit(f"release notes missing: {RELEASE_NOTES}")


def collect(staging: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted(item for item in staging.rglob("*") if item.is_file()):
        relative = path.relative_to(staging).as_posix()
        if path.name in {"FILE-MANIFEST.json", "FILE-MANIFEST.txt"}:
            continue
        rows.append({
            "path": relative,
            "size": path.stat().st_size,
            "sha256": sha256(path),
        })
    return rows


def write_manifest(staging: Path, name: str, component: str, rows: list[dict[str, object]]) -> None:
    manifest = {
        "name": name,
        "component": component,
        "requires": {"textures": TEXTURES_NAME} if component == "main" else {},
        "generatedAtUtc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "fileCount": len(rows),
        "totalBytes": sum(int(row["size"]) for row in rows),
        "files": rows,
    }
    destination = staging / "Docs" / "SLACS2NG" / component
    (destination / "FILE-MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = [
        f"# {name}",
        f"# files: {manifest['fileCount']}  bytes: {manifest['totalBytes']}",
        "# sha256  size  path",
    ]
    lines.extend(f"{row['sha256']}  {row['size']:>12}  {row['path']}" for row in rows)
    (destination / "FILE-MANIFEST.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_zip(staging: Path, archive: Path) -> None:
    with zipfile.ZipFile(archive, "w", allowZip64=True) as bundle:
        for path in sorted(item for item in staging.rglob("*") if item.is_file()):
            relative = path.relative_to(staging).as_posix()
            compression = zipfile.ZIP_STORED if path.suffix.lower() == ".dds" else zipfile.ZIP_DEFLATED
            bundle.write(path, relative, compress_type=compression)


def stage_component(component: str, name: str, output: Path, no_zip: bool) -> None:
    output = output.resolve()
    staging = (output / name).resolve()
    if not name or name in {".", ".."} or "/" in name or "\\" in name or staging.parent != output:
        raise SystemExit("archive name must be a single directory name")
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    for source in sorted(path for path in PACKAGE.rglob("*") if path.is_file()):
        relative = source.relative_to(PACKAGE)
        is_texture = relative.parts[0].casefold() == "textures"
        if is_texture != (component == "textures"):
            continue
        destination = staging / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    documentation = staging / "Docs" / "SLACS2NG" / component
    documentation.mkdir(parents=True, exist_ok=True)
    shutil.copy2(TEXTURE_README if component == "textures" else README, documentation / "README.zh-CN.md")
    if component == "main":
        shutil.copy2(RELEASE_NOTES, documentation / "RELEASE-NOTES.zh-CN.md")

    rows = collect(staging)
    write_manifest(staging, name, component, rows)

    archive = output / f"{name}.zip"
    if not no_zip:
        write_zip(staging, archive)

    total = sum(int(row["size"]) for row in rows)
    print(f"staged {len(rows)} files, {total} bytes -> {staging}")
    if not no_zip:
        print(f"archive {archive.stat().st_size} bytes -> {archive}")
    print(f"manifest {documentation / 'FILE-MANIFEST.txt'}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--component", choices=("main", "textures", "all"), default="main")
    parser.add_argument("--name", default=DEFAULT_NAME, help="main component archive name")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--no-zip", action="store_true")
    args = parser.parse_args()

    verify_package()
    if args.component in {"main", "all"}:
        stage_component("main", args.name, args.output, args.no_zip)
    if args.component in {"textures", "all"}:
        stage_component("textures", TEXTURES_NAME, args.output, args.no_zip)


if __name__ == "__main__":
    main()
