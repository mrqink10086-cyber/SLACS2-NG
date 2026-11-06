#!/usr/bin/env python3
"""Build the distributable Data tree for SLACS2 NG.

This is intentionally a build step rather than a one-off: it compiles the
Papyrus bridge, verifies the cleaned plugin and VMAD, checks the generated
SKSE metadata, and stages the DLL.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from clean_esp import verify as verify_esp
from verify_assets import verify_textures
from verify_chinese_font import DEFAULT_FONT, missing_codepoints

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "package" / "Data"
PSC = ROOT / "src" / "papyrus" / "SLACS_Main.psc"
PEX = PACKAGE / "Scripts" / "SLACS_Main.pex"
ESP = PACKAGE / "SLACS_2_Plus_SCO.esp"
CONFIG = PACKAGE / "SKSE" / "Plugins" / "SLACS2NG.json"
MENU_INI = PACKAGE / "SKSE" / "Plugins" / "SKSEMenuFramework.ini"
MENU_STRINGS = PACKAGE / "SKSE" / "Plugins" / "SKSEMenuFrameworkStrings.json"
FONT_LICENSE = PACKAGE / "SKSE" / "Plugins" / "Fonts" / "NotoSansCJKsc-OFL.txt"

MENU_STRING_KEYS = (
    "ModControlPanel",
    "Options",
    "Options.ResumeGame",
    "Options.OpenSettings",
    "Settings.Title",
    "Settings.MenuStyle",
    "Settings.ResetWindows",
    "Settings.FreezeTime",
    "Settings.BlurBackground",
    "Settings.ToggleMode.Keyboard",
    "Settings.ToggleKey.Keyboard",
    "Settings.ToggleMode.Gamepad",
    "Settings.ToggleKey.Gamepad",
)

REQUIRED_PEX_STRINGS = (
    "SLACS_Main",
    "RegisterEvents",
    "UnregisterEvents",
    "OnPPlusOrgasm",
    "OnPPlusStart",
    "OnPPlusEnd",
    "NotifyOrgasm",
    "NotifySceneStart",
    "NotifySceneEnd",
    "SexlabOrgasmSeparate",
    "HookAnimationStart",
    "HookAnimationEnd",
)

FORBIDDEN_PEX_STRINGS = (
    "MaxLayersPerPart",
    "ProcessComplexOrgasm",
    "SLACS_AffectedActors",
    "StartSlotBody",
)


def default_skyrim() -> Path:
    candidates = [
        os.environ.get("SLACS_SKYRIM_PATH"),
        r"E:/st/steamapps/common/Skyrim Special Edition",
        r"D:/tuanzimouse/Skyrim Special Edition",
    ]
    for candidate in candidates:
        if candidate and (Path(candidate) / "SkyrimSE.exe").is_file():
            return Path(candidate)
    raise SystemExit("Skyrim 1.6.1170 path not found; set SLACS_SKYRIM_PATH")


def compile_papyrus(skyrim: Path) -> None:
    compiler = skyrim / "Papyrus Compiler" / "PapyrusCompiler.exe"
    flags = skyrim / "Data" / "Source" / "Scripts" / "TESV_Papyrus_Flags.flg"
    imports = (
        PSC.parent,
        skyrim / "Data" / "Source" / "Scripts",
        skyrim / "Data" / "Scripts" / "Source",
    )
    for path in (compiler, flags, *imports):
        if not path.exists():
            raise SystemExit(f"Papyrus dependency missing: {path}")
    PEX.parent.mkdir(parents=True, exist_ok=True)
    if PEX.exists():
        PEX.unlink()
    command = [
        str(compiler),
        str(PSC),
        f"-f={flags}",
        "-i=" + ";".join(str(path) for path in imports),
        f"-o={PEX.parent}",
    ]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0 or not PEX.is_file() or PEX.stat().st_size == 0:
        raise SystemExit("Papyrus compilation failed")


def verify_pex() -> None:
    data = PEX.read_bytes()
    if data[:4] != b"\xfa\x57\xc0\xde":
        raise SystemExit(f"PEX has unexpected magic: {data[:4].hex()}")
    missing = [value for value in REQUIRED_PEX_STRINGS if value.encode("ascii") not in data]
    if missing:
        raise SystemExit(f"PEX missing strings: {', '.join(missing)}")
    stale = [value for value in FORBIDDEN_PEX_STRINGS if value.encode("ascii") in data]
    if stale:
        raise SystemExit(f"PEX contains legacy script strings: {', '.join(stale)}")


def verify_runtime_metadata() -> None:
    generated = list((ROOT / "build" / ".gens").glob("*/windows/x64/*/commonlibsse-ng-plugin.cpp"))
    if not generated:
        raise SystemExit("generated SKSE plugin metadata not found")
    text = generated[0].read_text(encoding="utf-8")
    normalized = "".join(text.split())
    if "SKSE::PluginDeclaration::RuntimeCompatibility{SKSE::PluginDeclaration::VersionNumber{1,6,1170,0}}" not in normalized:
        raise SystemExit(f"unexpected runtime metadata in {generated[0]}")


def verify_menu_assets() -> None:
    if not CONFIG.is_file() or CONFIG.stat().st_size == 0:
        raise SystemExit(f"settings file missing: {CONFIG}")
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    if config.get("schemaVersion") != 1:
        raise SystemExit("SLACS2NG.json must use schemaVersion 1")

    if not MENU_INI.is_file():
        raise SystemExit(f"menu framework ini missing: {MENU_INI}")
    ini = MENU_INI.read_text(encoding="utf-8")
    required_ini = ("EnableChinese = true", "PrimaryFont = NotoSansCJKsc-Regular.otf")
    for entry in required_ini:
        if entry not in ini:
            raise SystemExit(f"SKSEMenuFramework.ini missing: {entry}")

    if not MENU_STRINGS.is_file():
        raise SystemExit(f"menu strings missing: {MENU_STRINGS}")
    strings = json.loads(MENU_STRINGS.read_text(encoding="utf-8"))
    missing = [key for key in MENU_STRING_KEYS if not strings.get(key)]
    if missing:
        raise SystemExit(f"menu strings missing keys: {', '.join(missing)}")

    if not DEFAULT_FONT.is_file():
        raise SystemExit(f"Chinese font missing: {DEFAULT_FONT}")
    if not FONT_LICENSE.is_file():
        raise SystemExit(f"font license missing: {FONT_LICENSE}")
    missing_glyphs = missing_codepoints(DEFAULT_FONT)
    if missing_glyphs:
        detail = ", ".join(f"U+{code:04X}" for code in sorted(missing_glyphs))
        raise SystemExit(f"Chinese font is missing glyphs used by the UI: {detail}")


def stage_dll(dll: Path) -> None:
    if not dll.is_file() or dll.stat().st_size == 0:
        raise SystemExit(f"built DLL missing: {dll}")
    destination = PACKAGE / "SKSE" / "Plugins" / "SLACS2 NG.dll"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dll, destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--skyrim", type=Path)
    args = parser.parse_args()

    verify_esp(ESP.read_bytes())
    verify_textures()
    verify_menu_assets()
    compile_papyrus(args.skyrim or default_skyrim())
    verify_pex()
    verify_runtime_metadata()
    if args.dll:
        stage_dll(args.dll)
    print(f"package ready: {PACKAGE}")


if __name__ == "__main__":
    main()
